#!/usr/bin/env python3
"""
mkartwork.py - build the pico-bootLoader menu tiles of pico-galagino.

    tools/capture_footage.sh ~/roms/arcade /tmp/gal_footage
    tools/mkartwork.py --footage /tmp/gal_footage --bootloader ../pico-bootLoader/emu/assets

writes DIR/themes/0/galagino.png (640x480) and DIR/themes/1/galagino.png
(320x240). Run the bootloader's tools/png2raw.py on the two PNGs afterwards to
build their .444/.555 caches.

The ROM sets are not part of this project, so neither are pictures of the
games: they are taken from your own ROM sets by the host harness
(tools/capture_footage.sh), then laid out here. The footage folder holds four
320x240 PPM frames with Tate mode Off (the game area is x 48-271):

    badge.ppm    theme 0: the box art in the badge (the game menu)
    left.ppm     theme 1: left panel (Pac-Man)
    middle.ppm   theme 1: middle panel (Galaga)
    right.ppm    theme 1: right panel (Donkey Kong)

The tiles follow the two themes of pico-bootLoader, with the geometry of its
tools/make_category_art.py, as pico-mooncresta's tools/mkartwork.py does:

    theme 0   640x480: flat saturated ground, a huge ghosted silhouette (an
              arcade joystick), a boxed badge on the left, the wordmark and a
              rule
    theme 1   320x240: three slanted panels of game footage, white wordmark
              across the middle

Requires numpy and ffmpeg (with librsvg). Derived from pico-mooncresta's
tools/mkartwork.py.
"""

import argparse
import base64
import os
import subprocess
import sys
import tempfile

import numpy as np

WORDMARK = "GALAGINO"

# Screenshots from the host harness: 320x240, Tate mode Off
GAME_X, GAME_W, GAME_H = 48, 224, 240


def write_png(path, img):
    h, w, _ = img.shape
    with tempfile.NamedTemporaryFile(suffix=".ppm", delete=False) as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(img.tobytes())
        ppm = f.name
    try:
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", ppm, path], check=True)
    finally:
        os.unlink(ppm)


def png_data_uri(img):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "a.png")
        write_png(path, img)
        with open(path, "rb") as f:
            return "data:image/png;base64," + base64.b64encode(f.read()).decode("ascii")


def rasterise(svg, out_png):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "tile.svg")
        with open(path, "w") as f:
            f.write(svg)
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", path, "-frames:v", "1", "-update", "1",
                        "-pix_fmt", "rgb24", "-compression_level", "9", out_png], check=True)


def decode_png(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                         check=True, capture_output=True).stdout
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                            "stream=width,height", "-of", "csv=p=0", path],
                           check=True, capture_output=True, text=True).stdout.strip().split(",")
    w, h = int(probe[0]), int(probe[1])
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3)


def load_footage(footage, name):
    path = os.path.join(footage, name + ".ppm")
    if not os.path.exists(path):
        sys.exit("%s not found; run tools/capture_footage.sh first" % path)
    with open(path, "rb") as f:
        data = f.read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    if (w, h) != (320, 240):
        sys.exit("%s: expected a 320x240 frame from hosttest/gal_host" % path)
    return np.frombuffer(parts[3][:w * h * 3], np.uint8).reshape(h, w, 3)


# ---------------------------------------------------------------------------
# Theme 0, geometry from pico-bootLoader's tools/make_category_art.py
# ---------------------------------------------------------------------------
T0_W, T0_H = 640, 480
T0_GROUND, T0_TINT = "#2f9a54", "#46b06b"
BADGE_X, BADGE_Y, BADGE_W, BADGE_H = 2, 186, 90, 108
BADGE_FRAME = 8
BADGE_GROUND = "#000000"  # black, as the game screens' own background, so the box art has no edge
RULE_Y, RULE_TH = 229, 2
DASH_X0, DASH_X1 = 94, 108
TEXT_X, TEXT_BASELINE, TEXT_SIZE = 114, 246, 45

# The ghosted silhouette: an arcade joystick in a 100x100 box. `knock` is
# punched back out of it in the ground colour.
STICK_HULL = (
    '<circle cx="50" cy="22" r="16"/>'                 # ball
    '<rect x="46" y="30" width="8" height="42"/>'      # shaft
    '<ellipse cx="50" cy="73" rx="15" ry="5"/>'        # collar
    '<path d="M12 77 L88 77 L96 97 L4 97 Z"/>'         # base
)
STICK_KNOCK = (
    '<circle cx="44" cy="16" r="4.5"/>'                # highlight on the ball
    '<circle cx="74" cy="85" r="3.5"/>'                # buttons
    '<circle cx="85" cy="85" r="3.5"/>'
    '<rect x="8" y="91" width="84" height="2.5"/>'     # edge of the base
)
WATERMARK = (560, 330, 300, -14)  # centre x, centre y, box size, rotation


