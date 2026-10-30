from dataclasses import dataclass
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets" / "personas"


@dataclass(frozen=True)
class Settings:
    deepseek_api_key: str
    deepseek_base_url: str
    modelbest_api_key: str
    modelbest_base_url: str
    modelbest_model: str
    asr_model: str
    asr_device: str

    @classmethod
    def from_env(cls) -> "Settings":
        return cls(
            deepseek_api_key=os.getenv("DEEPSEEK_API_KEY", ""),
            deepseek_base_url=os.getenv("DEEPSEEK_BASE_URL", "https://api.deepseek.com").rstrip("/"),
            modelbest_api_key=os.getenv("MODELBEST_API_KEY", ""),
            modelbest_base_url=os.getenv("MODELBEST_BASE_URL", "https://api.modelbest.cn").rstrip("/"),
            modelbest_model=os.getenv("MODELBEST_MODEL", "VoxCPM2"),
            asr_model=os.getenv("ASR_MODEL", "Qwen/Qwen3-ASR-1.7B-hf"),
            asr_device=os.getenv("ASR_DEVICE", "auto"),
        )
