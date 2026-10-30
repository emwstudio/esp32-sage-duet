from dataclasses import dataclass
from typing import Literal


Speaker = Literal["shaolan", "sun"]


@dataclass(frozen=True)
class Turn:
    turn: int
    speaker: Speaker
    text: str
    audio_url: str
    duration_ms: int


@dataclass(frozen=True)
class AudioResult:
    wav_bytes: bytes
    duration_ms: int
