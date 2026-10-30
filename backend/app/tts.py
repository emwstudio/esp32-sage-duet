import base64
import json
import httpx

from .audio import normalize_tts_audio
from .config import ASSETS
from .schemas import AudioResult, Speaker


class TTSError(RuntimeError):
    pass


class VoxCPMClient:
    def __init__(self, api_key: str, base_url: str, model: str):
        self.api_key = api_key
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.references: dict[Speaker, str] = {}

    def reference(self, speaker: Speaker) -> str:
        if speaker not in self.references:
            audio = (ASSETS / speaker / "reference.wav").read_bytes()
            self.references[speaker] = "data:audio/wav;base64," + base64.b64encode(audio).decode("ascii")
        return self.references[speaker]

    async def _synthesize_once(self, speaker: Speaker, text: str) -> AudioResult:
        if not self.api_key:
            raise TTSError("MODELBEST_API_KEY is not configured")
        spoken_text = text.replace("薄肌", "搏击") if speaker == "shaolan" else text
        payload = {"model": self.model, "input": spoken_text, "ref_audio": self.reference(speaker)}
        headers = {"Authorization": f"Bearer {self.api_key}"}
        chunks: list[bytes] = []
        done = False
        async with httpx.AsyncClient(timeout=httpx.Timeout(120.0, connect=10.0)) as client:
            async with client.stream(
                "POST", f"{self.base_url}/v1/audio/speech", headers=headers, json=payload
            ) as response:
                if response.status_code != 200:
                    await response.aread()
                    raise TTSError(f"ModelBest returned HTTP {response.status_code}")
                async for line in response.aiter_lines():
                    if not line.startswith("data:"):
                        continue
                    try:
                        event = json.loads(line[5:].strip())
                    except json.JSONDecodeError as exc:
                        raise TTSError("ModelBest returned malformed SSE JSON") from exc
                    event_type = event.get("type")
                    if event_type == "speech.audio.delta":
                        encoded = event.get("audio")
                        if not isinstance(encoded, str):
                            raise TTSError("ModelBest audio delta has no audio field")
                        chunks.append(base64.b64decode(encoded, validate=True))
                    elif event_type == "speech.audio.done":
                        done = True
        if not done:
            raise TTSError("ModelBest stream ended without speech.audio.done")
        if not chunks:
            raise TTSError("ModelBest returned no audio")
        return await normalize_tts_audio(b"".join(chunks))

    async def synthesize(self, speaker: Speaker, text: str) -> AudioResult:
        try:
            return await self._synthesize_once(speaker, text)
        except httpx.HTTPError:
            return await self._synthesize_once(speaker, text)
