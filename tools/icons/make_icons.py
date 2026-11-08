"""Draws Canvas Editor's icon (a SEKAI sky with Leo/need-style shooting stars) and writes
the files the builds use:

    MikuMikuWorld/res/canvas_icon.png    window icon + Linux, 512 px
    MikuMikuWorld/canvas_icon.ico        Windows exe icon (16 - 256 px)
    MikuMikuWorld/canvas_icon.icns       macOS bundle icon
    tools/android/res/mipmap-*/ic_launcher.png   Android launcher icons

    python tools/icons/make_icons.py      (needs Pillow)
"""
import math
import os
from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "MikuMikuWorld")


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def mix_colour(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def gradient(size, top, bottom, angle_vertical=True):
    g = Image.new("RGBA", size)
    d = ImageDraw.Draw(g)
    n = size[1] if angle_vertical else size[0]
    for i in range(n):
        c = lerp(top, bottom, i / max(1, n - 1))
        if angle_vertical:
            d.line([(0, i), (size[0], i)], fill=c)
        else:
            d.line([(i, 0), (i, size[1])], fill=c)
    return g


def polygon_mask(s, points):
    m = Image.new("L", (s, s), 0)
    ImageDraw.Draw(m).polygon(points, fill=255)
    return m


def falling_triangle(s, cx, cy, r, rot, top, bottom, trail=3, trail_len=0.16):
    """A SEKAI shard falling down: glossy gradient, bevelled light edges, ghost copies and
    streaks above it."""
    # blurred layers start as their own colour at alpha 0, so the blur has no grey fringe
    layer = Image.new("RGBA", (s, s), bottom[:3] + (0,))

    def points(px, py, pr):
        return [(px + pr * math.cos(rot + a), py + pr * math.sin(rot + a))
                for a in (-math.pi / 2, math.pi / 6, math.pi * 5 / 6)]

    # a comet tail streaming upwards: the triangle's width, its colour fading out
    pts0 = points(cx, cy, r)
    xs = [p[0] for p in pts0]
    top_y = min(p[1] for p in pts0)
    tail_h = s * trail_len * (0.9 + 0.2 * trail)
    x0, x1 = min(xs) + r * 0.25, max(xs) - r * 0.25
    alpha = Image.new("L", (s, s), 0)
    ad = ImageDraw.Draw(alpha)
    steps = 48
    for j in range(steps):
        t = j / steps
        y = cy - (cy - top_y) * 0.3 - tail_h * t
        shrink = (x1 - x0) * 0.35 * t
        ad.rectangle([x0 + shrink, y - tail_h / steps - 1, x1 - shrink, y], fill=int(150 * (1 - t) ** 1.6))
    alpha = alpha.filter(ImageFilter.GaussianBlur(s * 0.006))
    tail = Image.new("RGBA", (s, s), mix_colour(top, bottom, 0.5)[:3] + (255,))
    tail.putalpha(alpha)
    layer.alpha_composite(tail)
    d = ImageDraw.Draw(layer)
    # thin bright streaks inside the tail
    for k in (-1, 0, 1):
        x = cx + k * r * 0.38
        for j in range(20):
            t0, t1 = j / 20, (j + 1) / 20
            d.line([(x, cy - r * 0.5 - tail_h * 0.85 * t0), (x, cy - r * 0.5 - tail_h * 0.85 * t1)],
                   fill=(255, 255, 255, int(200 * (1 - t0) ** 1.5)), width=max(1, int(s * 0.004)))

    # soft shadow
    shadow = Image.new("RGBA", (s, s), (68, 68, 102, 0))
    ImageDraw.Draw(shadow).polygon(points(cx, cy + s * 0.01, r), fill=(68, 68, 102, 55))
    layer.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(s * 0.008)))

    # body: vertical gradient clipped to the triangle
    pts = points(cx, cy, r)
    body = gradient((s, s), top, bottom)
    layer.paste(body, (0, 0), polygon_mask(s, pts))
    d = ImageDraw.Draw(layer)
    # inner facet: a lighter triangle towards the upper edge (bevel)
    inner = points(cx, cy - r * 0.08, r * 0.55)
    facet = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    facet.paste(gradient((s, s), (255, 255, 255, 170), top[:3] + (0,)), (0, 0), polygon_mask(s, inner))
    layer.alpha_composite(facet)
    d = ImageDraw.Draw(layer)
    # bright rim, brighter on the edges facing up
    for a, b in ((pts[0], pts[1]), (pts[1], pts[2]), (pts[2], pts[0])):
        up = (a[1] + b[1]) / 2 < cy + r * 0.2
        d.line([a, b], fill=(255, 255, 255, 235 if up else 140), width=max(2, int(s * (0.010 if up else 0.006))))
    # a glint on the top corner
    gx, gy = pts[0]
    gr = r * 0.32
    d.polygon([(gx, gy - gr), (gx + gr * 0.2, gy), (gx, gy + gr), (gx - gr * 0.2, gy)], fill=(255, 255, 255, 230))
    d.polygon([(gx - gr, gy), (gx, gy - gr * 0.2), (gx + gr, gy), (gx, gy + gr * 0.2)], fill=(255, 255, 255, 230))
    return layer


