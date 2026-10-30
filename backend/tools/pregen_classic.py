import asyncio

from app.classic import ClassicClient
from app.config import Settings
from app.llm import SUN_VARIANTS
from app.schemas import Speaker
from app.tts import VoxCPMClient


async def main() -> None:
    settings = Settings.from_env()
    classic = ClassicClient()
    tts = VoxCPMClient(settings.modelbest_api_key, settings.modelbest_base_url, settings.modelbest_model)
    entries = [("shaolan", quote["text"]) for quote in classic.quotes]
    entries += [("sun", text) for text in SUN_VARIANTS]
    entries = list(dict.fromkeys(entries))
    semaphore = asyncio.Semaphore(4)

    async def generate(index: int, speaker: Speaker, text: str) -> None:
        path = classic.audio_path(speaker, text)
        if path.exists():
            print(f"[{index}/{len(entries)}] 已存在 {speaker}", flush=True)
            return
        async with semaphore:
            print(f"[{index}/{len(entries)}] 生成 {speaker}: {text}", flush=True)
            audio = await tts.synthesize(speaker, text)
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary = path.with_suffix(".tmp")
            temporary.write_bytes(audio.wav_bytes)
            temporary.replace(path)

    await asyncio.gather(*(
        generate(index, speaker, text)
        for index, (speaker, text) in enumerate(entries, 1)
    ))
    print("经典语录音频预生成完成", flush=True)


if __name__ == "__main__":
    asyncio.run(main())
