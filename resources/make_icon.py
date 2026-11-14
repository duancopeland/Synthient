"""Draws the Synthient app icon: a synthwave sunset sliced by a sound wave.

The sun's classic horizontal cut-outs are replaced by a sine wave that slices
through it and carries on across the horizon grid - retro synth + audio in one mark.

Run: analysis\\.venv\\Scripts\\python.exe resources\\make_icon.py
Writes resources/icon.png (1024 px) and resources/icon.ico (16-256 px).
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter

S = 2048          # drawn at 2x, downsampled for smooth edges
OUT = 1024
HERE = os.path.dirname(os.path.abspath(__file__))


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def vertical_gradient(size, stops):
    w, h = size
    img = Image.new("RGBA", size)
    px = img.load()
    for y in range(h):
        t = y / (h - 1)
        for i in range(len(stops) - 1):
            t0, c0 = stops[i]
            t1, c1 = stops[i + 1]
            if t0 <= t <= t1:
                col = lerp(c0, c1, (t - t0) / (t1 - t0))
                break
        for x in range(w):
            px[x, y] = col
    return img


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def stroke(draw, pts, width, fill):
    """Smooth thick line: stamp discs densely along the path (PIL's wide lines leave jaggies)."""
    rad = width / 2
    for (xa, ya), (xb, yb) in zip(pts, pts[1:]):
        n = max(1, int(math.hypot(xb - xa, yb - ya) / 2))
        for k in range(n + 1):
            x, y = xa + (xb - xa) * k / n, ya + (yb - ya) * k / n
            draw.ellipse([x - rad, y - rad, x + rad, y + rad], fill=fill)


def wave_points(x0, x1, y, amp, cycles, phase=0.0, steps=600):
    pts = []
    for i in range(steps + 1):
        x = x0 + (x1 - x0) * i / steps
        pts.append((x, y + amp * math.sin(phase + cycles * 2 * math.pi * i / steps)))
    return pts


def main():
    # Night sky background with a soft purple glow near the horizon.
    bg = vertical_gradient((S, S), [(0.0, (12, 10, 32, 255)), (0.62, (44, 14, 72, 255)), (1.0, (10, 8, 24, 255))])
    icon = Image.new("RGBA", (S, S))
    icon.paste(bg, (0, 0))

    k2 = S / 1024
    horizon = int(640 * k2)
    cx, cy, r = S // 2, int(600 * k2), int(330 * k2)

    # Sun: gold -> hot pink -> violet, clipped at the horizon.
    sun_grad = vertical_gradient((2 * r, 2 * r), [(0.0, (255, 214, 102, 255)), (0.55, (255, 79, 163, 255)), (1.0, (150, 60, 255, 255))])
    sun_mask = Image.new("L", (2 * r, 2 * r), 0)
    ImageDraw.Draw(sun_mask).ellipse([0, 0, 2 * r - 1, 2 * r - 1], fill=255)
    md = ImageDraw.Draw(sun_mask)
    md.rectangle([0, horizon - (cy - r), 2 * r, 2 * r], fill=0)

    # The clever bit: the sun's stripes are cut by sine waves that thicken toward the horizon.
    for i, (yy, thick) in enumerate([(470, 16), (528, 26), (586, 38)]):
        pts = [(x - (cx - r), y - (cy - r)) for x, y in wave_points(cx - r - 20, cx + r + 20, yy * k2, (14 + i * 4) * k2, 1.5, phase=i * 0.9)]
        stroke(md, pts, thick * k2, 0)

    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse([cx - r - 80, cy - r - 80, cx + r + 80, cy + r + 80], fill=(255, 79, 163, 90))
    glow = glow.filter(ImageFilter.GaussianBlur(120))
    icon = Image.alpha_composite(icon, glow)
    icon.paste(sun_grad, (cx - r, cy - r), sun_mask)

    # Perspective grid floor in cyan.
    grid = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    g = ImageDraw.Draw(grid)
    cyan = (79, 209, 255, 200)
    for k in range(-9, 10):
        g.line([(cx + k * 80, horizon), (cx + k * 380, S)], fill=cyan, width=10)
    y, step = horizon + 28, 36
    while y < S:
        g.line([(0, y), (S, y)], fill=cyan, width=10)
        step *= 1.45
        y += step
    g.line([(0, horizon), (S, horizon)], fill=(255, 140, 220, 255), width=14)
    icon = Image.alpha_composite(icon, grid)

    # The sound wave itself, glowing, sweeping across the sun and out over the grid.
    wave = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    w = ImageDraw.Draw(wave)
    pts = wave_points(150, S - 150, 612 * k2, 120 * k2, 2.0, phase=0.0)
    stroke(w, pts, 92, (120, 230, 255, 255))
    halo = wave.filter(ImageFilter.GaussianBlur(56))
    icon = Image.alpha_composite(icon, halo)
    icon = Image.alpha_composite(icon, halo)
    core = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    stroke(ImageDraw.Draw(core), pts, 44, (235, 252, 255, 255))
    icon = Image.alpha_composite(icon, wave)
    icon = Image.alpha_composite(icon, core)

    # Rounded app-tile shape.
    final = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    final.paste(icon, (0, 0), rounded_mask(S, 420))
    final = final.resize((OUT, OUT), Image.LANCZOS)

    final.save(os.path.join(HERE, "icon.png"))
    final.save(os.path.join(HERE, "icon.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    final.resize((64, 64), Image.LANCZOS).save(os.path.join(HERE, "icon_64_preview.png"))
    print("wrote icon.png and icon.ico")


if __name__ == "__main__":
    main()
