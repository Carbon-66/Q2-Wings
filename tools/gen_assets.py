#!/usr/bin/env python3
"""Generates src/assets.h (everything the game draws that isn't worked out at run time) and the
card's icons. Usage: tools/gen_assets.py [REMAKE_DIR]

  font    Poppins Bold (SIL OFL 1.1, card/OFL-Poppins.txt) at three sizes: per glyph an alpha map
          and an outline map (the alpha grown by 1 or 2 px, solid), so text reads on any sky. The
          game draws a string's outlines first and its letters after, so no outline covers the
          letter before it
  bird    Pip, an original character: a round coral bird with a peach face, a yellow beak and a
          two-feather crest. Drawn at 8x and shrunk; poses glide, dive, sleep, flap
  noise   the paper grain the hills and sky are multiplied by: Tiny Wings Remake's noise.png
          (MIT, Sergey Tikhonov), 512 x 512, made to tile with no seam, as 0..255 around its mean
  clouds  three soft cloud shapes, alpha only
"""
import math, pathlib, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

ROOT = pathlib.Path(__file__).resolve().parent.parent
REMAKE = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'third_party' / 'tw-images'
FONT = '/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf'
out = []


def arr(name, data, ctype='unsigned char'):
    body = ','.join(str(b) for b in data)
    out.append(f'static const {ctype} {name}[{len(data)}] = {{{body}}};\n')


# ---------------------------------------------------------------------------------------------
# font

def font(tag, size, grow):
    f = ImageFont.truetype(FONT, size)
    asc, desc = f.getmetrics()
    h = asc + desc + 2 * grow
    glyphs, alpha, outline = [], bytearray(), bytearray()
    for ch in range(32, 127):
        c = chr(ch)
        adv = int(round(f.getlength(c)))
        w = max(adv, 1) + 2 * grow + 2
        im = Image.new('L', (w, h), 0)
        ImageDraw.Draw(im).text((grow + 1, grow), c, font=f, fill=255)
        # a round, solid outline: the letter's alpha spread over a disc of radius grow
        ol = im.copy()
        for dy in range(-grow, grow + 1):
            for dx in range(-grow, grow + 1):
                if dx * dx + dy * dy <= grow * grow + grow:
                    ol = ImageChops.lighter(ol, ImageChops.offset(im, dx, dy))
        glyphs.append((len(alpha), w, adv))
        alpha += im.tobytes()
        outline += ol.tobytes()
    arr(f'font_{tag}_alpha', alpha)
    arr(f'font_{tag}_outline', outline)
    out.append(f'static const unsigned int font_{tag}_glyphs[95][3] = {{' +
               ','.join(f'{{{o},{w},{a}}}' for o, w, a in glyphs) + '};\n')
    out.append(f'#define FONT_{tag.upper()}_H {h}\n#define FONT_{tag.upper()}_GROW {grow}\n')


font('big', 30, 3)
font('mid', 20, 2)
font('small', 15, 1)

# ---------------------------------------------------------------------------------------------
# Pip, the bird: body centre at the sprite's centre, facing right (+x)

BIRD = 44  # sprite size, px
S = 8


# How Pip opens up for a coin. The upper and lower halves of the beak turn about the hinge at the
# back of the beak (up and down by the angles below), over a dark mouth, with a tongue in some.
#   style:   (upper deg, lower deg, tongue, beak scale)
# The 'warm' styles: the inside of the beak is a deeper shade of the beak's own orange, lighter
# towards the tips, with only a small dark throat at the back. Same tuple, plus the throat size.
WARM_STYLES = {
    'warm':      (24, 22, 0.9, 1.15),  # opened evenly
    'warm-wide': (34, 34, 1.0, 1.3),   # a bigger beak opened wider, for a real gulp
    'warm-up':   (42, 14, 0.9, 1.3),   # the top of the beak lifted high, the bottom a little
}
MOUTH_STYLES = {
    'wedge':  None,               # v0.5: two separate wedges over a wide dark mouth
    'hinge':  (20, 22, True, 1.0),  # opened evenly, like a hinge
    'gentle': (10, 14, False, 1.0), # just parted
    'gulp':   (26, 40, True, 1.15), # a big, round gulp
    'jaw':    (0, 42, True, 1.05),  # the top stays put, the bottom drops right open
    'chomp':  (32, 32, False, 0.8), # a short beak opened very wide, a small round mouth
}
MOUTH_STYLE = 'warm'  # chosen: the light inside, opened evenly