def star_points(cx, cy, r_outer, r_inner, rot=-math.pi / 2, points=5):
    pts = []
    for i in range(points * 2):
        r = r_outer if i % 2 == 0 else r_inner
        a = rot + i * math.pi / points
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def shooting_star(s, hx, hy, angle, length, width, head, colour):
    """A shooting star like Leo/need's night sky: a five-point head, a glowing tapered tail
    streaming back along `angle` (radians, the direction the tail points), stardust on it."""
    layer = Image.new("RGBA", (s, s), colour[:3] + (0,))
    dx, dy = math.cos(angle), math.sin(angle)
    nx, ny = -dy, dx
    tx, ty = hx + dx * length, hy + dy * length

    # glow tail (blurred wedge) and a bright core
    glow = Image.new("L", (s, s), 0)
    gd = ImageDraw.Draw(glow)
    steps = 60
    for j in range(steps):
        t0, t1 = j / steps, (j + 1) / steps
        w0, w1 = width * (1 - t0) ** 1.2, width * (1 - t1) ** 1.2
        p0 = (hx + dx * length * t0, hy + dy * length * t0)
        p1 = (hx + dx * length * t1, hy + dy * length * t1)
        quad = [(p0[0] + nx * w0, p0[1] + ny * w0), (p1[0] + nx * w1, p1[1] + ny * w1),
                (p1[0] - nx * w1, p1[1] - ny * w1), (p0[0] - nx * w0, p0[1] - ny * w0)]
        gd.polygon(quad, fill=int(235 * (1 - t0) ** 1.25))
    glow = glow.filter(ImageFilter.GaussianBlur(width * 0.6))
    tail = Image.new("RGBA", (s, s), mix_colour(colour, (255, 255, 255, 255), 0.25)[:3] + (255,))
    tail.putalpha(glow)
    layer.alpha_composite(tail)

    core = Image.new("L", (s, s), 0)
    cd = ImageDraw.Draw(core)
    for j in range(steps):
        t0, t1 = j / steps, (j + 1) / steps
        cw = max(1, int(width * 0.35 * (1 - t0)))
        cd.line([(hx + dx * length * t0, hy + dy * length * t0), (hx + dx * length * t1, hy + dy * length * t1)],
                fill=int(255 * (1 - t0) ** 2), width=cw)
    white = Image.new("RGBA", (s, s), (255, 255, 255, 255))
    white.putalpha(core.filter(ImageFilter.GaussianBlur(1.2)))
    layer.alpha_composite(white)

    d = ImageDraw.Draw(layer)
    # stardust along the tail
    import random
    rnd = random.Random(int(hx * 7 + hy * 13))
    for _ in range(14):
        t = rnd.random() ** 0.7
        off = (rnd.random() - 0.5) * width * 3.2 * (1 - t * 0.5)
        x, y = hx + dx * length * t + nx * off, hy + dy * length * t + ny * off
        r = width * (0.10 + 0.25 * rnd.random()) * (1 - t * 0.6)
        a = int(255 * (1 - t) ** 0.8)
        dust = mix_colour(colour, (255, 255, 255, 255), 0.35)[:3] + (a,)
        d.polygon([(x, y - r * 2.4), (x + r * 0.6, y), (x, y + r * 2.4), (x - r * 0.6, y)], fill=dust)
        d.polygon([(x - r * 2.4, y), (x, y - r * 0.6), (x + r * 2.4, y), (x, y + r * 0.6)], fill=dust)
        d.ellipse([x - r * 0.45, y - r * 0.45, x + r * 0.45, y + r * 0.45], fill=(255, 255, 255, a))

    # head: a glowing star
    halo = Image.new("RGBA", (s, s), colour[:3] + (0,))
    ImageDraw.Draw(halo).ellipse([hx - head * 1.9, hy - head * 1.9, hx + head * 1.9, hy + head * 1.9],
                                 fill=mix_colour(colour, (255, 255, 255, 255), 0.55)[:3] + (170,))
    layer.alpha_composite(halo.filter(ImageFilter.GaussianBlur(head * 0.9)))
    d = ImageDraw.Draw(layer)
    rot = angle + math.pi   # one point leads the way
    d.polygon(star_points(hx, hy, head * 1.16, head * 0.56, rot), fill=(255, 255, 255, 255))
    d.polygon(star_points(hx, hy, head * 0.98, head * 0.46, rot), fill=colour[:3] + (255,))
    d.polygon(star_points(hx, hy - head * 0.05, head * 0.62, head * 0.29, rot), fill=mix_colour(colour, (255, 255, 255, 255), 0.55)[:3] + (255,))
    d.polygon(star_points(hx - head * 0.12, hy - head * 0.14, head * 0.26, head * 0.12, rot), fill=(255, 255, 255, 255))
    # a see-through glint crossing the head, tilted a little
    gr = head * 1.7
    tilt = math.radians(18)
    glint = Image.new("RGBA", (s, s), (255, 255, 255, 0))
    gd2 = ImageDraw.Draw(glint)
    for base in (0.0, math.pi / 2):
        a = base + tilt
        ux, uy = math.cos(a), math.sin(a)
        vx, vy = -uy, ux
        gd2.polygon([(hx + ux * gr, hy + uy * gr), (hx + vx * gr * 0.09, hy + vy * gr * 0.09),
                     (hx - ux * gr, hy - uy * gr), (hx - vx * gr * 0.09, hy - vy * gr * 0.09)], fill=(255, 255, 255, 120))
    layer.alpha_composite(glint)
    return layer


