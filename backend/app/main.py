import asyncio
from contextlib import asynccontextmanager
from io import BytesIO
import logging
from time import perf_counter
import wave

from fastapi import FastAPI, HTTPException, Request, Response
from pydantic import BaseModel

from .asr import ASRError, QwenASR
from .audio import AudioError
from .classic import ClassicClient
from .config import Settings
from .conversation import ConversationEngine, SessionConflict, SessionNotFound
from .llm import LLMError
from .tts import TTSError, VoxCPMClient


MAX_UPLOAD_BYTES = 2 * 1024 * 1024


class NextRequest(BaseModel):
    after_turn: int


def create_app(asr=None, llm=None, tts=None, load_asr_on_startup: bool = True) -> FastAPI:
    settings = Settings.from_env()
    asr = asr or QwenASR(settings.asr_model, settings.asr_device)
    classic = ClassicClient()
    tts = tts or VoxCPMClient(settings.modelbest_api_key, settings.modelbest_base_url, settings.modelbest_model)
    engine = ConversationEngine(classic.generate, classic.synthesize)
    asr_lock = asyncio.Lock()

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        if load_asr_on_startup:
            await asr.load()
        yield

    app = FastAPI(title="ESP32 Sage", version="0.1.0", lifespan=lifespan)
    app.state.asr = asr
    app.state.engine = engine

    @app.get("/healthz")
    async def healthz():
        return {
            "status": "ok",
            "asr_loaded": getattr(asr, "model", None) is not None,
            "asr_device": getattr(asr, "device", ""),
        }

    @app.post("/v1/auto-sessions", status_code=201)
    async def create_auto_session():
        try:
            session, turn = await engine.create_session("4.5小时访谈经典语录")
            return {"session_id": session.session_id, "turn": turn}
        except (LLMError, TTSError, AudioError) as exc:
            raise HTTPException(502, str(exc)) from exc

    @app.post("/v1/sessions", status_code=201)
    async def create_session(request: Request):
        data = await request.body()
        if not data or len(data) > MAX_UPLOAD_BYTES:
            raise HTTPException(413, "recording is empty or exceeds 20 seconds")
        try:
            with wave.open(BytesIO(data), "rb") as wav:
                seconds = wav.getnframes() / wav.getframerate()
                if seconds < 0.2 or seconds > 20.0:
                    raise HTTPException(422, "recording must be between 0.2 and 20 seconds")
        except (wave.Error, EOFError) as exc:
            raise HTTPException(415, "expected a valid PCM WAV recording") from exc
        try:
            started = perf_counter()
            async with asr_lock:
                question = await asr.transcribe_wav(data)
            logging.getLogger(__name__).info("ASR ready in %.2fs", perf_counter() - started)
            if not question.strip():
                raise HTTPException(422, "speech was not recognized")
            session, turn = await engine.create_session(question)
            return {"session_id": session.session_id, "question": question, "turn": turn}
        except HTTPException:
            raise
        except (ASRError, LLMError, TTSError, AudioError) as exc:
            raise HTTPException(502, str(exc)) from exc

    @app.post("/v1/sessions/{session_id}/turns:next")
    async def next_turn(session_id: str, body: NextRequest):
        try:
            turn = await engine.next_turn(session_id, body.after_turn)
            return {"turn": turn}
        except asyncio.CancelledError as exc:
            raise HTTPException(409, "session cancelled") from exc
        except SessionNotFound as exc:
            raise HTTPException(404, "session not found") from exc
        except SessionConflict as exc:
            raise HTTPException(409, str(exc)) from exc
        except (LLMError, TTSError, AudioError) as exc:
            raise HTTPException(502, str(exc)) from exc

    @app.get("/v1/sessions/{session_id}/audio/{turn}.wav")
    async def audio(session_id: str, turn: int):
        try:
            content = engine.get_audio(session_id, turn)
            return Response(content=content, media_type="audio/wav")
        except SessionNotFound as exc:
            raise HTTPException(404, "audio not found") from exc

    @app.post("/v1/sessions/{session_id}/cancel", status_code=204)
    async def cancel(session_id: str):
        await engine.cancel(session_id)
        return Response(status_code=204)

    return app


app = create_app(load_asr_on_startup=False)
