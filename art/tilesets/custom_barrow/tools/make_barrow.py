"""Outer Barrow custom tiles (original art, CC0): barrow_maps.txt -> outer_barrow.png + outer_barrow.json.

Objects: letter maps -> per-stone top-left shading -> seams -> 1px outline.
Ground: seeded wrap-around stamps.  Transitions: corner-bitmask blend with a 1px cast shadow.
Mound: 3x3 autotile cut from one shaded mound.  Wall: footprint from N/E/S/W connections + texture.
"""
import json, math, os, random, re
from PIL import Image

T = 16
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAL = {}
for l in open(os.path.join(HERE, '..', '..', 'palette.gpl')):
    m = re.match(r'\s*(\d+)\s+(\d+)\s+(\d+)\s+(\S+)', l)
    if m: PAL[m.group(4)] = tuple(map(int, m.groups()[:3])) + (255,)
CLEAR = (0, 0, 0, 0)
CORNERS = {0: 'c', 3: 'n', 12: 's', 5: 'w', 10: 'e', 7: 'nw', 11: 'ne', 13: 'sw', 14: 'se',
           1: 'inner_nw', 2: 'inner_ne', 4: 'inner_sw', 8: 'inner_se', 6: 'diag_ne_sw', 9: 'diag_nw_se'}


def parse(path):
    cfg = {'ramp': {}, 'ground': {}, 'transition': [], 'maps': {}, 'shadow': []}
    cur = None
    for line in open(path):
        line = line.rstrip('\n')
        if line.startswith('['):
            name, size = line[1:-1].split(); w, h = map(int, size.split('x'))
            cur = cfg['maps'][name] = {'w': w, 'h': h, 'rows': []}; continue
        if cur is not None:
            if line.strip() == '' or line.startswith('#'): cur = None
            else: cur['rows'].append(line); continue
        if not line or line.startswith('#'): continue
        k, *rest = line.split()
        if k == 'ramp': cfg['ramp'][rest[0]] = [PAL[c] for c in rest[1:]]
        elif k == 'ground':
            g = {'stamps': []}
            for tok in rest[1:]:
                a, b = tok.split('=', 1)
                if a == 'stamp':
                    cols, n, pat = b.split(':'); g['stamps'].append(([PAL[c] for c in cols.split(',')], int(n), pat.split('/')))
                else: g[a] = b
            cfg['ground'][rest[0]] = g
        elif k == 'transition':
            cfg['transition'].append((rest[0], dict(t.split('=') for t in rest[1:])))
        elif k in ('mound', 'wall'): cfg[k] = dict(t.split('=') for t in rest)
        elif k == 'shadow': cfg['shadow'].append((rest[0], PAL[rest[1]]))
    return cfg


CFG = parse(os.path.join(HERE, 'barrow_maps.txt'))
STONE, WOOD, GRASS = CFG['ramp']['stone'], CFG['ramp']['wood'], CFG['ramp']['grass']
OUT, DARK = PAL['outline'], PAL['gstone_d']
MARK = {'x': DARK, 'K': OUT, '*': PAL['fol1'], ':': PAL['wstone1'], '^': PAL['fol1m']}
R = range(-2, 3)


def shade(solid, x, y):
    """3 = top-left corner, 2 = lit edge, 1 = interior, 0 = edge facing bottom-right (same rule as icons)."""
    nx = sum(dx for dx in R for dy in R if (dx or dy) and (x + dx, y + dy) not in solid)
    ny = sum(dy for dx in R for dy in R if (dx or dy) and (x + dx, y + dy) not in solid)
    edge = any((x + dx, y + dy) not in solid for dx in (-1, 0, 1) for dy in (-1, 0, 1))
    lit = -(nx + ny) if edge else 0
    if edge and lit == 0 and (nx or ny): lit = -ny or -nx
    tl = (x - 1, y) not in solid and (x, y - 1) not in solid
    return 3 if (tl and lit > 0) else 2 if lit > 0 else 0 if lit < 0 else 1


