"""Structural layout of the 3-floor dragon cave (local coords)."""
import math, json
import numpy as np
from scipy import ndimage
from noise import ValueNoise

W, H = 96, 76          # layout area
MG = 12                # mountain margin around it
OX, OY = 952, 962      # global position of local (0,0)
GW, GH = W + 2*MG, H + 2*MG
MOUNT, FLOOR, LAVA = 0, 1, 2

def G(lx, ly):
    return lx + MG, ly + MG

DIRS = {"N": (0, -1), "S": (0, 1), "E": (1, 0), "W": (-1, 0)}
# name, upper z, lower z, P (local), d = direction the ramp climbs on the lower floor
CONNS = [
    ("A", 8, 9, (14, 38), "N"),
    ("D", 8, 9, (80, 36), "N"),
    ("B", 9, 10, (30, 64), "W"),
    ("C", 9, 10, (66, 64), "E"),
    ("ENT", 7, 8, (46, 5), "N"),
]

def conn_cells(z):
    forced, marks = {}, {}
    for name, zu, zl, (px, py), d in CONNS:
        dx, dy = DIRS[d]
        qx, qy = -dy, dx
        if z == zl:   # lower floor: ramp at P, rock at P+d (+sides), floor at P-d
            forced[(px, py)] = FLOOR
            marks[(px, py)] = ("ramp", name, d)
            forced[(px+dx, py+dy)] = MOUNT
            for s in (-1, 1):
                forced[(px+dx+s*qx, py+dy+s*qy)] = MOUNT
                forced[(px+s*qx, py+s*qy)] = MOUNT
            forced[(px-dx, py-dy)] = FLOOR
            forced[(px-2*dx, py-2*dy)] = FLOOR
            marks[(px-dx, py-dy)] = ("rampbase", name, d)
        if z == zu:   # upper floor: hole at P, arrival floor at P+d, rock on the other sides
            forced[(px, py)] = FLOOR
            marks[(px, py)] = ("hole", name, d)
            forced[(px+dx, py+dy)] = FLOOR
            forced[(px+2*dx, py+2*dy)] = FLOOR
            forced[(px-dx, py-dy)] = MOUNT
            for s in (-1, 1):
                forced[(px+s*qx, py+s*qy)] = MOUNT
                forced[(px-dx+s*qx, py-dy+s*qy)] = MOUNT
    return forced, marks

