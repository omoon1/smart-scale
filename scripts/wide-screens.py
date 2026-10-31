#!/usr/bin/env python3
"""Export reviewed 640x172 LVGL frames; run make preview-wide first (Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
source = root / "build-host/screens"
output = root / "docs/screens/wide"
selected = ["wide-21-ready-live", "wide-02-dose", "wide-11-brewing",
            "wide-07-icons", "wide-17-calibrated", "wide-23-sensor-error"]
output.mkdir(parents=True, exist_ok=True)
sheet = Image.new("RGB", (1304, 624), "#e8eee9")
draw = ImageDraw.Draw(sheet)
for i, name in enumerate(selected):
    path = source / f"{name}.ppm"
    if not path.exists():
        raise SystemExit("Run make preview-wide first")
    frame = Image.open(path)
    assert frame.size == (640, 172), f"Unexpected dimensions: {path}"
    frame.save(output / f"{name}.png")
    x, y = 8 + (i % 2) * 652, 30 + (i // 2) * 208
    draw.text((x, y - 20), name[8:].replace("-", " ").upper(), fill="#152825")
    sheet.paste(frame, (x, y))
sheet.save(output / "overview.png")
print(output / "overview.png")
