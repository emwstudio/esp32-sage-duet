from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PEOPLE = {
    "shaolan": {
        "repo": "ShaolanPet",
        "clips": ["30.wav", "31.wav", "32.wav", "26.wav"],
    },
    "sun": {
        "repo": "SunPet",
        "clips": ["03.m4a", "06.m4a", "08.m4a"],
    },
    "fengge": {
        "repo": "FengGe-Desktop-Pet",
        "clips": ["01.wav", "02.wav", "03.wav", "04.wav", "05.wav", "06.wav", "07.wav"],
    },
}


def run(args: list[str]) -> None:
    subprocess.run(args, check=True)


def prepare(person: str, config: dict[str, object]) -> None:
    repo = ROOT / "vendor" / str(config["repo"])
    destination = ROOT / "assets" / "personas" / person
    destination.mkdir(parents=True, exist_ok=True)
    face = destination / "face.jpg"
    run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(repo / "Assets" / "face.png"), "-an", "-map_metadata", "-1",
        "-vf", "scale=466:466:force_original_aspect_ratio=decrease:force_divisible_by=2,"
        "pad=466:466:(ow-iw)/2:(oh-ih)/2:color=black,setsar=1",
        "-frames:v", "1", "-c:v", "mjpeg", "-q:v", "2", "-pix_fmt", "yuvj420p",
        "-update", "1", str(face),
    ])

    clips = [repo / "Assets" / "Clips" / name for name in config["clips"]]
    command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"]
    for clip in clips:
        command.extend(["-i", str(clip)])
    filters = []
    labels = []
    for index in range(len(clips)):
        label = f"a{index}"
        filters.append(
            f"[{index}:a]silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.05,"
            f"aresample=24000,aformat=sample_fmts=s16:channel_layouts=mono[{label}]"
        )
        labels.append(f"[{label}]")
    filters.append(f"{''.join(labels)}concat=n={len(clips)}:v=0:a=1,atrim=0:10,loudnorm=I=-20:TP=-2:LRA=11[out]")
    command.extend([
        "-filter_complex", ";".join(filters), "-map", "[out]",
        "-c:a", "pcm_s16le", "-ar", "24000", "-ac", "1",
        str(destination / "reference.wav"),
    ])
    run(command)


if __name__ == "__main__":
    for name, settings in PEOPLE.items():
        prepare(name, settings)
        print(f"prepared {name}")