def paint_pieces(grid, W, H, ramp=STONE, front_ramp=None, seam=None, front_seam=None):
    """grid: {(x,y): char}. Letters = pieces (upper = front face, one step darker). Returns {(x,y): rgba}."""
    px = {}
    pieces = {}
    for p, c in grid.items():
        if c.isalpha() and c != 'x' and c != 'K': pieces.setdefault((c.lower(), c.isupper()), set()).add(p)
    allp = {p for p, c in grid.items() if c.isalpha() and c not in 'xK'}
    for (ch, front), pts in pieces.items():
        solid = pts                       # top and front faces shade (and seam) separately
        for (x, y) in pts:
            lv = shade(solid, x, y)
            rp = front_ramp if (front and front_ramp) else ramp
            idx = {0: 1, 1: 2, 2: 3, 3: 3}[lv] - (1 if front and not front_ramp else 0)
            # interior speckle so big faces aren't flat
            if lv == 1 and (x * 7 + y * 13 + ord(ch)) % 17 == 0: idx -= 1
            col = rp[max(idx, 0)]
            for q in ((x + 1, y), (x, y + 1)):      # seam on the bottom/right side of a stone
                if q in allp and q not in solid: col = (front_seam if front else seam) or DARK
            px[(x, y)] = col
    for p, c in grid.items():
        if c in MARK: px[p] = MARK[c]
        elif c in '1234': px[p] = WOOD[int(c) - 1]
    return px


def outline(img, exempt=()):
    W, H = img.size; a = img.load()
    solid = {(x, y) for x in range(W) for y in range(H) if a[x, y][3] and (x, y) not in exempt}
    # enclosed transparent holes -> crevice colour
    seen = set(); stack = [(x, y) for x in range(W) for y in (0, H - 1)] + [(x, y) for y in range(H) for x in (0, W - 1)]
    while stack:
        p = stack.pop()
        if p in seen or not (0 <= p[0] < W and 0 <= p[1] < H) or a[p][3]: continue
        seen.add(p); x, y = p; stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    for x in range(W):
        for y in range(H):
            if not a[x, y][3] and (x, y) not in seen: a[x, y] = DARK; solid.add((x, y))
    for (x, y) in list(solid):
        for q in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= q[0] < W and 0 <= q[1] < H and not a[q][3]: a[q] = OUT
    return img


def ground_shadow(img, col):
    """Soft 1-2px shadow under the base, shifted right (light from the top-left)."""
    W, H = img.size; a = img.load(); snap = img.copy().load()
    cols = [x for x in range(W) if any(a[x, y][3] for y in range(H))]
    if not cols: return img
    x0, x1 = min(cols), max(cols)
    for x in range(x0, x1 + 1):
        yb = max((y for y in range(H) if snap[x, y][3]), default=None)
        if yb is None: continue
        mid = x0 + (x1 - x0) * 0.2 < x < x1 - (x1 - x0) * 0.15
        for dy, ok_ in ((1, True), (2, mid)):
            X, Y = x + 1, yb + dy
            if ok_ and X < W and Y < H and not a[X, Y][3]: a[X, Y] = col
    return img


def render_map(m, name=''):
    W, H = m['w'] * T, m['h'] * T
    rows = m['rows'][-H:]; rows = ['.' * W] * (H - len(rows)) + rows   # bottom-aligned
    grid = {(x, y): c for y, r in enumerate(rows) for x, c in enumerate(r[:W]) if c != '.'}
    img = Image.new('RGBA', (W, H), CLEAR); a = img.load()
    for p, col in paint_pieces(grid, W, H).items(): a[p] = col
    img = outline(img, exempt={p for p, c in grid.items() if c == '^'})
    for pre, col in CFG['shadow']:
        if name.startswith(pre): ground_shadow(img, col)
    return img