# ------------------------------------------------------------------ plans
# nodes: name -> (x, y, rx, ry, angle_deg)
# edges: (a, b, width, [via points])  a/b = node name or (x,y)
PLANS = {
 8: dict(
  nodes={
   "NH": (48, 12, 11, 5.5, 0), "NW": (28, 16, 6, 4.5, -25), "W1": (16, 26, 7, 8, 20),
   "W2": (21, 43, 4.5, 6, 15), "SW": (20, 56, 8, 6, -30), "S1": (35, 64, 6, 4, 15),
   "S": (50, 62, 10, 6, -10), "SE": (73, 58, 8, 6, 25), "E2": (86, 44, 4.5, 7, 0),
   "E": (81, 24, 7.5, 7, -20), "NE": (67, 13, 7, 5, 15),
   "p1": (7, 21, 4.5, 3.5, 30), "p2": (59, 6, 4.5, 3, 0), "p3": (32, 70, 5, 3, 10),
   "p4": (91, 50, 3.5, 4.5, 0), "p5": (63, 68, 4.5, 3, -10),
  },
  edges=[("NH", "NW", 5.2), ("NW", "W1", 5.2), ("W1", (14, 36), 3.8, [(15, 32)]),
         ("W1", "W2", 4.2, [(21, 34)]), ("W2", "SW", 5.2), ("SW", "S1", 5.2), ("S1", "S", 5.2),
         ("S", "SE", 5.2), ("SE", "E2", 5.2), ("E2", "E", 5.2, [(87, 34)]),
         ("E", (80, 34), 3.8), ("E", "NE", 5.2), ("NE", "NH", 5.2),
         ("W1", "p1", 4.2), ("NE", "p2", 4.2), ("S1", "p3", 4.2), ("E2", "p4", 4.2),
         ("S", "p5", 4.2), ("NH", (46, 6), 3.8)],
  lava_nodes=[(44, 36, 6, 4, 30), (50, 40, 5, 3.5, -20), (41, 42, 4, 3, 0), (10, 30, 2.8, 2.2, 20),
              (55, 68, 4, 2.2, 0), (79, 63, 3.2, 2.2, 20), (27, 9, 3, 2, -30)],
  lava_edges=[[(47, 31, 3.4), (49, 24, 3.4), (48, 17, 3.4)], [(47, 44, 3.2), (45, 50, 3.4), (48, 57, 3.4)]],
  bridges=[],
 ),
 9: dict(
  nodes={
   "WH": (18, 47, 8, 6.5, 25), "NW": (21, 18, 8.5, 6, -20), "WS": (36, 36, 5, 3.5, -35),
   "SW": (22, 61, 8, 5, -15), "NE": (73, 16, 8.5, 6, 20), "EH": (80, 48, 7.5, 6.5, -25),
   "ES": (62, 47, 5, 3.5, 30), "SE": (75, 62, 8, 5, 15),
   "p1": (11, 11, 4.5, 3.5, 20), "p2": (86, 10, 4.5, 3.5, -20), "p3": (90, 56, 3.5, 4.5, 0), "p4": (8, 54, 4, 4, 0),
  },
  edges=[("NW", "WH", 5.2, [(22, 30), (21, 38)]), ((14, 40), "WH", 3.8), ("WH", "WS", 4.2, [(28, 41)]),
         ("WH", "SW", 5.2), ("SW", (28, 64), 3.8), ("NW", "NE", 4.2, [(32, 20), (40, 22), (48, 22), (56, 21), (64, 18)]),
         ("NE", "EH", 5.2, [(85, 28), (86, 39)]), ((80, 39), "EH", 3.8), ("EH", "ES", 4.2, [(70, 48)]),
         ("EH", "SE", 5.2), ("SE", (68, 64), 3.8),
         ("NW", "p1", 4.2), ("NE", "p2", 4.2), ("EH", "p3", 4.2), ("WH", "p4", 4.2)],
  lava_nodes=[(46, 65, 5, 3.5, 20), (51, 68, 4, 3, -20), (47, 3, 3, 2.5, 0), (12, 22, 2.6, 2, 30), (82, 21, 2.6, 2, -30)],
  lava_edges=[[(47, -12, 3.6), (45, 6, 4.2), (48, 22, 4.4), (41, 33, 4.2), (46, 40, 4.6),
               (56, 46, 4.4), (52, 56, 4.4), (48, 64, 4.8), (50, 76, 4.4), (51, 88, 4.2)]],
  bridges=[[(38, 22, 4.6), (48, 22, 4.8), (58, 21, 4.6)]],
 ),
 10: dict(
  nodes={
   "BR": (37, 62, 6.5, 5.5, -20), "W": (21, 45, 7.5, 8, 15), "NW": (24, 18, 8.5, 6.5, -15),
   "N": (48, 10, 11, 5, 0), "NEST": (48, 23, 4.5, 5.5, 0), "NE": (72, 18, 8.5, 6.5, 15),
   "E": (76, 44, 7.5, 8, -15), "CR": (59, 62, 6.5, 5.5, 20),
   "p1": (10, 41, 4, 4, 0), "p2": (87, 37, 4, 4, 0), "p3": (48, 3, 4.5, 2.5, 0),
   "p4": (12, 14, 4.5, 3.5, 20), "p5": (84, 12, 4.5, 3.5, -20),
  },
  edges=[("BR", "W", 5.2, [(30, 54)]), ((32, 64), "BR", 3.8), ("W", "NW", 5.2, [(20, 32)]),
         ("NW", "N", 5.2, [(35, 13)]), ("N", "NEST", 5.2), ("N", "NE", 5.2, [(61, 12)]),
         ("NE", "E", 5.2, [(76, 31)]), ("E", "CR", 5.2, [(68, 55)]), ("CR", (64, 64), 3.8),
         ("W", "p1", 4.2), ("E", "p2", 4.2), ("N", "p3", 4.2), ("NW", "p4", 4.2), ("NE", "p5", 4.2)],
  lava_nodes=[(42, 34, 8, 5, 25), (52, 32, 7, 4.5, -15), (47, 40, 7, 5, 10), (58, 38, 5, 3.5, 30), (37, 40, 4, 3, 0), (48, 29, 5, 2.5, 0)],
  lava_edges=[[(47, 44, 4.2), (44, 53, 4), (45, 60, 4), (51, 66, 4.2), (49, 74, 3.8), (48, 88, 3.6)],
              [(38, 38, 3.2), (31, 41, 3.2), (27, 43, 3)], [(60, 40, 3.2), (68, 42, 3.2), (71, 44, 3)]],
  bridges=[],
  islands=[(45, 35, 1.8), (54, 36, 1.5)],
 ),
}