def theme0_svg(rule_x0, badge_frame=None):
    cx, cy, size, rot = WATERMARK
    s = size / 100.0
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{T0_W}" height="{T0_H}">',
        f'<rect width="{T0_W}" height="{T0_H}" fill="{T0_GROUND}"/>',
        f'<g transform="rotate({rot} {cx} {cy}) translate({cx - size / 2},{cy - size / 2}) scale({s:.5f})">'
        f'<g fill="{T0_TINT}">{STICK_HULL}</g><g fill="{T0_GROUND}">{STICK_KNOCK}</g></g>',
    ]
    if rule_x0 is None:
        # measuring pass: the wordmark alone
        parts.append(
            f'<rect width="{T0_W}" height="{T0_H}" fill="#000"/>'
            f'<text x="{TEXT_X}" y="{TEXT_BASELINE}" font-family="Liberation Sans" font-weight="bold" '
            f'font-size="{TEXT_SIZE}" letter-spacing="2" fill="#ffffff">{WORDMARK}</text></svg>')
        return "".join(parts)

    ix, iy = BADGE_X + BADGE_FRAME, BADGE_Y + BADGE_FRAME
    iw, ih = BADGE_W - 2 * BADGE_FRAME, BADGE_H - 2 * BADGE_FRAME
    art_h = iw
    # the whole game area as square box art, centred on the plate (no footer bar)
    game = badge_frame[:, GAME_X:GAME_X + GAME_W]
    art = (f'<svg x="{ix}" y="{iy + (ih - art_h) // 2}" width="{iw}" height="{art_h}" '
           f'viewBox="0 0 {GAME_W} {GAME_H}" preserveAspectRatio="xMidYMid slice">'
           f'<image xlink:href="{png_data_uri(game)}" width="{GAME_W}" height="{GAME_H}"/></svg>')
    parts.append(
        f'<rect x="{BADGE_X}" y="{BADGE_Y}" width="{BADGE_W}" height="{BADGE_H}" fill="#ffffff"/>'
        f'<rect x="{ix}" y="{iy}" width="{iw}" height="{ih}" fill="{BADGE_GROUND}"/>'
        + art)
    parts.append(
        f'<rect x="{DASH_X0}" y="{RULE_Y}" width="{DASH_X1 - DASH_X0}" height="{RULE_TH}" fill="#ffffff"/>'
        f'<text x="{TEXT_X}" y="{TEXT_BASELINE}" font-family="Liberation Sans" font-weight="bold" '
        f'font-size="{TEXT_SIZE}" letter-spacing="2" fill="#ffffff">{WORDMARK}</text>'
        f'<rect x="{rule_x0}" y="{RULE_Y}" width="{637 - rule_x0}" height="{RULE_TH}" fill="#ffffff"/>')
    parts.append("</svg>")
    return "".join(parts)


def theme0_tile(out_png, footage):
    # measure the wordmark's ink, then start the rule 12px after it
    with tempfile.TemporaryDirectory() as d:
        probe = os.path.join(d, "m.png")
        rasterise(theme0_svg(None), probe)
        ink = (decode_png(probe) > 128).any(axis=2)
        cols = np.where(ink.any(axis=0))[0]
        rule_x0 = int(cols.max()) + 12
    rasterise(theme0_svg(rule_x0, load_footage(footage, "badge")), out_png)


# ---------------------------------------------------------------------------
# Theme 1: panels 8.5 degrees off vertical, 2px black seams
# ---------------------------------------------------------------------------
T1_W, T1_H = 320, 240
PANEL_CLIP = {
    "L": "0,0 120,0 84,240 0,240",
    "M": "120,0 236,0 200,240 84,240",
    "R": "236,0 320,0 320,240 200,240",
}
T1_TEXT_Y, T1_TEXT_SIZE, T1_STROKE = 114, 33, "#101820"

# Where each frame goes in its panel box (L 0-120, M 84-236, R 200-320) at
# 1:1: the x offset of the 320x240 frame that puts the middle of its game area
# in the middle of the panel. Each panel shows a different game.
FOOTAGE_X = {"L": -100, "M": 0, "R": 100}


def theme1_tile(out_png, footage):
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{T1_W}" height="{T1_H}"><defs>',
    ]
    for slot, pts in PANEL_CLIP.items():
        parts.append(f'<clipPath id="p{slot}"><polygon points="{pts}"/></clipPath>')
    parts.append(f'</defs><rect width="{T1_W}" height="{T1_H}" fill="#000"/>')
    for slot, name in zip("LMR", ("left", "middle", "right")):
        img = load_footage(footage, name)
        parts.append(f'<g clip-path="url(#p{slot})"><image xlink:href="{png_data_uri(img)}" '
                     f'x="{FOOTAGE_X[slot]}" y="0" width="{T1_W}" height="{T1_H}" image-rendering="pixelated"/></g>')
    parts.append('<g stroke="#000" stroke-width="2" fill="none"><path d="M120 0L84 240M236 0L200 240"/></g>')
    parts.append(
        f'<text x="{T1_W // 2}" y="{T1_TEXT_Y}" text-anchor="middle" font-family="Liberation Sans Narrow" '
        f'font-weight="bold" font-style="italic" font-size="{T1_TEXT_SIZE}" letter-spacing="1" '
        f'fill="#ffffff" stroke="{T1_STROKE}" stroke-width="3" paint-order="stroke fill">{WORDMARK}</text>')
    parts.append("</svg>")
    rasterise("".join(parts), out_png)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--footage", metavar="DIR", required=True, help="frames from tools/capture_footage.sh")
    ap.add_argument("--bootloader", metavar="DIR", required=True, help="pico-bootLoader emu/assets directory")
    args = ap.parse_args()
    for theme, make in (("0", theme0_tile), ("1", theme1_tile)):
        path = os.path.join(args.bootloader, "themes", theme, "galagino.png")
        make(path, args.footage)
        print("wrote", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
