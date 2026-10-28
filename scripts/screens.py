#!/usr/bin/env python3
"""Convert the actual LVGL framebuffer output to reviewable PNGs (requires Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
source = root / "build-host/screens"
output = root / "docs/screens"
output.mkdir(parents=True, exist_ok=True)
paths = sorted(source.glob("*.ppm"))
if not paths:
    raise SystemExit("Run make preview first")
for path in paths:
    Image.open(path).save(output / f"{path.stem}.png")
selected = ["01-ready", "02-objects", "03-dose", "04-extracting", "05-result", "12-register-name"]
sheet = Image.new("RGB", (1008, 1092), "#e8eee9")
draw = ImageDraw.Draw(sheet)
for index, name in enumerate(selected):
    x, y = (index % 2) * 504 + 12, (index // 2) * 364 + 32
    draw.text((x, y - 22), name[3:].replace("-", " ").upper(), fill="#152825")
    sheet.paste(Image.open(output / f"{name}.png"), (x, y))
sheet.save(output / "overview.png")
print(output / "overview.png")
