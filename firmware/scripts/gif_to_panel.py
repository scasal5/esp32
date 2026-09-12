"""Convierte un GIF al formato de la placa: 240x284, loop, paleta adaptativa."""

from __future__ import annotations

import argparse
import sys

from PIL import Image, ImageSequence

PANEL_W = 240
PANEL_H = 284
MIN_DELAY_MS = 70


def cover_to_panel(im: Image.Image) -> Image.Image:
    im = im.convert("RGBA")
    scale = max(PANEL_W / im.width, PANEL_H / im.height)
    nw = max(PANEL_W, round(im.width * scale))
    nh = max(PANEL_H, round(im.height * scale))
    # NEAREST: downloadfile es mosaico de caracteres; LANCZOS lo empasta.
    im = im.resize((nw, nh), Image.Resampling.NEAREST)
    left = (nw - PANEL_W) // 2
    top = (nh - PANEL_H) // 2
    cropped = im.crop((left, top, left + PANEL_W, top + PANEL_H))
    bg = Image.new("RGB", (PANEL_W, PANEL_H), (10, 10, 10))
    bg.paste(cropped, mask=cropped.split()[3])
    return bg


def compose_frames(src: Image.Image) -> tuple[list[Image.Image], list[int]]:
    canvas = Image.new("RGBA", src.size, (0, 0, 0, 255))
    prev = canvas.copy()
    out: list[Image.Image] = []
    delays: list[int] = []

    for frame in ImageSequence.Iterator(src):
        delay = int(frame.info.get("duration", MIN_DELAY_MS) or MIN_DELAY_MS)
        delays.append(max(delay, MIN_DELAY_MS))
        disp = frame.disposal_method if hasattr(frame, "disposal_method") else 2
        rgba = frame.convert("RGBA")
        if disp == 3:
            prev = canvas.copy()
        canvas.paste(rgba, (0, 0), rgba)
        out.append(cover_to_panel(canvas))
        if disp == 2:
            canvas = Image.new("RGBA", src.size, (0, 0, 0, 255))
        elif disp == 3:
            canvas = prev

    return out, delays


def to_p_frames(frames: list[Image.Image]) -> list[Image.Image]:
    sample = Image.new("RGB", (PANEL_W, PANEL_H * min(4, len(frames))))
    for i, fr in enumerate(frames[:4]):
        sample.paste(fr, (0, i * PANEL_H))
    pal = sample.quantize(colors=128, method=Image.Quantize.MEDIANCUT)
    return [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames]


def convert(src: str, dst: str) -> None:
    with Image.open(src) as im:
        frames, delays = compose_frames(im)
    pframes = to_p_frames(frames)
    pframes[0].save(
        dst,
        save_all=True,
        append_images=pframes[1:],
        duration=delays,
        loop=0,
        optimize=True,
        disposal=2,
        interlace=False,
    )


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("src")
    p.add_argument("dst")
    args = p.parse_args()
    convert(args.src, args.dst)
    with Image.open(args.dst) as out:
        print(
            f"{args.dst}: {out.size[0]}x{out.size[1]} frames={out.n_frames} "
            f"loop={out.info.get('loop')} dur={out.info.get('duration')} "
            f"bytes={__import__('os').path.getsize(args.dst)}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