def rot(p, o, deg):
    a = math.radians(deg)
    x, y = p[0] - o[0], p[1] - o[1]
    return (o[0] + x * math.cos(a) - y * math.sin(a), o[1] + x * math.sin(a) + y * math.cos(a))


def quad(p0, p1, p2, n=10):
    """points along a quadratic curve from p0 to p2, pulled towards p1"""
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
             (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]) for t in (i / n for i in range(n + 1))]


def open_beak_warm(d, bx, by, style, line):
    """The beak opened as a sideways V: two curved halves, thick at the back and pointed at the
    tips, splayed out from the corner of the mouth. The inside is a warm orange, deeper at the
    back, with a small dark throat; it stops short of the tips with a curved edge, so there is
    sky between the tips and it reads as an open beak rather than a filled-in mouth."""
    up, down, throat, k = WARM_STYLES[style]
    hx, hy = bx - 0.4 * S, by + 0.4 * S                     # the corner of the mouth
    L = 7.4 * S * k
    ru, rd = math.radians(up), math.radians(down)
    tu = (hx + L * math.cos(ru), hy - L * math.sin(ru))      # upper tip
    tl = (hx + L * 0.88 * math.cos(rd), hy + L * 0.88 * math.sin(rd))  # lower tip
    bu = (hx - 1.4 * S, hy - 3.6 * S * k)                    # the halves' backs, on the face
    bl = (hx - 1.4 * S, hy + 3.2 * S * k)

    def along(p, f):
        return (hx + (p[0] - hx) * f, hy + (p[1] - hy) * f)

    # the inside: from the corner out to 80% of each inner edge, the front edge curving back
    def cavity(f, col):
        a, b = along(tu, 0.8 * f), along(tl, 0.8 * f)
        mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
        dip = (hx + (mid[0] - hx) * 0.45, hy + (mid[1] - hy) * 0.45)
        d.polygon([(hx, hy)] + quad(a, dip, b), fill=col)
    for i, f in enumerate((1.0, 0.8, 0.6, 0.42)):
        t = i / 3
        cavity(f, (int(250 - 20 * t), int(176 - 64 * t), int(98 - 44 * t), 255))
    r = 1.3 * S * throat
    cx, cy = hx + 1.5 * S, hy + 0.3 * S
    d.ellipse([cx - r, cy - r * 1.15, cx + r, cy + r * 1.15], fill=(150, 58, 44, 255))

    # the halves: inner edge straight from the corner to the tip, outer edge bowed outwards
    def half(back, tip, bow, fill):
        mid = ((back[0] + tip[0]) / 2, (back[1] + tip[1]) / 2)
        dx, dy = tip[0] - back[0], tip[1] - back[1]
        n = math.hypot(dx, dy)
        ctrl = (mid[0] + dy / n * bow, mid[1] - dx / n * bow)
        pts = [(hx, hy)] + quad(back, ctrl, tip)[::-1][:-1] + [back] if False else [(hx, hy), tip] + quad(tip, ctrl, back)[1:]
        d.polygon(pts, fill=fill, outline=line)
        return pts
    half(bu, tu, 1.6 * S * k, (255, 202, 52, 255))
    half(bl, tl, -1.4 * S * k, (244, 168, 38, 255))
    # a pale shine along the top half's back
    sh = quad(along(bu, 1.0), ((bu[0] + tu[0]) / 2, (bu[1] + tu[1]) / 2 - 0.6 * S), tu, 6)[1:4]
    d.line([(x + 0.5 * S, y + 0.9 * S) for x, y in sh], fill=(255, 236, 168, 255), width=int(S * 0.9))