# ------------------------------------------------------------------ fields
def poly_field(xs, ys, pts):
    best = np.full(xs.shape, -1e9)
    for (ax, ay, aw), (bx, by, bw) in zip(pts, pts[1:]):
        vx, vy = bx-ax, by-ay
        L2 = vx*vx + vy*vy or 1e-9
        t = np.clip(((xs-ax)*vx + (ys-ay)*vy)/L2, 0, 1)
        d = np.hypot(xs-(ax+t*vx), ys-(ay+t*vy))
        w = aw + (bw-aw)*t
        best = np.maximum(best, w/2.0 - d)
    return best

def ell_field(xs, ys, cx, cy, rx, ry, ang):
    a = math.radians(ang)
    ca, sa = math.cos(a), math.sin(a)
    u = (xs-cx)*ca + (ys-cy)*sa
    v = -(xs-cx)*sa + (ys-cy)*ca
    dn = np.sqrt((u/rx)**2 + (v/ry)**2)
    return (1 - dn) * min(rx, ry)

_NC = {}
def noise_grid(seed, scale, oct=3):
    key = (seed, scale, oct)
    if key not in _NC:
        n = ValueNoise(seed)
        g = np.zeros((GH, GW))
        for gy in range(GH):
            for gx in range(GW):
                g[gy, gx] = n.fbm(gx, gy, scale, oct)
        _NC[key] = g / (np.abs(g).max() + 1e-9)
    return _NC[key]

def conn_points(z):
    pts = []
    for name, zu, zl, (px, py), d in CONNS:
        if z in (zu, zl):
            pts.append((px, py))
    return pts

