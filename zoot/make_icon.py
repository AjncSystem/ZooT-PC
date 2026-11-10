"""ZooT-PC icon: original art. A heater shield with a silver rim and rivets, a deep blue field and
its own emblem (a gold ocarina over a red banner). No Nintendo artwork (no Triforce, no crest).
Writes zoot/art/zoot.ico (16-256 px), zoot/art/zoot-preview.png and zoot/art/zoot-small-sizes.png.

Run with: python -I zoot/make_icon.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "art")
S = 1024  # drawn large, scaled down

SILVER_LIGHT = (238, 242, 248, 255)
SILVER = (178, 186, 198, 255)
SILVER_DARK = (92, 100, 114, 255)
BLUE_LIGHT = (52, 92, 178, 255)
BLUE = (28, 56, 128, 255)
BLUE_DARK = (12, 26, 70, 255)
GOLD_LIGHT = (255, 232, 150, 255)
GOLD = (226, 180, 58, 255)
GOLD_DARK = (138, 98, 24, 255)
RED_LIGHT = (226, 74, 58, 255)
RED = (176, 36, 30, 255)
RED_DARK = (104, 18, 16, 255)


def shield_points(cx, top, w, h, steps=80):
    """Heater shield: a top edge rising to a soft peak, sides curving to a point."""
    half = w / 2
    pts = []
    for i in range(steps + 1):
        t = i / steps
        x = cx - half + w * t
        y = top + 0.05 * h * (1 - math.sin(math.pi * t))
        pts.append((x, y))
    for i in range(1, steps + 1):
        t = i / steps
        x = cx + half * (1 - t ** 1.7)
        y = top + 0.05 * h + (0.95 * h) * (math.sin(t * math.pi / 2) ** 1.1)
        pts.append((x, y))
    for i in range(steps - 1, 0, -1):
        t = i / steps
        x = cx - half * (1 - t ** 1.7)
        y = top + 0.05 * h + (0.95 * h) * (math.sin(t * math.pi / 2) ** 1.1)
        pts.append((x, y))
    return pts


def gradient(size, top, bottom, diagonal=False):
    g = Image.new("RGBA", size)
    d = ImageDraw.Draw(g)
    w, h = size
    n = w + h if diagonal else h
    for k in range(n):
        t = k / max(1, n - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(4))
        if diagonal:
            d.line([(k, 0), (k - h, h)], fill=c, width=2)
        else:
            d.line([(0, k), (w, k)], fill=c)
    return g


def mask_of(points):
    m = Image.new("L", (S, S), 0)
    ImageDraw.Draw(m).polygon(points, fill=255)
    return m


def paste(img, fill, mask):
    img.paste(fill, (0, 0), mask)


def draw():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    cx, top, w, h = S / 2, S * 0.05, S * 0.82, S * 0.90
    outer = shield_points(cx, top, w, h)
    mid = shield_points(cx, top + S * 0.045, w - S * 0.09, h - S * 0.10)
    inner = shield_points(cx, top + S * 0.075, w - S * 0.15, h - S * 0.165)

    # drop shadow
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).polygon([(x + 10, y + 18) for x, y in outer], fill=(0, 0, 0, 150))
    img = Image.alpha_composite(img, shadow.filter(ImageFilter.GaussianBlur(16)))

    # silver rim: bright diagonal sheen, then a darker bevel step
    paste(img, gradient((S, S), SILVER_LIGHT, SILVER_DARK, diagonal=True), mask_of(outer))
    paste(img, gradient((S, S), SILVER, SILVER_LIGHT, diagonal=True), mask_of(mid))
    # blue field
    paste(img, gradient((S, S), BLUE_LIGHT, BLUE_DARK), mask_of(inner))
    d = ImageDraw.Draw(img)
    d.line(inner + [inner[0]], fill=SILVER_DARK, width=8)
    d.line(outer + [outer[0]], fill=SILVER_DARK, width=6)

    # rivets on the rim
    for t in (0.10, 0.50, 0.90):
        x = cx - (w - S * 0.045) / 2 + (w - S * 0.045) * t
        y = top + S * 0.026 + 0.05 * h * (1 - math.sin(math.pi * t)) * 0.95
        rivet(img, x, y)
    for side in (-1, 1):
        for t in (0.35, 0.68):
            x = cx + side * (w / 2 - S * 0.022) * (1 - t ** 1.7)
            y = top + 0.05 * h + 0.95 * h * (math.sin(t * math.pi / 2) ** 1.1) - S * 0.01
            rivet(img, x - side * S * 0.004, y)

    img = Image.alpha_composite(img, eagle(cx, S * 0.46))
    return img


def eagle(cx, cy):
    """A generic heraldic eagle displayed, in gold: raised wings with feathers fanning down and
    out from each arm, a fanned tail, the head turned to the dexter side."""
    m = Image.new("L", (S, S), 0)  # silhouette
    md = ImageDraw.Draw(m)
    feathers = []  # outlines drawn after the gold fill

    def feather(bx, by, ang, length, width):
        """Tapered feather from base (bx, by) along angle `ang` (radians, image coordinates)."""
        dx, dy = math.cos(ang), math.sin(ang)
        px, py = -dy, dx
        tipx, tipy = bx + dx * length, by + dy * length
        pts = [(bx + px * width / 2, by + py * width / 2)]
        for k in range(1, 9):
            t = k / 8
            wdt = width / 2 * (1 - t ** 3)
            pts.append((bx + dx * length * t + px * wdt, by + dy * length * t + py * wdt))
        pts.append((tipx, tipy))
        for k in range(8, 0, -1):
            t = k / 8
            wdt = width / 2 * (1 - t ** 3)
            pts.append((bx + dx * length * t - px * wdt, by + dy * length * t - py * wdt))
        pts.append((bx - px * width / 2, by - py * width / 2))
        md.polygon(pts, fill=255)
        feathers.append(pts)

    for side in (-1, 1):
        # wing arm: from the shoulder up and out, as a thick curved band
        sx, sy = cx + side * S * 0.055, cy - S * 0.045
        tx, ty = cx + side * S * 0.225, cy - S * 0.205
        arm = []
        n = 7
        for k in range(n):
            t = k / (n - 1)
            # slight upward bow
            x = sx + (tx - sx) * t
            y = sy + (ty - sy) * t - S * 0.035 * math.sin(math.pi * t)
            arm.append((x, y))
        md.line(arm, fill=255, width=int(S * 0.06), joint="curve")
        tip_r = S * 0.03
        md.ellipse([tx - tip_r, ty - tip_r, tx + tip_r, ty + tip_r], fill=255)
        # flight feathers hanging from the arm, fanning from steep (inner) to flat (outer)
        for k in range(n):
            t = k / (n - 1)
            bx, by = arm[k]
            down = math.radians(78 - 62 * t)  # angle below horizontal
            ang = down if side > 0 else math.pi - down
            length = S * (0.10 + 0.06 * math.sin(math.pi * (0.25 + 0.6 * t)))
            feather(bx, by + S * 0.012, ang, length, S * 0.045)

    # tail fan
    for k in range(5):
        a = math.radians(70 + 10 * k)
        feather(cx, cy + S * 0.12, a, S * 0.13, S * 0.05)
    # legs with spread talons
    for side in (-1, 1):
        hx, hy = cx + side * S * 0.045, cy + S * 0.11
        fx, fy = cx + side * S * 0.095, cy + S * 0.17
        md.line([(hx, hy), (fx, fy)], fill=255, width=int(S * 0.025))
        for a in (-35, 0, 35):
            ang = math.radians((60 if side > 0 else 120) + a)
            md.line([(fx, fy), (fx + math.cos(ang) * S * 0.035, fy + math.sin(ang) * S * 0.035)],
                    fill=255, width=int(S * 0.012))
    # body and neck
    md.ellipse([cx - S * 0.065, cy - S * 0.07, cx + S * 0.065, cy + S * 0.15], fill=255)
    md.ellipse([cx - S * 0.04, cy - S * 0.14, cx + S * 0.045, cy - S * 0.02], fill=255)
    # head turned right, with a hooked beak
    hx, hy, hr = cx + S * 0.025, cy - S * 0.15, S * 0.042
    md.ellipse([hx - hr, hy - hr, hx + hr, hy + hr], fill=255)
    # hooked raptor beak: upper mandible arching over and down to a point, short lower mandible
    beak = [(hx + hr * 0.55, hy - hr * 0.55), (hx + hr * 1.25, hy - hr * 0.62),
            (hx + hr * 1.75, hy - hr * 0.30), (hx + hr * 1.95, hy + hr * 0.15),
            (hx + hr * 1.80, hy + hr * 0.75), (hx + hr * 1.62, hy + hr * 0.30),
            (hx + hr * 1.30, hy + hr * 0.38), (hx + hr * 0.60, hy + hr * 0.45)]
    md.polygon(beak, fill=255)
    # crest feathers at the back of the head
    for a in (200, 220, 240):
        ang = math.radians(a)
        feather(hx - hr * 0.6, hy, ang, S * 0.045, S * 0.022)

    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    layer.paste(gradient((S, S), GOLD_LIGHT, GOLD_DARK), (0, 0), m)
    d = ImageDraw.Draw(layer)
    line = (96, 64, 12, 255)
    for pts in feathers:
        d.line(pts + [pts[0]], fill=line, width=5)
    # body feather scallops
    for row in range(3):
        for col in range(-1, 2):
            x = cx + col * S * 0.035 + (S * 0.017 if row % 2 else 0)
            y = cy + S * (0.0 + 0.045 * row)
            d.arc([x - S * 0.022, y - S * 0.02, x + S * 0.022, y + S * 0.02], 20, 160, fill=line,
                  width=4)
    # eye
    ex, ey, er = hx + hr * 0.35, hy - hr * 0.2, S * 0.009
    d.ellipse([ex - er, ey - er, ex + er, ey + er], fill=BLUE_DARK)
    # outline of the whole silhouette
    edge = m.filter(ImageFilter.FIND_EDGES).filter(ImageFilter.MaxFilter(5))
    layer.paste(Image.new("RGBA", (S, S), line), (0, 0), edge)
    # glossy highlight on the upper half
    hl = Image.new("L", (S, S), 0)
    ImageDraw.Draw(hl).ellipse([cx - S * 0.25, cy - S * 0.26, cx + S * 0.25, cy - S * 0.04],
                               fill=70)
    hl = hl.filter(ImageFilter.GaussianBlur(20))
    layer.paste(Image.new("RGBA", (S, S), (255, 250, 225, 255)), (0, 0),
                Image.composite(hl, Image.new("L", (S, S), 0), m))

    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", (S, S), (0, 0, 0, 120)), (8, 12), m)
    return Image.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(9)), layer)


def rivet(img, x, y):
    r = S * 0.017
    d = ImageDraw.Draw(img)
    d.ellipse([x - r, y - r, x + r, y + r], fill=SILVER_DARK)
    d.ellipse([x - r * 0.8, y - r * 0.8, x + r * 0.6, y + r * 0.6], fill=SILVER_LIGHT)


def banner(cx, cy):
    """A plain red heraldic ribbon with folded ends, no text."""
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    half = S * 0.25
    hgt = S * 0.075
    wave = S * 0.03

    def y_at(x):
        return cy + wave * math.sin((x - cx) / half * math.pi * 0.9)

    # folded tails behind
    for side in (-1, 1):
        x0 = cx + side * half * 0.88
        x1 = cx + side * (half + S * 0.09)
        y0 = y_at(x0) + S * 0.03
        tail = [(x0, y0 - hgt / 2), (x1, y0 - hgt / 2), (x1 - side * S * 0.035, y0),
                (x1, y0 + hgt / 2), (x0, y0 + hgt / 2)]
        d.polygon(tail, fill=RED_DARK)
    # main ribbon, built from thin vertical strips so it follows the wave
    steps = 120
    for i in range(steps):
        xa = cx - half + 2 * half * i / steps
        xb = cx - half + 2 * half * (i + 1) / steps
        ya, yb = y_at(xa), y_at(xb)
        shade = 0.5 + 0.5 * math.cos((xa - cx) / half * math.pi * 0.9)
        col = tuple(int(RED[k] + (RED_LIGHT[k] - RED[k]) * shade) for k in range(3)) + (255,)
        d.polygon([(xa, ya - hgt / 2), (xb, yb - hgt / 2), (xb, yb + hgt / 2),
                   (xa, ya + hgt / 2)], fill=col)
    # gold edges
    edge_top = [(cx - half + 2 * half * i / steps, y_at(cx - half + 2 * half * i / steps) - hgt / 2)
                for i in range(steps + 1)]
    edge_bot = [(x, y + hgt) for x, y in edge_top]
    d.line(edge_top, fill=GOLD, width=7)
    d.line(edge_bot, fill=GOLD, width=7)
    return layer


def ocarina(cx, cy):
    """Classic transverse ocarina in gold: an egg-shaped body, a short mouthpiece and small
    finger holes, tilted."""
    C = S
    oc = Image.new("RGBA", (C, C), (0, 0, 0, 0))
    ox, oy = C / 2, C / 2
    A, B = C * 0.235, C * 0.105
    body = []
    for i in range(240):
        t = 2 * math.pi * i / 240
        body.append((ox + A * math.cos(t), oy + B * math.sin(t) * (1.0 - 0.38 * math.cos(t))))
    mp_mask = Image.new("L", (C, C), 0)
    md = ImageDraw.Draw(mp_mask)
    sx, sy = ox - A * 0.62, oy - B * 0.55
    ex, ey = ox - A * 0.98, oy - B * 1.45
    md.line([(sx, sy), (ex, ey)], fill=255, width=int(C * 0.08))
    r = C * 0.04
    md.ellipse([ex - r, ey - r, ex + r, ey + r], fill=255)
    body_mask = Image.new("L", (C, C), 0)
    ImageDraw.Draw(body_mask).polygon(body, fill=255)
    shape = Image.composite(Image.new("L", (C, C), 255), body_mask, mp_mask)

    oc.paste(gradient((C, C), GOLD_LIGHT, GOLD_DARK), (0, 0), shape)
    edge = shape.filter(ImageFilter.FIND_EDGES).filter(ImageFilter.MaxFilter(7))
    oc.paste(Image.new("RGBA", (C, C), (96, 64, 12, 255)), (0, 0), edge)
    hl = Image.new("L", (C, C), 0)
    ImageDraw.Draw(hl).ellipse([ox - A * 0.55, oy - B * 0.85, ox + A * 0.35, oy - B * 0.35],
                               fill=160)
    hl = hl.filter(ImageFilter.GaussianBlur(10))
    oc.paste(Image.new("RGBA", (C, C), (255, 250, 220, 255)), (0, 0),
             Image.composite(hl, Image.new("L", (C, C), 0), shape))
    d = ImageDraw.Draw(oc)
    d.ellipse([ex - r * 0.45, ey - r * 0.45, ex + r * 0.45, ey + r * 0.45], fill=(80, 52, 10, 255))
    for hx, hy, hr in [(-0.30, -0.18, 0.025), (-0.08, -0.26, 0.025), (0.14, -0.24, 0.025),
                       (0.36, -0.14, 0.023), (-0.18, 0.30, 0.018), (0.10, 0.28, 0.018)]:
        x, y, rr = ox + A * hx * 2, oy + B * hy * 2, C * hr
        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=BLUE_DARK)
    oc = oc.rotate(14, resample=Image.BICUBIC, center=(ox, oy))
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.alpha_composite(oc, (int(cx - ox), int(cy - oy)))
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", (S, S), (0, 0, 0, 120)), (8, 14), out.split()[3])
    return Image.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(10)), out)


def sword():
    """A plain longsword drawn diagonally (hilt lower left, tip upper right): a straight silver
    blade with a fuller, a straight gold crossguard, a leather-wrapped grip and a round pommel."""
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    x0, y0 = S * 0.13, S * 0.90   # pommel end
    x1, y1 = S * 0.90, S * 0.10   # tip
    length = math.hypot(x1 - x0, y1 - y0)
    dx, dy = (x1 - x0) / length, (y1 - y0) / length
    px, py = -dy, dx

    def at(t, off=0.0):
        return (x0 + dx * length * t + px * off, y0 + dy * length * t + py * off)

    guard_t, grip_t = 0.27, 0.07
    bw = S * 0.036  # half blade width
    blade = [at(guard_t, bw), at(0.90, bw), at(1.0, 0), at(0.90, -bw), at(guard_t, -bw)]
    blade_mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(blade_mask).polygon(blade, fill=255)
    steel = gradient((S, S), SILVER_LIGHT, SILVER_DARK, diagonal=True)
    layer.paste(steel, (0, 0), blade_mask)
    # bright edge on one side, fuller down the middle
    d.line([at(guard_t + 0.01, bw * 0.55), at(0.9, bw * 0.55)], fill=(255, 255, 255, 220),
           width=int(S * 0.008))
    d.line([at(guard_t + 0.02, -bw * 0.05), at(0.82, -bw * 0.05)], fill=SILVER_DARK,
           width=int(S * 0.012))
    d.line(blade + [blade[0]], fill=(70, 78, 92, 255), width=5)
    # grip with leather wraps
    gw = S * 0.024
    grip = [at(grip_t, gw), at(guard_t, gw), at(guard_t, -gw), at(grip_t, -gw)]
    d.polygon(grip, fill=(96, 58, 30, 255), outline=(52, 30, 14, 255))
    for k in range(7):
        t = grip_t + (guard_t - grip_t) * (k + 0.5) / 7
        d.line([at(t - 0.012, gw), at(t + 0.012, -gw)], fill=(60, 34, 16, 255), width=5)
    # crossguard
    cg = S * 0.135
    ct = S * 0.022
    guard = [at(guard_t - ct / length, cg), at(guard_t + ct / length, cg),
             at(guard_t + ct / length, -cg), at(guard_t - ct / length, -cg)]
    gmask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(gmask).polygon(guard, fill=255)
    layer.paste(gradient((S, S), GOLD_LIGHT, GOLD_DARK, diagonal=True), (0, 0), gmask)
    d.line(guard + [guard[0]], fill=GOLD_DARK, width=5)
    for side in (1, -1):
        cx, cy = at(guard_t, side * cg)
        r = S * 0.026
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=GOLD, outline=GOLD_DARK, width=5)
    # pommel
    pxm, pym = at(grip_t - 0.025)
    r = S * 0.038
    d.ellipse([pxm - r, pym - r, pxm + r, pym + r], fill=GOLD, outline=GOLD_DARK, width=6)
    d.ellipse([pxm - r * 0.45, pym - r * 0.6, pxm + r * 0.1, pym - r * 0.1],
              fill=GOLD_LIGHT)
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", (S, S), (0, 0, 0, 130)), (8, 12), layer.split()[3])
    return Image.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(10)), layer)


def with_sword(shield):
    """The shield, a little smaller, over the sword crossing behind it."""
    out = sword()
    scale = 0.80
    small = shield.resize((int(S * scale), int(S * scale)), Image.LANCZOS)
    out.alpha_composite(small, (int(S * (1 - scale) / 2), int(S * (1 - scale) / 2)))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    big = with_sword(draw())
    big.resize((128, 128), Image.LANCZOS).save(os.path.join(OUT, "zoot-128.png"))
    big.resize((512, 512), Image.LANCZOS).save(os.path.join(OUT, "zoot-preview.png"))
    big.resize((256, 256), Image.LANCZOS).save(
        os.path.join(OUT, "zoot.ico"), sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, "zoot-256.png"))
    strip = Image.new("RGBA", (16 + 24 + 32 + 48 + 64 + 6 * 12, 64), (40, 40, 40, 255))
    x = 12
    for s in (16, 24, 32, 48, 64):
        strip.alpha_composite(big.resize((s, s), Image.LANCZOS), (x, (64 - s) // 2))
        x += s + 12
    strip.resize((strip.width * 3, strip.height * 3), Image.NEAREST).save(
        os.path.join(OUT, "zoot-small-sizes.png"))
    print("ok", OUT)


if __name__ == "__main__":
    main()