def open_beak(d, bx, by, style, line):
    if style in WARM_STYLES:
        open_beak_warm(d, bx, by, style, line)
        return
    if MOUTH_STYLES[style] is None:  # the v0.5 drawing
        d.polygon([(bx - 1.6 * S, by - 1.6 * S), (bx + 6.0 * S, by - 4.6 * S), (bx + 5.4 * S, by + 4.8 * S),
                   (bx - 1.6 * S, by + 1.8 * S)], fill=(120, 26, 40, 255), outline=line)
        d.ellipse([bx + 0.2 * S, by + 0.6 * S, bx + 4.2 * S, by + 3.0 * S], fill=(236, 112, 120, 255))
        d.polygon([(bx - 1.2 * S, by - 3.8 * S), (bx + 7.0 * S, by - 4.4 * S), (bx - 1.2 * S, by - 0.4 * S)],
                  fill=(255, 196, 40, 255), outline=line)
        d.polygon([(bx - 1.2 * S, by + 1.0 * S), (bx + 6.2 * S, by + 4.6 * S), (bx - 1.2 * S, by + 3.8 * S)],
                  fill=(250, 176, 30, 255), outline=line)
        return
    up, down, tongue, k = MOUTH_STYLES[style]
    hinge = (bx - 1.0 * S, by + 0.4 * S)
    L = 7.5 * S * k  # the closed beak, split along its middle line
    upper = [(bx - 1.0 * S, by - 3.2 * S * k), (bx - 1.0 * S + L, by + 0.4 * S), hinge]
    lower = [hinge, (bx - 1.0 * S + L * 0.92, by + 0.4 * S), (bx - 1.0 * S, by + 3.4 * S * k)]
    upper = [rot(p, hinge, -up) for p in upper]
    lower = [rot(p, hinge, down) for p in lower]
    # the mouth: from the hinge out between the two tips, rounded at the back
    back = (hinge[0] - 1.2 * S, hinge[1])
    d.polygon([back, upper[1], lower[1]], fill=(110, 24, 38, 255), outline=line)
    if tongue:
        tx, ty = rot((hinge[0] + L * 0.45, hinge[1] + 0.6 * S), hinge, down * 0.6)
        r = 1.7 * S * k
        d.ellipse([tx - r * 1.3, ty - r * 0.8, tx + r * 1.3, ty + r * 0.8], fill=(238, 110, 122, 255))
    d.polygon(upper, fill=(255, 196, 40, 255), outline=line)
    d.polygon(lower, fill=(246, 170, 28, 255), outline=line)