def build_floor(z, seed, warp_amp=1.9, edge_amp=1.0):
    P = PLANS[z]
    ys, xs = np.mgrid[0:GH, 0:GW].astype(float)
    xs -= MG; ys -= MG
    # warp falloff near connection points (they are fixed)
    fall = np.ones_like(xs)
    for (px, py) in conn_points(z):
        d = np.hypot(xs-px, ys-py)
        fall = np.minimum(fall, np.clip((d-3)/6.0, 0, 1))
    wx = noise_grid(seed+1, 14.0, 2) * warp_amp * fall
    wy = noise_grid(seed+2, 14.0, 2) * warp_amp * fall
    X, Y = xs + wx, ys + wy
    nodes = P["nodes"]
    f = np.full((GH, GW), -1e9)
    for (cx, cy, rx, ry, ang) in nodes.values():
        f = np.maximum(f, ell_field(X, Y, cx, cy, rx, ry, ang))
    for e in P["edges"]:
        a, b, w = e[0], e[1], e[2]
        via = e[3] if len(e) > 3 else []
        pa = nodes[a][:2] if isinstance(a, str) else a
        pb = nodes[b][:2] if isinstance(b, str) else b
        pts = [(pa[0], pa[1], w)] + [(vx, vy, w) for (vx, vy) in via] + [(pb[0], pb[1], w)]
        f = np.maximum(f, poly_field(X, Y, pts))
    en = noise_grid(seed+3, 3.2, 2) * edge_amp + noise_grid(seed+4, 7.0, 2) * 0.6
    floor = ((f + en) > 0) | (f > 0.8)
    # lava
    lf = np.full((GH, GW), -1e9)
    for (cx, cy, rx, ry, ang) in P["lava_nodes"]:
        lf = np.maximum(lf, ell_field(X, Y, cx, cy, rx, ry, ang))
    for r in P["lava_edges"]:
        lf = np.maximum(lf, poly_field(X, Y, r))
    ln = noise_grid(seed+5, 3.0, 2) * 0.9 + noise_grid(seed+6, 6.0, 2) * 0.6
    lava = ((lf + ln) > 0) | (lf > 1.0)
    for (ix, iy, ir) in P.get("islands", []):
        lava &= ~((np.hypot(X-ix, Y-iy) + noise_grid(seed+8, 2.5, 2)*0.6) < ir)
    bf = np.full((GH, GW), -1e9)
    for b in P["bridges"]:
        bf = np.maximum(bf, poly_field(X, Y, b))
    bridge = ((bf + noise_grid(seed+9, 3.0, 2)*0.4) > 0) | (bf > 0.9)
    grid = np.full((GH, GW), MOUNT, dtype=np.int8)
    grid[floor] = FLOOR
    grid[lava] = LAVA
    grid[bridge & (floor | lava)] = FLOOR
    edge = np.zeros_like(grid, dtype=bool)
    edge[:3, :] = edge[-3:, :] = True; edge[:, :3] = edge[:, -3:] = True
    grid[edge & (grid == FLOOR)] = MOUNT
    return grid

def apply_forced(grid, z):
    forced, marks = conn_cells(z)
    for (lx, ly), t in forced.items():
        gx, gy = G(lx, ly)
        grid[gy, gx] = t
    return marks

def block3_ok(mask):
    ok = np.zeros_like(mask)
    H_, W_ = mask.shape
    b = np.ones((H_-2, W_-2), dtype=bool)
    for dy in range(3):
        for dx in range(3):
            b &= mask[dy:H_-2+dy, dx:W_-2+dx]
    for dy in range(3):
        for dx in range(3):
            ok[dy:H_-2+dy, dx:W_-2+dx] |= b
    return ok

def forced_ex(z):
    forced, _ = conn_cells(z)
    ex = np.zeros((GH, GW), dtype=bool)
    for k in forced:
        gx, gy = G(*k)
        ex[max(0, gy-1):gy+2, max(0, gx-1):gx+2] = True
    return ex

def block_ok(mask):
    ok = np.zeros_like(mask)
    b = mask[:-1, :-1] & mask[1:, :-1] & mask[:-1, 1:] & mask[1:, 1:]
    ok[:-1, :-1] |= b; ok[1:, :-1] |= b; ok[:-1, 1:] |= b; ok[1:, 1:] |= b
    return ok

def cleanup(grid, z, iters=3):
    forced, _ = conn_cells(z)
    fz = {G(*k): v for k, v in forced.items()}
    ex = forced_ex(z)
    for _ in range(iters):
        fl = grid == FLOOR
        grid[fl & ~block3_ok(fl) & ~ex] = MOUNT
        nf = grid != FLOOR
        grid[(grid == MOUNT) & ~block_ok(nf)] = FLOOR
        lv = grid == LAVA
        okl = block_ok(lv)
        for gy, gx in zip(*np.where(lv & ~okl)):
            nb = grid[max(0, gy-1):gy+2, max(0, gx-1):gx+2]
            grid[gy, gx] = FLOOR if (nb == FLOOR).sum() >= (nb == MOUNT).sum() else MOUNT
        for (gx, gy), v in fz.items():
            grid[gy, gx] = v
    lab, n = ndimage.label(grid == FLOOR)
    if n > 1:
        keyl = set(int(lab[gy, gx]) for (gx, gy), v in fz.items() if v == FLOOR)
        sizes = ndimage.sum(np.ones_like(lab), lab, range(1, n+1))
        keep = 1 + int(np.argmax(sizes))
        for i in range(1, n+1):
            if i != keep and i not in keyl:
                grid[lab == i] = MOUNT
    for (gx, gy), v in fz.items():
        grid[gy, gx] = v
    return grid

