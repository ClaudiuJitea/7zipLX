#!/usr/bin/env python3
"""Compose the raw offscreen captures into README-ready images."""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"


def font(path: str, size: int) -> ImageFont.FreeTypeFont:
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.load_default()


def vertical_gradient(size: tuple[int, int], top: str, bottom: str) -> Image.Image:
    from PIL import ImageColor

    width, height = size
    top_rgb = ImageColor.getrgb(top)
    bottom_rgb = ImageColor.getrgb(bottom)
    image = Image.new("RGB", size)
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / max(1, height - 1)
        color = tuple(round(a + (b - a) * t) for a, b in zip(top_rgb, bottom_rgb))
        draw.line([(0, y), (width, y)], fill=color)
    return image


def rounded_mask(size: tuple[int, int], radius: int) -> Image.Image:
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius=radius, fill=255)
    return mask


def paste_window(
    canvas: Image.Image,
    shot: Image.Image,
    position: tuple[int, int],
    radius: int = 12,
    shadow: int = 28,
) -> None:
    shot = shot.convert("RGBA")
    mask = rounded_mask(shot.size, radius)
    shadow_layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    shadow_mask = Image.new("L", canvas.size, 0)
    x, y = position
    shadow_mask.paste(mask, (x + 6, y + 10))
    shadow_layer.putalpha(shadow_mask.filter(ImageFilter.GaussianBlur(shadow)))
    canvas.alpha_composite(shadow_layer)
    canvas.paste(shot, position, mask)


def caption(draw: ImageDraw.ImageDraw, position: tuple[int, int], title: str, subtitle: str) -> None:
    x, y = position
    draw.text((x, y), title, font=font(FONT_BOLD, 24), fill="#f4f6fa")
    draw.text((x, y + 32), subtitle, font=font(FONT, 15), fill="#9aa6b5")


def compose_hero(raw: Path, out: Path) -> None:
    shot = Image.open(raw / "hero.png")
    canvas = vertical_gradient((1240, 836), "#0b0d11", "#1b2230").convert("RGBA")
    paste_window(canvas, shot, (30, 38), radius=14, shadow=30)
    canvas.convert("RGB").save(out / "hero.png", optimize=True)


def compose_two_panes(raw: Path, out: Path) -> None:
    shot = Image.open(raw / "two-panes-light.png")
    canvas = vertical_gradient((1240, 836), "#eef1f5", "#dce4ee").convert("RGBA")
    paste_window(canvas, shot, (30, 38), radius=14, shadow=26)
    canvas.convert("RGB").save(out / "two-panes.png", optimize=True)


def compose_workflows(raw: Path, out: Path) -> None:
    compress = Image.open(raw / "compress-dialog.png")
    extract = Image.open(raw / "extract-dialog.png")
    hashes = Image.open(raw / "hash-result.png")
    hashes = hashes.resize((560, round(hashes.height * 560 / hashes.width)), Image.LANCZOS)
    canvas = vertical_gradient((1240, 900), "#0d1015", "#202837").convert("RGBA")
    draw = ImageDraw.Draw(canvas)
    caption(draw, (64, 30), "Create archives", "format, level, method, volumes and passwords")
    caption(draw, (664, 30), "Extract with options", "paths, overwrite policy and passwords")
    paste_window(canvas, compress, (64, 100), radius=12, shadow=24)
    paste_window(canvas, extract, (664, 100), radius=12, shadow=24)
    caption(draw, (664, 566), "Verify with streamed hashes", "no temporary extraction step")
    paste_window(canvas, hashes, (664, 596), radius=12, shadow=24)
    canvas.convert("RGB").save(out / "workflows.png", optimize=True)


def compose_writeback(raw: Path, out: Path) -> None:
    prompt = Image.open(raw / "external-edit.png")
    progress = Image.open(raw / "progress-dialog.png")
    progress = progress.resize((520, round(progress.height * 520 / progress.width)), Image.LANCZOS)
    canvas = vertical_gradient((1240, 500), "#0d1015", "#202837").convert("RGBA")
    draw = ImageDraw.Draw(canvas)
    caption(draw, (64, 30), "External edits come back", "save in your editor, then update the archive")
    caption(draw, (670, 30), "Nested updates are atomic", "7z and ZIP update in place; GZip/XZ rebuild safely")
    paste_window(canvas, prompt, (64, 100), radius=12, shadow=24)
    paste_window(canvas, progress, (670, 110), radius=12, shadow=24)
    draw.text((64, 370), "Watched with QFileSystemWatcher + polling", font=font(FONT, 16), fill="#9aa6b5")
    draw.text((64, 398), "Nested parents are rebuilt bottom-up", font=font(FONT, 16), fill="#9aa6b5")
    draw.text((64, 426), "Keep Changes or save later from the File menu", font=font(FONT, 16), fill="#9aa6b5")
    canvas.convert("RGB").save(out / "writeback.png", optimize=True)


def compose_options(raw: Path, out: Path) -> None:
    shot = Image.open(raw / "options-dialog.png")
    canvas = vertical_gradient((720, 670), "#0d1015", "#202837").convert("RGBA")
    paste_window(canvas, shot, (34, 34), radius=12, shadow=24)
    canvas.convert("RGB").save(out / "options.png", optimize=True)


def main() -> int:
    raw = Path(sys.argv[1] if len(sys.argv) > 1 else "docs/screenshots-new")
    out = Path(sys.argv[2] if len(sys.argv) > 2 else "docs/screenshots")
    out.mkdir(parents=True, exist_ok=True)
    compose_hero(raw, out)
    compose_two_panes(raw, out)
    compose_workflows(raw, out)
    compose_writeback(raw, out)
    compose_options(raw, out)
    print(f"Composed README images in {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