def draw(size=1024):
    """A SEKAI sky with a shooting star in every SEKAI's colour."""
    s = size
    pad = int(s * 0.04)
    radius = int(s * 0.23)
    tile = Image.new("L", (s, s), 0)
    ImageDraw.Draw(tile).rounded_rectangle([pad, pad, s - pad, s - pad], radius=radius, fill=255)

    # sky: the game's bg_sekai when the asset pack is there, else a pale blue-white glow
    sky_file = os.path.join(SRC, "res", "sekai", "bg", "bg_sekai.png")
    if os.path.exists(sky_file):
        sky = Image.open(sky_file).convert("RGBA")
        k = s / min(sky.size)
        sky = sky.resize((int(sky.width * k) + 1, int(sky.height * k) + 1), Image.LANCZOS)
        sky = sky.crop(((sky.width - s) // 2, (sky.height - s) // 2, (sky.width - s) // 2 + s, (sky.height - s) // 2 + s))
    else:
        sky = gradient((s, s), (191, 213, 255, 255), (232, 242, 255, 255))
    # a little deeper blue at the top, like dusk
    dusk = gradient((s, s), (120, 140, 235, 120), (255, 255, 255, 0))
    sky = Image.alpha_composite(sky, dusk)
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    img.paste(sky, (0, 0), tile)

    stars = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    # one shooting star for every SEKAI, in its unit colour
    sekai = {
        "virtual_singer": (51, 204, 187, 255),     # #33CCBB
        "leo_need": (68, 85, 221, 255),            # #4455DD
        "more_more_jump": (136, 221, 68, 255),     # #88DD44
        "vivid_bad_squad": (238, 17, 102, 255),    # #EE1166
        "wonderlands_showtime": (255, 153, 0, 255),  # #FF9900
        "nightcord": (136, 68, 153, 255),          # #884499
    }
    tail = -math.radians(38)       # tails stream up and to the right: the stars fall down-left
    for hx, hy, length, width, head, unit in (
        (0.80, 0.20, 0.20, 0.018, 0.034, "more_more_jump"),
        (0.52, 0.30, 0.28, 0.020, 0.038, "wonderlands_showtime"),
        (0.22, 0.38, 0.30, 0.020, 0.038, "nightcord"),
        (0.76, 0.56, 0.24, 0.020, 0.040, "vivid_bad_squad"),
        (0.46, 0.60, 0.34, 0.024, 0.046, "leo_need"),
        (0.26, 0.78, 0.56, 0.040, 0.078, "virtual_singer"),
    ):
        stars.alpha_composite(shooting_star(s, hx * s, hy * s, tail, length * s, width * s, head * s, sekai[unit]))
    # clip to the tile by multiplying alpha (a masked paste would darken the glow)
    stars.putalpha(ImageChops.multiply(stars.getchannel("A"), tile))
    img.alpha_composite(stars)

    d = ImageDraw.Draw(img)
    # the small SEKAI shards stay in the corners
    for cx, cy, r, rot, colour in ((0.20, 0.20, 0.045, -0.4, (200, 170, 255, 235)), (0.84, 0.84, 0.040, 0.8, (255, 160, 205, 235))):
        cx, cy, r = cx * s, cy * s, r * s
        pts = [(cx + r * math.cos(rot + a), cy + r * math.sin(rot + a)) for a in (-math.pi / 2, math.pi / 6, math.pi * 5 / 6)]
        d.polygon(pts, fill=colour)
        d.line(pts + [pts[0]], fill=(255, 255, 255, 230), width=max(2, s // 170))
    # twinkles
    for x, y, r in ((0.52, 0.16, 0.020), (0.86, 0.50, 0.016), (0.13, 0.36, 0.015), (0.62, 0.88, 0.017), (0.33, 0.30, 0.012)):
        x, y, r = x * s, y * s, r * s
        d.polygon([(x, y - r), (x + r * 0.22, y), (x, y + r), (x - r * 0.22, y)], fill=(255, 255, 255, 240))
        d.polygon([(x - r, y), (x, y - r * 0.22), (x + r, y), (x, y + r * 0.22)], fill=(255, 255, 255, 240))

    # white rim
    d.rounded_rectangle([pad, pad, s - pad, s - pad], radius=radius, outline=(255, 255, 255, 255), width=max(3, s // 64))
    d.rounded_rectangle([pad + s // 64, pad + s // 64, s - pad - s // 64, s - pad - s // 64], radius=radius - s // 64,
                        outline=(188, 188, 208, 160), width=max(1, s // 256))

    out = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    drop = Image.new("RGBA", (s, s), (68, 68, 102, 0))
    drop.paste((68, 68, 102, 90), (0, 0), tile)
    out.alpha_composite(drop.filter(ImageFilter.GaussianBlur(s * 0.02)), (0, int(s * 0.012)))
    out.alpha_composite(img)
    return out


def main():
    big = draw(1024)
    big.resize((512, 512), Image.LANCZOS).save(os.path.join(SRC, "res", "canvas_icon.png"))
    sizes = [16, 24, 32, 48, 64, 128, 256]
    big.save(os.path.join(SRC, "canvas_icon.ico"), sizes=[(n, n) for n in sizes])
    try:
        big.save(os.path.join(SRC, "canvas_icon.icns"))
    except Exception as e:  # older Pillow without ICNS writing
        print("icns skipped:", e)
    android = os.path.join(ROOT, "tools", "android", "res")
    for name, n in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)):
        folder = os.path.join(android, "mipmap-" + name)
        os.makedirs(folder, exist_ok=True)
        big.resize((n, n), Image.LANCZOS).save(os.path.join(folder, "ic_launcher.png"))
    print("icons written")


if __name__ == "__main__":
    main()