def diag_fix(grid):
    changed = True
    while changed:
        changed = False
        for gy in range(GH-1):
            for gx in range(GW-1):
                a, b, c, d = grid[gy, gx], grid[gy, gx+1], grid[gy+1, gx], grid[gy+1, gx+1]
                if a == FLOOR and d == FLOOR and b != FLOOR and c != FLOOR:
                    grid[gy, gx+1] = FLOOR; changed = True
                elif b == FLOOR and c == FLOOR and a != FLOOR and d != FLOOR:
                    grid[gy, gx] = FLOOR; changed = True
    return grid

def reach_report(grid, z):
    lab, n = ndimage.label(grid == FLOOR)
    pts = []
    for name, zu, zl, (px, py), d in CONNS:
        if z == zl:
            dx, dy = DIRS[d]
            pts.append((name + "-base", px-dx, py-dy))
        if z == zu:
            dx, dy = DIRS[d]
            pts.append((name + "-arr", px+dx, py+dy))
    labs = {nm: int(lab[G(x, y)[1], G(x, y)[0]]) for nm, x, y in pts}
    return labs

SEEDS = {8: 811, 9: 922, 10: 1033}

def build(z, seed=None):
    g = build_floor(z, SEEDS[z] if seed is None else seed)
    apply_forced(g, z)
    g = cleanup(g, z)
    g = diag_fix(g)
    g = cleanup(g, z, 2)
    marks = apply_forced(g, z)
    return g, marks

def render(grid, marks, path, scale=5):
    from PIL import Image, ImageDraw
    col = {MOUNT: (120, 40, 30), FLOOR: (190, 150, 100), LAVA: (255, 110, 0)}
    img = Image.new("RGB", (GW*scale, GH*scale))
    d = ImageDraw.Draw(img)
    for gy in range(GH):
        for gx in range(GW):
            d.rectangle([gx*scale, gy*scale, gx*scale+scale-1, gy*scale+scale-1], fill=col[int(grid[gy, gx])])
    for (lx, ly), m in marks.items():
        gx, gy = G(lx, ly)
        c = {"ramp": (0, 255, 0), "hole": (0, 0, 0), "rampbase": (0, 160, 255)}[m[0]]
        d.rectangle([gx*scale, gy*scale, gx*scale+scale-1, gy*scale+scale-1], fill=c)
    for i in range(0, W+1, 10):
        d.line([(MG+i)*scale, (MG-1)*scale, (MG+i)*scale, MG*scale-1], fill=(255, 255, 255))
    for i in range(0, H+1, 10):
        d.line([(MG-1)*scale, (MG+i)*scale, MG*scale-1, (MG+i)*scale], fill=(255, 255, 255))
    img.save(path)

if __name__ == "__main__":
    from PIL import Image
    imgs = []
    for z in (8, 9, 10):
        g, marks = build(z)
        np.save(f"grid{z}.npy", g)
        json.dump({f"{k[0]},{k[1]}": v for k, v in marks.items()}, open(f"marks{z}.json", "w"))
        render(g, marks, f"shots/layout{z}.png")
        imgs.append(Image.open(f"shots/layout{z}.png"))
        print(z, "floor", int((g == FLOOR).sum()), "lava", int((g == LAVA).sum()), reach_report(g, z))
    wtot = sum(i.width for i in imgs) + 20
    out = Image.new("RGB", (wtot, imgs[0].height), (255, 255, 255))
    x = 0
    for i in imgs:
        out.paste(i, (x, 0)); x += i.width + 10
    out.save("shots/layout_all.png")
