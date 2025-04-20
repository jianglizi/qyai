from fastapi import FastAPI, File, UploadFile
from pydantic import BaseModel
from transformers import AutoTokenizer, AutoModelForCausalLM
import torch

import whisper
from io import BytesIO

app = FastAPI(title="秋原管家对话 API")

# 1. 加载模型与 tokenizer
MODEL_PATH = "./qyAI/output_full"
device = "cuda" if torch.cuda.is_available() else "cpu"

model = AutoModelForCausalLM.from_pretrained(MODEL_PATH)
tokenizer = AutoTokenizer.from_pretrained(MODEL_PATH, trust_remote_code=True)
model.to(device)


# 加载 Whisper 模型
whisper_model = whisper.load_model("small")  # 选择模型（tiny, base，small，medium，large）


# 2. 系统提示
SYSTEM_PROMPT = (
    "你是秋原管家，既是智能家居控制助手，也可以作为陪聊和问答助手。\n"
    "— 如果用户输入以下家电命令，请在回答末尾附加对应的命令标记：\n"
    "  <|fan_on|>, <|fan_off|>, <|light_on|>, <|light_off|>,\n"
    "  <|fan_speed_up|>, <|fan_speed_down|>, <|fan_high|>,\n"
    "  <|ac_on|>, <|ac_off|>, <|get_temperature|>, <|get_humidity|>,\n"
    "  <|window_open|>, <|window_close|>, <|status|>\n"
    "— 如果用户的问题是其他内容（闲聊、知识问答、建议等），\n"
    "  请用自然语言直接回答，不要输出任何 <|…|> 标记。"
)

# 3. 定义请求体，只接收一个字符串
class ChatRequest(BaseModel):
    message: str

class ChatResponse(BaseModel):
    reply: str

# 4. 聊天接口
@app.post("/chat/", response_model=ChatResponse)
def chat(req: ChatRequest):
    # system + user
    msgs = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user",   "content": req.message}
    ]

    # 拼 prompt
    prompt = tokenizer.apply_chat_template(
        msgs,
        tokenize=False,
        add_generation_prompt=True
    )
    inputs = tokenizer(
        prompt,
        add_special_tokens=False,
        max_length=512,
        truncation=True,
        return_tensors="pt"
    )
    inputs = {k: v.to(device) for k, v in inputs.items()}

    # 推理
    with torch.no_grad():
        gen_ids = model.generate(
            input_ids=inputs["input_ids"],
            attention_mask=inputs["attention_mask"],
            pad_token_id=tokenizer.pad_token_id,
            max_new_tokens=256
        )

    # 切出新生成部分并解码
    gen_ids = gen_ids[0][ inputs["input_ids"].shape[-1] : ]
    reply = tokenizer.decode(gen_ids, skip_special_tokens=True)

    return ChatResponse(reply=reply)

# 5. 语音识别接口
@app.post("/stt/")
async def stt(audio: UploadFile = File(...)):
    # 保存音频文件
    audio_bytes = await audio.read()
    audio_path = "uploaded_audio.wav"
    with open(audio_path, "wb") as f:
        f.write(audio_bytes)

    # 使用 Whisper 进行语音识别
    audio = whisper.load_audio(audio_path)
    audio = whisper.pad_or_trim(audio)
    result = whisper_model.transcribe(audio, language="zh")

    # 返回识别结果
    return {"text": result['text']}

# 启动命令  uvicorn app:app --reload --host 0.0.0.0 --port 8000