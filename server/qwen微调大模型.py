# finetune_qwen_lora_merge.py
# ============================================================
# 功能：对 Qwen GPTQ‑Int8 模型进行 LoRA 微调 ➜
#      1. 训练并保存 Adapter（增量权重）
#      2. 再次加载基座模型 + Adapter，执行 merge_and_unload()
#         保存“完整合并模型”，可直接推理部署
# ============================================================

from torch.utils.data import Dataset, DataLoader
from transformers import AutoTokenizer, AutoModelForCausalLM
import torch
from peft import LoraConfig, get_peft_model, TaskType, PeftModel
from tqdm import tqdm
from torch import tensor
import json
import os

# ------------------------ 1. 数据集定义 ------------------------
class MyDataSet(Dataset):
    """简单的问答 JSONL 数据集，每行包含 {"question": "...", "answer": "..."}"""
    def __init__(self, data_path: str, tokenizer) -> None:
        super().__init__()
        self.tokenizer = tokenizer
        self.max_len = 512
        self.samples = []

        if data_path:
            with open(data_path, "r", encoding="utf-8") as f:
                for line in f:
                    if not line.strip():
                        continue
                    obj = json.loads(line)
                    self.samples.append({"question": obj["question"],
                                         "answer": obj["answer"]})
        print(f"加载数据：{len(self.samples)} 条")

    # 构造单条样本
    def preprocess(self, question, answer):
        # ① 按 Qwen Chat 模板构造 prompt
        msgs = [{"role": "user", "content": question}]
        prompt = self.tokenizer.apply_chat_template(
            msgs, tokenize=False, add_generation_prompt=True
        )
        # ② 分别编码指令与回复
        ins_enc = self.tokenizer(prompt, add_special_tokens=False,
                                 truncation=True, max_length=self.max_len)
        ans_enc = self.tokenizer(answer, add_special_tokens=False,
                                 truncation=True, max_length=self.max_len)
        # ③ 拼接 input_ids / attention / labels
        input_ids = ins_enc["input_ids"] + ans_enc["input_ids"] + [self.tokenizer.pad_token_id]
        attn_mask = ins_enc["attention_mask"] + ans_enc["attention_mask"] + [1]
        labels = [-100] * len(ins_enc["input_ids"]) + ans_enc["input_ids"] + [self.tokenizer.pad_token_id]
        return input_ids, attn_mask, labels

    def __getitem__(self, idx):
        ids, mask, lbl = self.preprocess(**self.samples[idx])
        return {"input_ids": tensor(ids),
                "attention_mask": tensor(mask),
                "labels": tensor(lbl)}

    def __len__(self):
        return len(self.samples)


# ------------------------ 2. 路径与参数 ------------------------
base_model_path = "./qwen/model/Qwen2.5-3B-Instruct"  # ★GPTQ Int8基座
data_file       = "./data.json"
adapter_dir     = "./qyAI/output_adapter"   # 仅 Adapter
merged_dir      = "./qyAI/output_full"      # 合并后完整模型
epochs          = 3
lr              = 1e-4
batch_size      = 1

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

# ------------------------ 3. 加载 Tokenizer & 基座模型 ------------------------
tokenizer = AutoTokenizer.from_pretrained(base_model_path, trust_remote_code=True)
# GPTQ 推荐使用 fp16 计算，device_map="auto" 自动放 GPU / CPU
base_model = AutoModelForCausalLM.from_pretrained(
    base_model_path,
    device_map="auto",
    torch_dtype=torch.float16
)

# ------------------------ 4. 注入 LoRA ------------------------
peft_cfg = LoraConfig(
    task_type      = TaskType.CAUSAL_LM,
    target_modules = ["q_proj", "k_proj", "v_proj", "o_proj",
                      "gate_proj", "up_proj", "down_proj"],
    r              = 8,
    lora_alpha     = 32,
    lora_dropout   = 0.1,
    inference_mode = False,
)
model = get_peft_model(base_model, peft_cfg)
model.print_trainable_parameters()

# ------------------------ 5. 数据加载 ------------------------
dataset   = MyDataSet(data_file, tokenizer)
dataloader = DataLoader(dataset, batch_size=batch_size, shuffle=True)

# ------------------------ 6. LoRA 训练 ------------------------
optimizer = torch.optim.AdamW(model.parameters(), lr=lr)
model.train()

for epoch in range(epochs):
    pbar = tqdm(dataloader, desc=f"Epoch {epoch+1}/{epochs}")
    for batch in pbar:
        input_ids      = batch["input_ids"].to(device)
        attention_mask = batch["attention_mask"].to(device)
        labels         = batch["labels"].to(device)

        loss = model(input_ids=input_ids,
                     attention_mask=attention_mask,
                     labels=labels).loss
        loss.backward()
        optimizer.step()
        optimizer.zero_grad()
        pbar.set_postfix(loss=f"{loss.item():.4f}")

# ------------------------ 7. 保存 Adapter ------------------------
os.makedirs(adapter_dir, exist_ok=True)
model.save_pretrained(adapter_dir)
tokenizer.save_pretrained(adapter_dir)
print(f"LoRA Adapter 已保存到：{adapter_dir}")

# ------------------------ 8. 合并 Adapter ➜ 完整模型 ------------------------
print("开始合并 LoRA Adapter 与基座模型 …")
# 重新加载基座（保持量化/精度一致）再载入 Adapter
merged_model = AutoModelForCausalLM.from_pretrained(
    base_model_path,
    device_map="auto",
    torch_dtype=torch.float16,
    trust_remote_code=True,
)
merged_model = PeftModel.from_pretrained(merged_model, adapter_dir)
# merge_and_unload 会把 LoRA 权重注入原层，并卸载多余模块
merged_model = merged_model.merge_and_unload()

# 保存完整模型
os.makedirs(merged_dir, exist_ok=True)
merged_model.save_pretrained(merged_dir)
tokenizer.save_pretrained(merged_dir)
print(f"✅ 合并完成！完整模型已保存到：{merged_dir}")
