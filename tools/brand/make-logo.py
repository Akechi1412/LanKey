#!/usr/bin/env python3
"""Generate the LanKey logo: assets/brand/lankey-logo.svg and app/lankey.ico.

The mark is a mechanical keycap seen from the front and slightly above, turned a little
to the left so its front and right walls show: a lit top face (vertical gradient) carrying a white "L" drawn as one rounded stroke whose foot ends in a
detached text caret, on a dark cobalt body, over a soft ground shadow.

The top face and its lettering are designed flat, in "top space" (a 200-unit square), and
projected with one affine matrix; the walls are the sweep of the visible part of the top
outline down the key height, shaded by the outward normal. The same numbers produce the
SVG and every icon size (each rendered with 8x supersampling rather than scaled from one
bitmap, so the 16 px tray-sized icon keeps clean edges).

Needs Pillow (`pip install Pillow`); nothing else, and nothing in the build depends on it -
the generated files are committed, this only regenerates them.

    python tools/brand/make-logo.py                 # writes both files
    python tools/brand/make-logo.py --preview DIR   # also writes a preview sheet to DIR
"""

import math
import struct
import sys
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
SVG_PATH = ROOT / "assets" / "brand" / "lankey-logo.svg"
ICO_PATH = ROOT / "app" / "lankey.ico"
ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)

# Brand palette (ui/win32/Theme.h: kBrandVietnamese sits in the middle of the top face).
FACE_TOP = (0x4E, 0x8C, 0xF2)
FACE_BOTTOM = (0x25, 0x60, 0xD2)
WALL_FRONT = (0x1E, 0x4F, 0xB3)  # wall facing the viewer (+y normal)
WALL_SIDE = (0x12, 0x33, 0x7E)   # wall on the right (+x normal)
WALL_DARKEN = 0.72               # wall colour at its foot, as a factor of the top edge
INK = (0xFF, 0xFF, 0xFF)

BOX = 256  # output box

# Top space: the key's top face, flat.
S = 200
R = 46  # corner radius of the top face
STROKE = 32
L_TOP = (58, 55)
L_CORNER = (58, 145)
L_END = (120, 145)
CARET = (138, 129, 158, 161)
INK_SHADOW = (0, 4, 5)  # dx, dy, blur

# Projection: top space -> output. x runs right and slightly down, y runs down and to the
# left, the key height goes straight down. Scaled and translated below so that the whole
# key (walls and ground shadow included) fits the box with a small margin.
KEY_HEIGHT = 48
TILT = ((1.0, 0.0), (0.0, 0.86))  # rows: (u from x, u from y), (v from x, v from y): no yaw, pitched
MARGIN = 4
GROUND_SHADOW = (0, 10, 12, 0.24)  # dx, dy, blur, opacity


def _fit():
    """Scale + translation that fit the projected key into the box."""
    (a, b), (c, d) = TILT
    corners = [(0, 0), (S, 0), (0, S), (S, S)]
    us = [a * x + b * y for x, y in corners]
    vs = [c * x + d * y for x, y in corners]
    width = max(us) - min(us)
    height = max(vs) - min(vs) + KEY_HEIGHT + GROUND_SHADOW[1] + GROUND_SHADOW[2] / 2
    k = min((BOX - 2 * MARGIN) / width, (BOX - 2 * MARGIN) / height)
    tx = (BOX - width * k) / 2 - min(us) * k
    ty = (BOX - height * k) / 2 - min(vs) * k
    return k, tx, ty


FIT_K, FIT_TX, FIT_TY = _fit()
A, B = TILT[0][0] * FIT_K, TILT[0][1] * FIT_K
C, D = TILT[1][0] * FIT_K, TILT[1][1] * FIT_K
H = KEY_HEIGHT * FIT_K


def project(x, y):
    return A * x + B * y + FIT_TX, C * x + D * y + FIT_TY


