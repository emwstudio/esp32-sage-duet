import asyncio
from dataclasses import dataclass, field
import logging
import time
from typing import Awaitable, Callable
from uuid import uuid4

from .schemas import AudioResult, Speaker, Turn


SPEAKERS: tuple[Speaker, ...] = ("shaolan", "sun")
SESSION_TTL_SECONDS = 600


class SessionNotFound(LookupError):
    pass


class SessionConflict(RuntimeError):
    pass


class SessionCancelled(RuntimeError):
    pass


@dataclass
class Session:
    session_id: str
    question: str
    turns: list[Turn] = field(default_factory=list)
    audio: dict[int, bytes] = field(default_factory=dict)
    last_active: float = field(default_factory=time.monotonic)
    cancelled: bool = False
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    current_task: asyncio.Task | None = None


Generate = Callable[[Speaker, str, list[dict]], Awaitable[str]]
Synthesize = Callable[[Speaker, str], Awaitable[AudioResult]]


class ConversationEngine:
    def __init__(self, generate: Generate, synthesize: Synthesize):
        self.generate = generate
        self.synthesize = synthesize
        self.sessions: dict[str, Session] = {}

    def _get(self, session_id: str) -> Session:
        self.expire_idle()
        session = self.sessions.get(session_id)
        if session is None or session.cancelled:
            raise SessionNotFound(session_id)
        return session

    def expire_idle(self) -> None:
        cutoff = time.monotonic() - SESSION_TTL_SECONDS
        expired = [
            session_id for session_id, session in self.sessions.items()
            if session.last_active < cutoff and session.current_task is None
        ]
        for session_id in expired:
            self.sessions.pop(session_id, None)

    async def create_session(self, question: str) -> tuple[Session, Turn]:
        question = question.strip()
        if not question:
            raise ValueError("question is empty")
        session = Session(session_id=uuid4().hex, question=question)
        self.sessions[session.session_id] = session
        try:
            turn = await self.next_turn(session.session_id, 0)
        except Exception:
            self.sessions.pop(session.session_id, None)
            raise
        return session, turn

    async def next_turn(self, session_id: str, after_turn: int) -> Turn:
        session = self._get(session_id)
        async with session.lock:
            session = self._get(session_id)
            if after_turn < 0 or after_turn > len(session.turns):
                raise SessionConflict("after_turn is outside the session")
            if after_turn < len(session.turns):
                return session.turns[after_turn]
            if session.cancelled:
                raise SessionCancelled(session_id)
            session.current_task = asyncio.current_task()
            session.last_active = time.monotonic()
            number = len(session.turns) + 1
            speaker = SPEAKERS[(number - 1) % len(SPEAKERS)]
            history = [
                {"speaker": turn.speaker, "text": turn.text, "turn": turn.turn}
                for turn in session.turns[-12:]
            ]
            try:
                started = time.perf_counter()
                text = await self.generate(speaker, session.question, history)
                logging.getLogger(__name__).info(
                    "turn %d %s text ready in %.2fs", number, speaker, time.perf_counter() - started
                )
                if session.cancelled:
                    raise SessionCancelled(session_id)
                started = time.perf_counter()
                audio = await self.synthesize(speaker, text)
                logging.getLogger(__name__).info(
                    "turn %d %s audio ready in %.2fs", number, speaker, time.perf_counter() - started
                )
                if session.cancelled:
                    raise SessionCancelled(session_id)
                turn = Turn(
                    turn=number,
                    speaker=speaker,
                    text=text,
                    audio_url=f"/v1/sessions/{session_id}/audio/{number}.wav",
                    duration_ms=audio.duration_ms,
                )
                session.turns.append(turn)
                session.audio[number] = audio.wav_bytes
                session.last_active = time.monotonic()
                return turn
            finally:
                session.current_task = None

    def get_audio(self, session_id: str, turn: int) -> bytes:
        session = self._get(session_id)
        try:
            return session.audio[turn]
        except KeyError as exc:
            raise SessionNotFound(f"audio {turn}") from exc

    async def cancel(self, session_id: str) -> None:
        session = self.sessions.pop(session_id, None)
        if session is None:
            return
        session.cancelled = True
        if session.current_task is not None:
            session.current_task.cancel()
