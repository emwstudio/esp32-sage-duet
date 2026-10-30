from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "firmware" / "main" / "faces.c"

with OUTPUT.open("w") as output:
    output.write('#include "lvgl.h"\n\n')
    for person in ("shaolan", "sun", "fengge"):
        raw = subprocess.check_output([
            "ffmpeg", "-hide_banner", "-loglevel", "error",
            "-i", str(ROOT / "assets" / "personas" / person / "face.jpg"),
            "-vf", "pad=498:498:16:16:black",
            "-f", "rawvideo", "-pix_fmt", "rgb565le", "-",
        ])
        output.write(f"static const uint8_t {person}_pixels[] = {{\n")
        for offset in range(0, len(raw), 16):
            output.write("    " + ",".join(f"0x{value:02x}" for value in raw[offset:offset + 16]) + ",\n")
        output.write("};\n")
        output.write(f"const lv_image_dsc_t face_{person} = {{\n")
        output.write("    .header = {\n")
        output.write("        .magic = LV_IMAGE_HEADER_MAGIC,\n")
        output.write("        .cf = LV_COLOR_FORMAT_RGB565,\n")
        output.write("        .flags = 0,\n")
        output.write("        .w = 498,\n        .h = 498,\n        .stride = 498 * 2,\n")
        output.write("        .reserved_2 = 0,\n    },\n")
        output.write(f"    .data_size = sizeof({person}_pixels),\n")
        output.write(f"    .data = {person}_pixels,\n")
        output.write("};\n\n")
