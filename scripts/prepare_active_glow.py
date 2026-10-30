from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
SIZE = 466
GOLD = (255, 211, 106)


def blurred_outline(shape: str, width: int, opacity: int, radius: float) -> Image.Image:
    mask = Image.new("L", (SIZE, SIZE), 0)
    draw = ImageDraw.Draw(mask)
    bounds = (10, 10, SIZE - 11, SIZE - 11)
    if shape == "circle":
        draw.ellipse(bounds, outline=opacity, width=width)
    else:
        draw.rounded_rectangle(bounds, radius=28, outline=opacity, width=width)
    return mask.filter(ImageFilter.GaussianBlur(radius))


def main() -> None:
    assets = ROOT / "assets" / "personas"
    output = ROOT / "firmware" / "main" / "active_glow.c"
    with output.open("w") as file:
        file.write('#include "lvgl.h"\n\n')
        for shape in ("circle", "square"):
            alpha = ImageChops.add(
                blurred_outline(shape, width=7, opacity=150, radius=20),
                blurred_outline(shape, width=14, opacity=65, radius=44),
            )
            glow = Image.new("RGBA", (SIZE, SIZE), GOLD + (0,))
            glow.putalpha(alpha)
            glow.save(assets / f"active_glow_{shape}.png")
            person = "shaolan" if shape == "circle" else "sun"
            face = Image.open(assets / person / "face.jpg").convert("RGBA")
            Image.alpha_composite(face, glow).convert("RGB").save(assets / f"active_glow_{shape}_preview.png")

            raw = bytearray()
            for opacity in range(256):
                raw.extend((GOLD[2], GOLD[1], GOLD[0], opacity))
            raw.extend(alpha.tobytes())
            file.write(f"static const uint8_t {shape}_pixels[] = {{\n")
            for offset in range(0, len(raw), 16):
                file.write("    " + ",".join(f"0x{value:02x}" for value in raw[offset:offset + 16]) + ",\n")
            file.write("};\n")
            file.write(f"const lv_image_dsc_t active_glow_{shape} = {{\n")
            file.write("    .header = {\n")
            file.write("        .magic = LV_IMAGE_HEADER_MAGIC,\n")
            file.write("        .cf = LV_COLOR_FORMAT_I8,\n")
            file.write("        .flags = 0,\n")
            file.write(f"        .w = {SIZE},\n        .h = {SIZE},\n        .stride = {SIZE},\n")
            file.write("        .reserved_2 = 0,\n    },\n")
            file.write(f"    .data_size = sizeof({shape}_pixels),\n")
            file.write(f"    .data = {shape}_pixels,\n")
            file.write("};\n\n")
            print(f"{shape} 光晕：{len(raw)} 字节；最大不透明度：{max(alpha.getdata())}/255")


if __name__ == "__main__":
    main()
