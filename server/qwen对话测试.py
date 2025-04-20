from transformers import AutoTokenizer, AutoModelForCausalLM
import torch

model_name = "./qyAI/output_full"

# 检查是否有可用的GPU
device = "cuda" if torch.cuda.is_available() else "cpu"

# 加载模型和tokenizer
model = AutoModelForCausalLM.from_pretrained(model_name)
tokenizer = AutoTokenizer.from_pretrained(model_name, trust_remote_code=True)

# 将模型移动到GPU或CPU
model.to(device)


# 只允许你训练集里出现过的特殊命令
system_prompt = (
    "你是秋原管家，既是智能家居控制助手，也可以作为陪聊和问答助手。\n"
    "— 如果用户输入以下家电命令，请在回答末尾附加对应的命令标记：\n"
    "  <|fan_on|>, <|fan_off|>, <|light_on|>, <|light_off|>,\n"
    "  <|fan_speed_up|>, <|fan_speed_down|>, <|fan_high|>,\n"
    "  <|ac_on|>, <|ac_off|>, <|get_temperature|>, <|get_humidity|>,\n"
    "  <|window_open|>, <|window_close|>, <|help|>, <|status|>\n"
    "— 如果用户的问题是其他内容（闲聊、知识问答、建议等），\n"
    "  请用自然语言直接回答，不要输出任何 <|…|> 标记。\n"
    "  此 <|…|> 标记，是为上面的单片机标识控制命令的，因此非明确控制场景不可使用命令标识。\n"
)

# 初始化messages为空列表，用来存储每次的对话
messages = [{"role": "system", "content": system_prompt}]

while True:
    # 从控制台输入
    user_input = input("请输入: ")

    # 如果输入"退出"，则跳出循环
    if user_input.lower() == "退出":
        print("退出对话。")
        break

    # 添加用户输入到对话记录
    messages.append({"role": "user", "content": user_input})

    # 创建输入的prompt
    prompt = tokenizer.apply_chat_template(
        messages,  # 传递所有的对话记录
        tokenize=False,
        add_generation_prompt=True
    )

    # 对输入文本进行tokenize
    instruction = tokenizer(
        prompt,
        add_special_tokens=False,  # 自动添加特殊标记
        max_length=512,            # 最大长度
        truncation=True,           # 超出最大长度时自动截断
        return_tensors="pt"
    )

    # 将生成的输入移动到正确的设备（GPU或CPU）
    instruction = {k: v.to(device) for k, v in instruction.items()}

    # 生成模型的输出
    generated_ids = model.generate(
        input_ids=instruction['input_ids'],
        attention_mask=instruction['attention_mask'],
        pad_token_id=tokenizer.pad_token_id,
        max_new_tokens=258
    )

    # 只保留生成的部分
    generated_ids = [generated_ids[0][len(instruction['input_ids'][0]):]]

    # 解码生成的ID
    response = tokenizer.batch_decode(generated_ids, skip_special_tokens=True)[0]

    # 输出生成的回应
    print(f"AI回应: {response}")
    
    # 将AI的回应也加入到对话记录中，保持记忆
    messages.append({"role": "assistant", "content": response})