def outline(n_arc=24):
    """Top-face outline in top space as (point, outward normal) pairs, clockwise from the
    top-left corner: straight edges are one segment, corners n_arc segments."""
    pts = []
    def arc(cx, cy, start_deg):
        for i in range(n_arc + 1):
            t = math.radians(start_deg + 90 * i / n_arc)
            pts.append((cx + R * math.cos(t), cy + R * math.sin(t)))
    arc(R, R, 180)          # top-left
    arc(S - R, R, 270)      # top-right
    arc(S - R, S - R, 0)    # bottom-right
    arc(R, S - R, 90)       # bottom-left
    segs = []
    for i in range(len(pts)):
        p0, p1 = pts[i], pts[(i + 1) % len(pts)]
        dx, dy = p1[0] - p0[0], p1[1] - p0[1]
        length = math.hypot(dx, dy)
        if length < 1e-6:
            continue
        segs.append((p0, p1, (dy / length, -dx / length)))  # y down, clockwise: outward normal
    return segs


def wall_segments():
    """Visible wall quads: (p0, p1, colour) with points in top space."""
    out = []
    for p0, p1, (nx, ny) in outline():
        # A wall is visible when its outward normal, projected, points down the screen
        # (the direction the key height extrudes in).
        if C * nx + D * ny <= 0:
            continue
        t = max(0.0, min(1.0, nx / (abs(nx) + abs(ny))))  # 0 = front wall, 1 = right wall
        colour = tuple(round(WALL_FRONT[i] + (WALL_SIDE[i] - WALL_FRONT[i]) * t) for i in range(3))
        out.append((p0, p1, colour))
    return out


def hex_(c):
    return "#%02X%02X%02X" % c


# ---- SVG ------------------------------------------------------------------------------------

def svg() -> str:
    cx0, cy0, cx1, cy1 = CARET
    walls = []
    for (x0, y0), (x1, y1), colour in wall_segments():
        u0, v0 = project(x0, y0)
        u1, v1 = project(x1, y1)
        foot = tuple(round(ch * WALL_DARKEN) for ch in colour)
        gid = f"w{len(walls)}"
        walls.append(
            f'    <linearGradient id="{gid}" gradientUnits="userSpaceOnUse" x1="0" y1="{min(v0, v1):.2f}" '
            f'x2="0" y2="{max(v0, v1) + H:.2f}"><stop offset="0" stop-color="{hex_(colour)}"/>'
            f'<stop offset="1" stop-color="{hex_(foot)}"/></linearGradient>\n'
            f'    <polygon points="{u0:.2f},{v0:.2f} {u1:.2f},{v1:.2f} {u1:.2f},{v1 + H:.2f} '
            f'{u0:.2f},{v0 + H:.2f}" fill="url(#{gid})" stroke="{hex_(colour)}" stroke-width="0.4"/>'
        )
    gdx, gdy, gblur, gop = GROUND_SHADOW
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {BOX} {BOX}" width="{BOX}" height="{BOX}">
  <!-- LanKey logo. Regenerate with tools/brand/make-logo.py; the .ico is built from the same numbers. -->
  <defs>
    <linearGradient id="face" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{hex_(FACE_TOP)}"/>
      <stop offset="1" stop-color="{hex_(FACE_BOTTOM)}"/>
    </linearGradient>
    <filter id="inkShadow" x="-20%" y="-20%" width="140%" height="140%">
      <feGaussianBlur stdDeviation="{INK_SHADOW[2] / 2}"/>
    </filter>
    <filter id="groundShadow" x="-30%" y="-30%" width="160%" height="160%">
      <feGaussianBlur stdDeviation="{gblur * FIT_K / 2:.2f}"/>
    </filter>
    <clipPath id="faceClip">
      <rect x="0" y="0" width="{S}" height="{S}" rx="{R}"/>
    </clipPath>
  </defs>
  <!-- ground shadow: the key's footprint, softened -->
  <g transform="matrix({A:.5f} {C:.5f} {B:.5f} {D:.5f} {FIT_TX + gdx * FIT_K:.3f} {FIT_TY + H + gdy * FIT_K:.3f})">
    <rect x="0" y="0" width="{S}" height="{S}" rx="{R}" fill="#000" fill-opacity="{gop}" filter="url(#groundShadow)"/>
  </g>
  <!-- walls: the visible outline swept down the key height, shaded by normal -->
  <g>
{chr(10).join(walls)}
  </g>
  <!-- top face, designed flat and projected -->
  <g transform="matrix({A:.5f} {C:.5f} {B:.5f} {D:.5f} {FIT_TX:.3f} {FIT_TY:.3f})">
    <rect x="0" y="0" width="{S}" height="{S}" rx="{R}" fill="url(#face)"/>
    <g clip-path="url(#faceClip)" transform="translate({INK_SHADOW[0]} {INK_SHADOW[1]})" filter="url(#inkShadow)" opacity="0.25">
      <path d="M{L_TOP[0]} {L_TOP[1]} V{L_CORNER[1]} H{L_END[0]}" fill="none" stroke="#000" stroke-width="{STROKE}" stroke-linecap="round" stroke-linejoin="round"/>
      <rect x="{cx0}" y="{cy0}" width="{cx1 - cx0}" height="{cy1 - cy0}" rx="5" fill="#000"/>
    </g>
    <path d="M{L_TOP[0]} {L_TOP[1]} V{L_CORNER[1]} H{L_END[0]}" fill="none" stroke="{hex_(INK)}" stroke-width="{STROKE}" stroke-linecap="round" stroke-linejoin="round"/>
    <rect x="{cx0}" y="{cy0}" width="{cx1 - cx0}" height="{cy1 - cy0}" rx="5" fill="{hex_(INK)}"/>
  </g>