def bird(pose, flap=0.0, mouth=False):
    n = BIRD * S
    im = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = n / 2
    r = 13.5 * S
    squash = 0.9 if pose == 'dive' else 1.0
    rx, ry = r / squash ** 0.5, r * squash
    body, dark, face = (238, 98, 74, 255), (176, 52, 44, 255), (255, 206, 160, 255)
    line = (92, 30, 30, 255)
    # crest: two curled feathers leaning back
    for k, (ang, ln) in enumerate(((-115, 7.5), (-95, 9.0))):
        a = math.radians(ang)
        bx, by = c + math.cos(a) * ry * 0.75, c + math.sin(a) * ry * 0.75
        tx, ty = bx + math.cos(a - 0.5) * ln * S, by + math.sin(a - 0.5) * ln * S
        d.line([(bx, by), (tx, ty)], fill=dark, width=int(2.6 * S))
        d.ellipse([tx - 2.0 * S, ty - 2.0 * S, tx + 2.0 * S, ty + 2.0 * S], fill=dark)
    # tail: three short feathers at the back
    for k in (-1, 0, 1):
        a = math.radians(180 + k * 18 - 8)
        bx, by = c + math.cos(a) * rx * 0.8, c + math.sin(a) * ry * 0.8
        tx, ty = bx + math.cos(a) * 6.5 * S, by + math.sin(a) * 6.5 * S
        d.line([(bx, by), (tx, ty)], fill=dark, width=int(3.2 * S))
    # body with an outline
    d.ellipse([c - rx - 1.2 * S, c - ry - 1.2 * S, c + rx + 1.2 * S, c + ry + 1.2 * S], fill=line)
    d.ellipse([c - rx, c - ry, c + rx, c + ry], fill=body)
    # shading: a darker lower back, a light top
    sh = Image.new('L', (n, n), 0)
    ImageDraw.Draw(sh).ellipse([c - rx * 1.3, c - ry * 0.2, c + rx * 0.6, c + ry * 1.6], fill=70)
    sh = sh.filter(ImageFilter.GaussianBlur(3 * S))
    mask = Image.new('L', (n, n), 0)
    ImageDraw.Draw(mask).ellipse([c - rx, c - ry, c + rx, c + ry], fill=255)
    sh = ImageChops.multiply(sh, mask)
    im.paste((120, 30, 30, 255), (0, 0), sh)
    hl = Image.new('L', (n, n), 0)
    ImageDraw.Draw(hl).ellipse([c - rx * 0.55, c - ry * 0.85, c + rx * 0.2, c - ry * 0.35], fill=90)
    hl = hl.filter(ImageFilter.GaussianBlur(2 * S))
    im.paste((255, 225, 200, 255), (0, 0), ImageChops.multiply(hl, mask))
    # face: a peach mask on the front half
    fm = Image.new('L', (n, n), 0)
    ImageDraw.Draw(fm).ellipse([c + rx * 0.05, c - ry * 0.55, c + rx * 1.05, c + ry * 0.5], fill=255)
    im.paste(face, (0, 0), ImageChops.multiply(fm, mask))
    # eye
    ex, ey = c + rx * 0.52, c - ry * 0.18
    if pose == 'sleep':
        d.arc([ex - 3.2 * S, ey - 2.4 * S, ex + 3.2 * S, ey + 2.4 * S], 20, 160, fill=line, width=int(1.4 * S))
    else:
        er = 3.1 * S
        d.ellipse([ex - er, ey - er * 1.15, ex + er, ey + er * 1.15], fill=(40, 22, 30, 255))
        d.ellipse([ex + er * 0.05, ey - er * 0.85, ex + er * 0.65, ey - er * 0.25], fill=(255, 255, 255, 255))
    # cheek
    d.ellipse([c + rx * 0.2, c + ry * 0.12, c + rx * 0.5, c + ry * 0.34], fill=(250, 140, 130, 255))
    # beak: a short yellow wedge
    bx, by = c + rx * 0.93, c + ry * 0.02
    if not mouth:
        d.polygon([(bx - 1 * S, by - 3.2 * S), (bx + 6.5 * S, by + 0.4 * S), (bx - 1 * S, by + 3.4 * S)],
                  fill=(255, 196, 40, 255), outline=line)
        d.line([(bx - 0.5 * S, by + 0.3 * S), (bx + 5.5 * S, by + 0.4 * S)], fill=(200, 120, 20, 255), width=S)
    else:
        open_beak(d, bx, by, MOUTH_STYLE if mouth is True else mouth, line)
    # wing: a cream-edged oval, raised by flap (-1 down .. 1 up); tucked when diving
    wx, wy = c - rx * 0.15, c + ry * 0.18
    if pose == 'dive':
        wing = [wx - 7 * S, wy - 3 * S, wx + 4 * S, wy + 3.5 * S]
        d.ellipse(wing, fill=dark, outline=line, width=S)
    else:
        wl = Image.new('RGBA', (n, n), (0, 0, 0, 0))
        wd = ImageDraw.Draw(wl)
        wd.ellipse([c - 10 * S, c - 4 * S, c + 2 * S, c + 4 * S], fill=dark, outline=line, width=S)
        wd.ellipse([c - 8 * S, c - 1 * S, c - 1 * S, c + 3 * S], fill=(250, 170, 140, 255))
        ang = 20 + flap * 45
        wl = wl.rotate(ang, center=(c - 1 * S, c), resample=Image.BICUBIC)
        im.alpha_composite(wl, (int(wx - c), int(wy - c - 2 * S)))
    if pose == 'sleep':
        pass
    return im.resize((BIRD, BIRD), Image.LANCZOS)