def ground(name):
    g = CFG['ground'][name]; rnd = random.Random(int(g['seed']))
    img = Image.new('RGBA', (T, T), PAL[g['base']]); a = img.load()
    for cols, n, pat in g['stamps']:
        for _ in range(n):
            x0, y0 = rnd.randrange(T), rnd.randrange(T)
            for dy, row in enumerate(pat):
                for dx, ch in enumerate(row):
                    if ch.isdigit(): a[(x0 + dx) % T, (y0 + dy) % T] = cols[int(ch)]
    return img


def value_noise(seed, cell):
    """Smooth periodic (16px) value noise in [-1, 1]."""
    rnd = random.Random(seed); n = T // cell
    lat = [[rnd.uniform(-1, 1) for _ in range(n)] for _ in range(n)]
    def f(x, y):
        gx, gy = (x % T) / cell, (y % T) / cell; i, j = int(gx), int(gy); u, v = gx - i, gy - j
        u, v = u * u * (3 - 2 * u), v * v * (3 - 2 * v)
        a, b = lat[j % n][i % n], lat[j % n][(i + 1) % n]; c, d = lat[(j + 1) % n][i % n], lat[(j + 1) % n][(i + 1) % n]
        return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v
    return f


def hsh(x, y, s):
    return ((x % T) * 73856093 ^ (y % T) * 19349663 ^ s * 83492791) % 1000 / 1000


def transition(inner, outer, shadow, mask, seed=1):
    gi, go = ground(inner).load(), ground(outer).load()
    shades = [PAL[c] for c in shadow.split(',')]
    n1, n2, n3 = value_noise(seed, 8), value_noise(seed + 1, 4), value_noise(seed + 2, 2)
    c = [(mask >> i) & 1 for i in range(4)]   # TL TR BL BR
    out = set()
    for y in range(T):
        for x in range(T):
            u, v = (x + 0.5) / T, (y + 0.5) / T
            f = c[0] * (1 - u) * (1 - v) + c[1] * u * (1 - v) + c[2] * (1 - u) * v + c[3] * u * v
            if f > 0.5 + 0.16 * n1(x, y) + 0.09 * n2(x, y) + 0.05 * n3(x, y): out.add((x, y))
    for (x, y) in list(out):   # 1px blade spikes from the outer terrain
        if (x, y + 1) not in out and y + 1 < T and hsh(x, y, seed) < 0.3: out.add((x, y + 1))
    img = Image.new('RGBA', (T, T)); a = img.load()
    for y in range(T):
        for x in range(T):
            if (x, y) in out: a[x, y] = go[x, y]; continue
            a[x, y] = gi[x, y]
            near = (x, y - 1) in out or (x - 1, y) in out
            near2 = (x, y - 2) in out and n2(x, y) > 0.1
            if (near and hsh(x, y, seed + 5) > 0.12) or near2:
                a[x, y] = shades[int(hsh(x, y, seed + 9) * len(shades))]
    return img


