import re
import httpx
from .personas import system_prompt
from .schemas import Speaker


SUN_VARIANTS = (
    "你说太对了！", "对，你说的太对了！", "对，你说的太对了，对吧？", "你说的太对了！", "你讲的太对了！",
    "哈哈，你说的太对了！", "哎，你说太对了！", "对对，是太对了，对吧？", "你讲的太对了！", "你说的太对了！",
    "太对了！", "你说太对了！", "你讲的太对了！", "你说的太对了！",
    "对，你讲太对了！", "你说的太对了！", "就就跟这个，你刚才讲的太对了！", "对，你说的太对了哈！",
    "你说的太对了，对吧？", "哎，你说的太对了！", "你说的太对了！",
    "哎，你讲的太对了！", "对，你说的太对了！", "对，你说的太对了！", "对，你说的太对了，对吧？",
)


class LLMError(RuntimeError):
    pass


def clean_reply(text: str) -> str:
    text = text.strip()
    text = re.sub(r"^(邵艾伦|孙宇晨|Alan|Justin Sun)\s*[：:]\s*", "", text, flags=re.I)
    text = text.strip().strip('"“”')
    text = re.sub(r"^[-*>\s]+", "", text)
    if "\n" in text or "\r" in text:
        raise LLMError("reply is not single-line")
    chinese = len(re.findall(r"[\u4e00-\u9fff]", text))
    if not text or chinese < 2 or chinese > 50 or len(text) > 80:
        raise LLMError("reply length is outside the one-sentence contract")
    if len(re.findall(r"[。！？!?]", text)) > 1:
        raise LLMError("reply contains multiple sentences")
    return text


class DeepSeekClient:
    def __init__(self, api_key: str, base_url: str):
        self.api_key = api_key
        self.base_url = base_url.rstrip("/")

    async def generate(self, speaker: Speaker, question: str, history: list[dict], strict: bool = False) -> str:
        if speaker == "sun":
            index = sum(item["speaker"] == "sun" for item in history) % len(SUN_VARIANTS)
            return SUN_VARIANTS[index]
        if not self.api_key:
            raise LLMError("DEEPSEEK_API_KEY is not configured")
        transcript = "\n".join(f"{item['speaker']}: {item['text']}" for item in history[-12:])
        instruction = (
            f"原始问题：{question}\n最近对话：\n{transcript or '（还没有，轮到你第一个接话）'}\n"
            f"现在轮到{speaker}。只接一句。"
        )
        instruction += (
            "你是观点输出者，不是复读机。先检查此前所有邵艾伦发言，必须换一个此前没有用过的核心论点。"
            "从薄肌与主动权、版本答案、内归因、睡眠、创造与消费、平台逻辑、复利、多巴胺、AI掌舵中，"
            "选一个尚未使用的角度；不能用相同论据再次说睡眠、短视频或薄肌。"
            "只留一个判断，4–22个汉字，不写两层解释，不要用逗号，不要排比，像原仓库短句一样直接。"
        )
        if strict:
            instruction += "上次输出不合格。严格按当前人物的口头禅/观点约束再写一次，禁止换行和解释。"
        payload = {
            "model": "deepseek-flash",
            "thinking": {"type": "disabled"},
            "max_tokens": 120,
            "temperature": 0.9,
            "messages": [
                {"role": "system", "content": system_prompt(speaker)},
                {"role": "user", "content": instruction},
            ],
        }
        headers = {"Authorization": f"Bearer {self.api_key}"}
        try:
            async with httpx.AsyncClient(timeout=httpx.Timeout(60.0, connect=10.0)) as client:
                response = await client.post(f"{self.base_url}/chat/completions", headers=headers, json=payload)
        except httpx.HTTPError as exc:
            raise LLMError("DeepSeek network error") from exc
        if response.status_code != 200:
            raise LLMError(f"DeepSeek returned HTTP {response.status_code}")
        try:
            content = response.json()["choices"][0]["message"]["content"]
        except (KeyError, IndexError, TypeError, ValueError) as exc:
            raise LLMError("DeepSeek response has no message content") from exc
        text = clean_reply(content)
        if speaker == "sun" and text not in SUN_VARIANTS:
            raise LLMError("SunPet reply is not a classic short catchphrase")
        if speaker == "shaolan":
            chinese = len(re.findall(r"[\u4e00-\u9fff]", text))
            if chinese > 22 or any(mark in text for mark in "，,；;"):
                raise LLMError("Shaolan reply is not a concise single idea")
        return text

    async def generate_with_retry(self, speaker: Speaker, question: str, history: list[dict]) -> str:
        try:
            return await self.generate(speaker, question, history)
        except LLMError as exc:
            message = str(exc)
            if "not configured" in message or "HTTP 4" in message:
                raise
        return await self.generate(speaker, question, history, strict=True)
