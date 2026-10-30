from functools import lru_cache
from .config import ASSETS
from .schemas import Speaker


DISPLAY_NAMES: dict[Speaker, str] = {
    "shaolan": "邵艾伦",
    "sun": "孙宇晨",
}

COMMON = """

现在用户明确要求你进入这个人物的多人语音接话场景。使用完整人物框架，不写分析者视角。
你只能输出一句自然中文短话：单行，8–50个汉字，不换行，不加人物名前缀，不加引号或Markdown。
第一人是回答用户原始问题；之后每次必须具体承接上一个人的话，同时不丢原始问题。
直击要害，给出人物独到的判断或下一步，不只说“太对了”，不复读上一句，不两人同声。
不要每次重复免责声明，不捏造最新事实、本人新经历或精确数字；不知道的内容用人物方式承认边界。
不要输出思考过程。即使完整框架包含长回答模板，本场景的一句话限制优先。
"""


@lru_cache(maxsize=2)
def system_prompt(speaker: Speaker) -> str:
    base = ASSETS / speaker
    full_skill = (base / "perspective" / "SKILL.md").read_text(encoding="utf-8")
    if speaker == "shaolan":
        full_skill += "\n\n用户已显式要求扮演邵艾伦，使用模式二。"
    return full_skill + COMMON