def mound(W, H, face, radius):
    """Domed mound: crown light top/left, darker flank bottom/right (sparse clusters), south face, base shadow."""
    mp = CFG['mound']; crown, flank, dens = int(mp['crown']), int(mp['flank']), float(mp['density'])
    cx0, cy0, cx1, cy1 = 1, 1, W - 2, H - 4          # room below for the ground shadow
    def inside(x, y):
        if not (cx0 <= x <= cx1 and cy0 <= y <= cy1): return False
        qx = min(x - cx0, cx1 - x); qy = min(y - cy0, cy1 - y)
        return not (qx < radius and qy < radius and (radius - qx - 0.5) ** 2 + (radius - qy - 0.5) ** 2 > radius ** 2)
    def dist(x, y, dx, dy, lim):
        return next((k for k in range(1, lim + 1) if not inside(x + dx * k, y + dy * k)), lim + 1)
    top = ground(mp['top']).load()
    def cluster(x, y, s, p):   # sparse 2-3px clusters: seed pixels + their right/down neighbour
        return any(hsh(x - dx, y - dy, s) < p for dx, dy in ((0, 0), (1, 0), (0, 1)))
    img = Image.new('RGBA', (W, H), CLEAR); a = img.load()
    for y in range(H):
        for x in range(W):
            if not inside(x, y): continue
            below = dist(x, y, 0, 1, face + 1)
            dn, dw, de = dist(x, y, 0, -1, crown), dist(x, y, -1, 0, crown), dist(x, y, 1, 0, flank)
            if below <= face:                                          # south face
                col = GRASS[1] if below >= face - 1 else GRASS[0]
                if below < face - 1 and cluster(x, y, 31, 0.10): col = GRASS[1]
            elif dn == 1 or dw == 1: col = GRASS[3]                    # lit rim
            elif de == 1: col = GRASS[0]                               # shaded east rim
            else:
                col = top[x % T, y % T]
                lit = max(crown + 1 - dn, crown + 1 - dw, 0) / crown           # 0..1 near the N/W rim
                ds = dist(x, y, 0, 1, face + flank + 1) - face
                dark = max(flank + 1 - de, flank + 1 - ds, 0) / flank  # near the E/S flank
                if dark > 0 and cluster(x, y, 17, dens * dark): col = GRASS[1] if dark < 0.7 else GRASS[0]
                elif lit > 0 and cluster(x, y, 23, dens * lit): col = GRASS[3]
            a[x, y] = col
    outline(img)
    for x in range(W):                                                # soft base shadow
        yb = max((y for y in range(H) if a[x, y][3]), default=None)
        if yb is None or yb < cy1: continue
        for dy in (1, 2):
            X = x + 1
            if X < W and yb + dy < H and not a[X, yb + dy][3] and (dy == 1 or hsh(x, 0, 7) < 0.6): a[X, yb + dy] = PAL[mp['shadow']]
    return img


def wall(conn):
    p = CFG['wall']; ty, dp, fc, arm = (int(p[k]) for k in ('top_y', 'depth', 'face', 'arm'))
    ax0 = (T - arm) // 2; ax1 = ax0 + arm
    def foot(x, y):   # on an extended canvas: neighbouring tiles continue connected arms
        tx, ty_, lx, ly = x // T, y // T, x % T, y % T
        if (tx, ty_) == (0, 0):
            core = ax0 <= lx < ax1 and ty <= ly < ty + dp
            if core: return True
            if 'N' in conn and ax0 <= lx < ax1 and ly < ty + dp: return True
            if 'S' in conn and ax0 <= lx < ax1 and ly >= ty: return True
            if 'W' in conn and lx < ax1 and ty <= ly < ty + dp: return True
            if 'E' in conn and lx >= ax0 and ty <= ly < ty + dp: return True
            return False
        if (tx, ty_) == (-1, 0) and 'W' in conn: return ty <= ly < ty + dp
        if (tx, ty_) == (1, 0) and 'E' in conn: return ty <= ly < ty + dp
        if (tx, ty_) == (0, -1) and 'N' in conn: return ax0 <= lx < ax1
        if (tx, ty_) == (0, 1) and 'S' in conn: return ax0 <= lx < ax1
        return False
    top, face = CFG['maps']['wall_top']['rows'], CFG['maps']['wall_face']['rows']
    grid = {}
    for y in range(-T, 2 * T):
        for x in range(-T, 2 * T):
            if foot(x, y): grid[(x, y)] = top[y % T][x % T]
            elif any(foot(x, y - k) for k in range(1, fc + 1)):
                grid[(x, y)] = face[y % T][x % T].upper()
    px = paint_pieces(grid, 3 * T, 3 * T, ramp=CFG['ramp']['walltop'], front_ramp=CFG['ramp']['wallface'],
                      seam=PAL['gstone0'], front_seam=DARK)
    # wall-level light on top of the per-stone shading: lit top/left rim, shaded right side and base
    WT, WF = CFG['ramp']['walltop'], CFG['ramp']['wallface']
    for (x, y), ch in grid.items():
        fr = ch.isupper(); L, U, Rr, D = (x - 1, y) not in grid, (x, y - 1) not in grid, (x + 1, y) not in grid, (x, y + 1) not in grid
        if not fr:
            if U or L: px[(x, y)] = WT[3]
            elif Rr: px[(x, y)] = WT[0]
        else:
            if D or Rr: px[(x, y)] = WF[0]
            elif L: px[(x, y)] = WF[2]
    big = Image.new('RGBA', (3 * T, 3 * T), CLEAR); a = big.load()
    for (x, y), col in px.items(): a[x + T, y + T] = col
    outline(big)
    return big.crop((T, T, 2 * T, 2 * T))


