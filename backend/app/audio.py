import asyncio
from io import BytesIO
import struct
import wave

from .schemas import AudioResult


MAX_DEVICE_WAV_BYTES = 2 * 1024 * 1024


class AudioError(RuntimeError):
    pass


async def convert_wav(data: bytes, sample_rate: int, channels: int, filters: str = "") -> bytes:
    arguments = [
        "ffmpeg", "-hide_banner", "-loglevel", "error",
        "-i", "pipe:0", "-c:a", "pcm_s16le", "-ar", str(sample_rate),
    ]
    if filters:
        arguments += ["-af", filters]
    arguments += ["-ac", str(channels), "-f", "wav", "pipe:1"]
    process = await asyncio.create_subprocess_exec(
        *arguments,
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    stdout, stderr = await process.communicate(data)
    if process.returncode != 0:
        raise AudioError(f"ffmpeg could not convert audio: {stderr.decode(errors='replace')[:500]}")
    if len(stdout) >= 44 and stdout[:4] == b"RIFF" and stdout[8:12] == b"WAVE":
        finalized = bytearray(stdout)
        if finalized[4:8] == b"\xff\xff\xff\xff":
            finalized[4:8] = struct.pack("<I", len(finalized) - 8)
        offset = 12
        while offset + 8 <= len(finalized):
            chunk = bytes(finalized[offset:offset + 4])
            size = struct.unpack("<I", finalized[offset + 4:offset + 8])[0]
            if chunk == b"data":
                if size == 0xFFFFFFFF:
                    finalized[offset + 4:offset + 8] = struct.pack("<I", len(finalized) - offset - 8)
                break
            offset += 8 + size + (size % 2)
        return bytes(finalized)
    return stdout


async def prepare_asr_audio(data: bytes) -> bytes:
    return await convert_wav(data, 16000, 1)


async def normalize_tts_audio(data: bytes) -> AudioResult:
    converted = await convert_wav(data, 24000, 2, "loudnorm=I=-14:TP=-1.5:LRA=11,apad=pad_dur=0.03")
    if len(converted) > MAX_DEVICE_WAV_BYTES:
        raise AudioError("synthesized audio exceeds the 2 MiB device buffer")
    try:
        with wave.open(BytesIO(converted), "rb") as wav:
            duration_ms = round(wav.getnframes() / wav.getframerate() * 1000)
    except wave.Error as exc:
        raise AudioError("converted audio is not valid PCM WAV") from exc
    return AudioResult(wav_bytes=converted, duration_ms=duration_ms)
