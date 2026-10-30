import hashlib
from io import BytesIO
import json
from pathlib import Path
import wave

from .audio import AudioError
from .config import ASSETS
from .llm import SUN_VARIANTS
from .schemas import AudioResult, Speaker


class ClassicClient:
    def __init__(self) -> None:
        path = ASSETS / "shaolan" / "classic_quotes.json"
        self.quotes = json.loads(path.read_text(encoding="utf-8"))
        if not self.quotes:
            raise ValueError("classic quote list is empty")

    async def generate(self, speaker: Speaker, question: str, history: list[dict], strict: bool = False) -> str:
        index = history[-1]["turn"] // 2 if history else 0
        if speaker == "sun":
            return SUN_VARIANTS[index % len(SUN_VARIANTS)]
        return self.quotes[index % len(self.quotes)]["text"]

    def audio_path(self, speaker: Speaker, text: str) -> Path:
        digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
        return ASSETS / "classic_audio" / speaker / f"{digest}.wav"

    async def synthesize(self, speaker: Speaker, text: str) -> AudioResult:
        path = self.audio_path(speaker, text)
        try:
            data = path.read_bytes()
            with wave.open(BytesIO(data), "rb") as wav:
                duration_ms = round(wav.getnframes() / wav.getframerate() * 1000)
        except (OSError, wave.Error) as exc:
            raise AudioError(f"classic audio is not pregenerated: {speaker}") from exc
        return AudioResult(wav_bytes=data, duration_ms=duration_ms)