</svg>
"""


# ---- raster ---------------------------------------------------------------------------------

def gradient(w, h, top, bottom):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    return img


def stroke_mask(size, k, dx=0.0, dy=0.0):
    """The L (rounded stroke) plus the caret in top space, scaled by k, as an alpha mask."""
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    w = STROKE * k
    pts = [((x + dx) * k, (y + dy) * k) for x, y in (L_TOP, L_CORNER, L_END)]
    d.line(pts, fill=255, width=int(round(w)), joint="curve")
    for x, y in pts:
        d.ellipse((x - w / 2, y - w / 2, x + w / 2, y + w / 2), fill=255)
    cx0, cy0, cx1, cy1 = CARET
    d.rounded_rectangle(((cx0 + dx) * k, (cy0 + dy) * k, (cx1 + dx) * k, (cy1 + dy) * k),
                        radius=5 * k, fill=255)
    return m


def render_top(k: float) -> Image.Image:
    """The flat top face at k pixels per top-space unit."""
    size = int(math.ceil(S * k))
    face_mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(face_mask).rounded_rectangle((0, 0, S * k, S * k), radius=R * k, fill=255)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    img.paste(gradient(size, size, FACE_TOP, FACE_BOTTOM), (0, 0), face_mask)

    dx, dy, blur = INK_SHADOW
    shadow = stroke_mask(size, k, dx, dy).filter(ImageFilter.GaussianBlur(blur * k / 2))
    shadow = Image.eval(shadow, lambda v: int(v * 0.25))
    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    layer.putalpha(Image.composite(shadow, Image.new("L", (size, size), 0), face_mask))
    img.alpha_composite(layer)

    ink = Image.new("RGBA", (size, size), INK + (255,))
    ink.putalpha(stroke_mask(size, k))
    img.alpha_composite(ink)
    return img


def render(size: int, oversample: int = 8) -> Image.Image:
    big = size * oversample
    k = big / BOX  # output units -> pixels
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))

    def uv(x, y, dz=0.0):
        u, v = project(x, y)
        return u * k, (v + dz) * k

    # Ground shadow: the footprint (top outline at the key's base), blurred.
    gdx, gdy, gblur, gop = GROUND_SHADOW
    foot = Image.new("L", (big, big), 0)
    pts = [uv(x, y, H + gdy * FIT_K) for (x, y), _, _ in outline()]
    pts = [(u + gdx * FIT_K * k, v) for u, v in pts]
    ImageDraw.Draw(foot).polygon(pts, fill=255)
    foot = foot.filter(ImageFilter.GaussianBlur(gblur * FIT_K * k / 2))
    foot = Image.eval(foot, lambda v: int(v * gop))
    layer = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    layer.putalpha(foot)
    img.alpha_composite(layer)

    # Walls.
    d = ImageDraw.Draw(img)
    strips = 12  # wall darkens towards its foot
    for (x0, y0), (x1, y1), colour in wall_segments():
        u0, v0 = uv(x0, y0)
        u1, v1 = uv(x1, y1)
        for i in range(strips):
            t0, t1 = i / strips, (i + 1) / strips
            f = 1 - (1 - WALL_DARKEN) * (t0 + t1) / 2
            c = tuple(round(ch * f) for ch in colour) + (255,)
            d.polygon([(u0, v0 + H * k * t0), (u1, v1 + H * k * t0), (u1, v1 + H * k * t1),
                       (u0, v0 + H * k * t1)], fill=c, outline=c)

    # Top face: rendered flat at a comparable resolution, then projected. transform()
    # takes the inverse mapping (output pixel -> source pixel).
    top_k = k * max(abs(A), abs(D)) * 1.1
    top = render_top(top_k)
    a, b, c, dd = A * k / top_k, B * k / top_k, C * k / top_k, D * k / top_k
    det = a * dd - b * c
    ia, ib, ic, id_ = dd / det, -b / det, -c / det, a / det
    tx, ty = FIT_TX * k, FIT_TY * k
    projected = top.transform(
        (big, big), Image.AFFINE,
        (ia, ib, -(ia * tx + ib * ty), ic, id_, -(ic * tx + id_ * ty)),
        resample=Image.BICUBIC)
    img.alpha_composite(projected)

    return img.resize((size, size), Image.LANCZOS)


def write_ico(path: Path, images: list) -> None:
    # PNG-compressed entries for every size: smaller than BMP and what Windows expects
    # for 256 px anyway.
    entries = []
    blobs = []
    offset = 6 + 16 * len(images)
    for img in images:
        buf = BytesIO()
        img.save(buf, format="PNG")
        blob = buf.getvalue()
        w = img.width if img.width < 256 else 0
        entries.append(struct.pack("<BBBBHHII", w, w, 0, 0, 1, 32, len(blob), offset))
        blobs.append(blob)
        offset += len(blob)
    with path.open("wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(images)))
        f.write(b"".join(entries))
        f.write(b"".join(blobs))


def main() -> None:
    SVG_PATH.parent.mkdir(parents=True, exist_ok=True)
    SVG_PATH.write_text(svg(), encoding="utf-8", newline="\n")
    images = [render(s) for s in ICO_SIZES]
    write_ico(ICO_PATH, images)
    print(f"wrote {SVG_PATH.relative_to(ROOT)} and {ICO_PATH.relative_to(ROOT)} "
          f"({ICO_PATH.stat().st_size} bytes)")
    if "--preview" in sys.argv:
        i = sys.argv.index("--preview")
        out = Path(sys.argv[i + 1]) if len(sys.argv) > i + 1 else ICO_PATH.parent
        sizes = (256, 48, 32, 16)
        sheet = Image.new("RGBA", (sum(sizes) + 16 * (len(sizes) + 1), 256 + 32),
                          (0xF3, 0xF5, 0xF8, 255))
        x = 16
        for s in sizes:
            sheet.alpha_composite(render(s), (x, 16 + (256 - s) // 2))
            x += s + 16
        sheet.save(out / "lankey-logo-preview.png")
        print(f"wrote {out / 'lankey-logo-preview.png'}")


if __name__ == "__main__":
    main()
