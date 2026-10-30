import asyncio
import logging
from pathlib import Path
import tempfile

from .audio import prepare_asr_audio


logger = logging.getLogger(__name__)


class ASRError(RuntimeError):
    pass


class QwenASR:
    def __init__(self, model_id: str, device_preference: str = "auto"):
        self.model_id = model_id
        self.device_preference = device_preference
        self.device = ""
        self.model = None
        self.processor = None
        self.torch = None
        self._load_lock = asyncio.Lock()

    async def load(self) -> None:
        async with self._load_lock:
            if self.model is not None:
                return
            await asyncio.to_thread(self._load_initial)

    def _load_initial(self) -> None:
        import torch
        self.torch = torch
        device = self.device_preference
        if device == "auto":
            device = "mps" if torch.backends.mps.is_available() else "cpu"
        self._load_on(device)

    def _load_on(self, device: str) -> None:
        from transformers import AutoModelForMultimodalLM, AutoProcessor
        dtype = self.torch.float16 if device == "mps" else self.torch.float32
        processor = AutoProcessor.from_pretrained(self.model_id)
        model = AutoModelForMultimodalLM.from_pretrained(self.model_id, dtype=dtype).to(device).eval()
        self.processor = processor
        self.model = model
        self.device = device
        logger.info("Qwen ASR loaded on %s with %s", device, dtype)

    async def transcribe_wav(self, data: bytes) -> str:
        await self.load()
        converted = await prepare_asr_audio(data)
        try:
            return await asyncio.to_thread(self._transcribe, converted)
        except Exception as exc:
            if self.device_preference == "auto" and self.device == "mps":
                logger.warning("Qwen ASR MPS inference failed; retrying once on CPU: %s", exc)
                await asyncio.to_thread(self._load_on, "cpu")
                return await asyncio.to_thread(self._transcribe, converted)
            raise ASRError(str(exc)) from exc

    def _transcribe(self, data: bytes) -> str:
        path = ""
        try:
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as temp:
                temp.write(data)
                path = temp.name
            inputs = self.processor.apply_transcription_request(
                audio=path,
                language="Chinese",
                prompt="Vocabulary: 邵艾伦, 孙宇晨, 薄肌, 自媒体, AI.",
            ).to(self.device, self.model.dtype)
            with self.torch.inference_mode():
                output = self.model.generate(**inputs, max_new_tokens=256, do_sample=False)
            generated = output[:, inputs["input_ids"].shape[1]:]
            return self.processor.decode(generated, return_format="transcription_only")[0].strip()
        finally:
            if path:
                Path(path).unlink(missing_ok=True)