def build():
    items = []
    for g in ('heath', 'heath2', 'heath3', 'grass', 'path', 'rocky'):
        items.append((g if g != 'grass' else 'grass', ground(g)))
    for name, t in CFG['transition']:
        for m, n in sorted(CORNERS.items(), key=lambda kv: list(CORNERS).index(kv[0])):
            items.append((f'{name}_{n}', transition(t['inner'], t['outer'], t['shadow'], m, int(t.get('seed', 1)))))
    mp = CFG['mound']; face, rad = int(mp['face']), int(mp['radius'])
    big = mound(3 * T, 3 * T, face, rad)
    for j, rn in enumerate(('n', '', 's')):
        for i, cn in enumerate(('w', '', 'e')):
            items.append(('mound_' + ((rn + cn) or 'c'), big.crop((i * T, j * T, (i + 1) * T, (j + 1) * T))))
    ent = mound(3 * T, 2 * T, face, rad)
    ent.alpha_composite(render_map(CFG['maps']['entrance_overlay']))
    items.append(('mound_entrance_3x2', ent))
    for n in ('cairn_small', 'cairn_medium', 'cairn_large', 'stone_tall', 'stone_medium', 'stone_short', 'stone_fallen',
              'stone_ring', 'stone_ring_3x3', 'grave_slab', 'grave_stone', 'grave_wood'):
        m = CFG['maps'][n]; items.append((f"{n}_{m['w']}x{m['h']}" if (m['w'], m['h']) != (1, 1) and not n.endswith('x3') else n, render_map(m, n)))
    for n, c in (('wall_h', 'EW'), ('wall_v', 'NS'), ('wall_corner_nw', 'ES'), ('wall_corner_ne', 'WS'),
                 ('wall_corner_sw', 'NE'), ('wall_corner_se', 'NW'), ('wall_t_s', 'EWS'), ('wall_t_n', 'EWN'),
                 ('wall_end_w', 'E'), ('wall_end_e', 'W'), ('wall_end_n', 'S'), ('wall_post', '')):
        items.append((n, wall(c)))
    x = y = shelf = 0; rects = {}; SW = 16
    for nm, im in items:
        w, h = im.width // T, im.height // T
        if x + w > SW: x, y, shelf = 0, y + shelf, 0
        rects[nm] = (x, y, w, h); x += w; shelf = max(shelf, h)
    sheet = Image.new('RGBA', (SW * T, (y + shelf) * T), CLEAR)
    for nm, im in items: sheet.alpha_composite(im, (rects[nm][0] * T, rects[nm][1] * T))
    sheet.save(os.path.join(HERE, 'outer_barrow.png'))
    json.dump({'tile_size': T, 'license': 'CC0 - original Emerald art (drawn from barrow_maps.txt)',
               'tiles': {nm: {'x': x * T, 'y': y * T, 'w': w * T, 'h': h * T} for nm, (x, y, w, h) in rects.items()}},
              open(os.path.join(HERE, 'outer_barrow.json'), 'w'), indent=1)
    print(len(items), 'entries', sheet.size)


if __name__ == '__main__':
    build()