if len(sys.argv) > 1 and sys.argv[1] == '--mouths':  # a sheet of the mouth styles to choose from
    pf = ImageFont.truetype('/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf', 22)
    names = sys.argv[2].split(',') if len(sys.argv) > 2 else list(MOUTH_STYLES)
    cw, sheet = 230, None
    sheet = Image.new('RGB', (cw * (len(names) + 1), 520), (250, 248, 240))
    sd = ImageDraw.Draw(sheet)
    sd.text((10, 60), 'glide', font=pf, fill=(60, 40, 30))
    sd.text((10, 230), 'dive', font=pf, fill=(60, 40, 30))
    sd.text((10, 390), 'game size\n(x1 and x2)', font=pf, fill=(60, 40, 30))
    sky = Image.new('RGBA', (cw - 20, 150), (140, 205, 228, 255))
    for i, name in enumerate(names):
        x = cw * (i + 1)
        sd.text((x + 10, 8), f'{i + 1}  {name}', font=pf, fill=(60, 40, 30))
        for row, (pose, y) in enumerate((('glide', 40), ('dive', 200))):
            b = bird(pose, 0.0, name).resize((BIRD * 4, BIRD * 4), Image.LANCZOS)
            sheet.paste(Image.new('RGB', b.size, (140, 205, 228)), (x + 15, y))
            sheet.paste(b, (x + 15, y), b)
        small = sky.copy()
        b1 = bird('glide', 0.0, name)
        small.alpha_composite(b1, (16, 50))
        b2 = b1.resize((BIRD * 2, BIRD * 2), Image.NEAREST)
        small.alpha_composite(b2, (90, 30))
        sheet.paste(small.convert('RGB'), (x + 10, 365))
    sheet.save(ROOT / 'test' / 'out' / 'mouth_options.png')
    sys.exit(0)

poses = [bird('glide', 0.0), bird('glide', 1.0), bird('glide', -0.6), bird('dive'), bird('sleep')]
# the same four awake poses with the mouth open (POSE_OPEN + pose)
poses += [bird('glide', 0.0, True), bird('glide', 1.0, True), bird('glide', -0.6, True), bird('dive', mouth=True)]
px = bytearray()
for im in poses:
    raw = im.tobytes()
    for i in range(0, len(raw), 4):  # premultiplied, in memory order B G R A
        r, g, b, a = raw[i:i + 4]
        px += bytes((b * a // 255, g * a // 255, r * a // 255, a))
arr('bird_px', px)
out.append(f'#define BIRD_SIZE {BIRD}\n#define BIRD_POSES {len(poses)}\n'
           '#define POSE_GLIDE 0\n#define POSE_FLAP_UP 1\n#define POSE_FLAP_DOWN 2\n#define POSE_DIVE 3\n#define POSE_SLEEP 4\n#define POSE_OPEN 5\n')
sheet = Image.new('RGBA', (BIRD * len(poses), BIRD), (120, 180, 220, 255))
for i, im in enumerate(poses):
    sheet.alpha_composite(im, (i * BIRD, 0))
sheet.resize((BIRD * len(poses) * 4, BIRD * 4), Image.NEAREST).save(ROOT / 'test' / 'out' / 'bird_sheet.png')

# ---------------------------------------------------------------------------------------------
# paper grain: the remake's whole noise.png (512 x 512), made to tile, stretched to 0..255
#
# Made seamless by Moisan's periodic plus smooth decomposition: the step where the image's
# opposite edges meet is spread out as a smooth, gentle correction over the whole image (the
# solution of a Poisson equation, done with an FFT), so the grain itself is left as it is,
# nothing is mirrored or copied, and the wrap is invisible.

import numpy as np

N = 512
u = np.asarray(Image.open(REMAKE / 'noise.png').convert('L'), dtype=np.float64)
assert u.shape == (N, N)
v = np.zeros_like(u)
v[0, :] += u[-1, :] - u[0, :]
v[-1, :] += u[0, :] - u[-1, :]
v[:, 0] += u[:, -1] - u[:, 0]
v[:, -1] += u[:, 0] - u[:, -1]
q = np.arange(N)
den = 2 * np.cos(2 * np.pi * q[:, None] / N) + 2 * np.cos(2 * np.pi * q[None, :] / N) - 4
den[0, 0] = 1
sf = np.fft.fft2(v) / den
sf[0, 0] = 0
p = u - np.real(np.fft.ifft2(sf))
p -= p.mean()
vals = np.clip(np.round(128 + p * 127 / np.abs(p).max()), 0, 255).astype(int).ravel().tolist()
arr('noise_px', vals)
out.append('#define NOISE_N 512\n')
tile = np.array(vals, dtype=np.uint8).reshape(N, N)
Image.fromarray(np.tile(tile, (2, 2))).save(ROOT / 'test' / 'out' / 'noise_tiled.png')

# ---------------------------------------------------------------------------------------------
# clouds: soft clusters of puffs, flat bottoms

random.seed(7)
CW, CH = 128, 56
cl = bytearray()
for k in range(3):
    n = 4
    im = Image.new('L', (CW * n, CH * n), 0)
    d = ImageDraw.Draw(im)
    base = CH * n * 0.78
    x = CW * n * 0.12
    while x < CW * n * 0.86:
        r = random.uniform(0.18, 0.34) * CH * n
        y = base - r * random.uniform(0.4, 1.0)
        d.ellipse([x - r, y - r, x + r, y + r], fill=255)
        x += r * random.uniform(0.7, 1.1)
    # a flatter, softer underside, then a fade to nothing at every edge of the sprite
    under = Image.new('L', im.size, 0)
    ImageDraw.Draw(under).rectangle([0, base - 6 * n, CW * n, CH * n], fill=255)
    under = under.filter(ImageFilter.GaussianBlur(6 * n))
    im = ImageChops.subtract(im, Image.eval(under, lambda v: v * 3 // 4))
    im = im.filter(ImageFilter.GaussianBlur(3 * n)).resize((CW, CH), Image.LANCZOS)
    win = Image.new('L', (CW, CH), 0)
    ImageDraw.Draw(win).rounded_rectangle([3, 3, CW - 4, CH - 4], radius=16, fill=255)
    win = win.filter(ImageFilter.GaussianBlur(3))
    im = ImageChops.multiply(im, win)
    cl += im.tobytes()
arr('cloud_px', cl)
out.append(f'#define CLOUD_W {CW}\n#define CLOUD_H {CH}\n#define CLOUDS 3\n')

hdr = ROOT / 'src' / 'assets.h'
hdr.write_text('/* Generated by tools/gen_assets.py: do not edit. */\n#ifndef ASSETS_H\n#define ASSETS_H\n'
               + ''.join(out) + '#endif\n')

# ---------------------------------------------------------------------------------------------
# icons: Pip over a striped hill on a sky disc


def icon(size):
    n = size * 8
    im = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    disc = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    g = ImageDraw.Draw(disc)
    for y in range(n):
        t = y / n
        g.line([(0, y), (n, y)], fill=(int(110 + 120 * t), int(190 + 40 * t), int(235 - 10 * t), 255))
    # a hill with three stripes
    hill = Image.new('L', (n, n), 0)
    pts = [(x, n * 0.72 + math.cos(x / n * math.pi * 1.6 + 0.4) * n * 0.1) for x in range(0, n + 1, 4)]
    ImageDraw.Draw(hill).polygon(pts + [(n, n), (0, n)], fill=255)
    stripes = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    sd = ImageDraw.Draw(stripes)
    cols = [(250, 220, 80), (110, 200, 120), (60, 160, 190)]
    w = n / 5
    for i in range(-6, 12):
        x0 = i * w
        sd.polygon([(x0, n), (x0 + w, n), (x0 + w + n * 0.6, 0), (x0 + n * 0.6, 0)], fill=cols[i % 3] + (255,))
    disc.paste(stripes, (0, 0), hill)
    mask = Image.new('L', (n, n), 0)
    ImageDraw.Draw(mask).ellipse([8, 8, n - 8, n - 8], fill=255)
    im.paste(disc, (0, 0), mask)
    b = bird('glide', 0.5).resize((int(n * 0.62), int(n * 0.62)), Image.LANCZOS)
    im.alpha_composite(b, (int(n * 0.2), int(n * 0.12)))
    return im.resize((size, size), Image.LANCZOS)


icon(52).save(ROOT / 'card' / 'icon.png')
icon(40).save(ROOT / 'card' / 'icon-40.png')
icon(52).resize((208, 208), Image.NEAREST).save(ROOT / 'test' / 'out' / 'icon_view.png')
print(f'assets.h: {hdr.stat().st_size} bytes')
