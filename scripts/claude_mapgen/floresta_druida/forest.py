"""Druid forest: terrain generator (stage 1). python forest.py preview|apply"""
import json
import math
import os
import random
import sys

import rme_mcp

HERE = os.path.dirname(os.path.abspath(__file__))
Z = 7
X0, Y0, W, H = 992, 992, 64, 64
CX, CY = 1024, 1023  # house centre
random.seed(7)

GROUNDS = json.load(open(os.path.join(HERE, "grounds.json")))
Z_ORDER = {"dirt": 400, "dark dirt": 1200, "dark grass": 3100, "grass": 3200, "lush grass": 3400, "forest floor": 3600}
EDGE_NAMES = ["n", "e", "s", "w", "cnw", "cne", "csw", "cse", "dnw", "dne", "dse", "dsw"]
BORDERS = {  # outer border of each ground, piece ids by edge name
    "grass": dict(zip(EDGE_NAMES, [4542, 4543, 4544, 4545, 4546, 4547, 4548, 4549, 4550, 4551, 4553, 4552])),
    "dark grass": dict(zip(EDGE_NAMES, [4784, 4785, 4786, 4787, 4788, 4789, 4790, 4791, 4792, 4793, 4795, 4794])),
    "lush grass": dict(zip(EDGE_NAMES, [12707, 12708, 12706, 12709, 12716, 12717, 12715, 12714, 12712, 12713, 12710, 12711])),
    "forest floor": dict(zip(EDGE_NAMES, [21113, 21124, 21123, 21114, 21115, 21116, 21117, 21118, 21119, 21120, 21122, 21121])),
    "dark dirt": dict(zip(EDGE_NAMES, [11165, 11167, 11166, 11168, 11172, 11171, 11170, 11169, 11175, 11176, 11173, 11174])),
}


# ---- the editor's border table (ground_brush_arrays.cpp) -------------------
N, S, E, Wd, NW, NE, SW, SE = 2, 64, 16, 8, 1, 4, 32, 128


def border_table(bits):
    hn, hs, he, hw = bool(bits & N), bool(bits & S), bool(bits & E), bool(bits & Wd)
    out = []
    used = set()
    if hn and hw and not hs and not he:
        out.append("dnw"); used |= {"n", "w"}
    if hn and he and not hs and not hw:
        out.append("dne"); used |= {"n", "e"}
    if hs and hw and not hn and not he:
        out.append("dsw"); used |= {"s", "w"}
    if hs and he and not hn and not hw:
        out.append("dse"); used |= {"s", "e"}
    for flag, name in ((hn, "n"), (hs, "s"), (he, "e"), (hw, "w")):
        if flag and name not in used:
            out.append(name)
    if bits & NW and not hn and not hw:
        out.append("cnw")
    if bits & NE and not hn and not he:
        out.append("cne")
    if bits & SW and not hs and not hw:
        out.append("csw")
    if bits & SE and not hs and not he:
        out.append("cse")
    return out


NEIGHBOURS = [(-1, -1, NW), (0, -1, N), (1, -1, NE), (-1, 0, Wd), (1, 0, E), (-1, 1, SW), (0, 1, S), (1, 1, SE)]


# ---- value noise -------------------------------------------------------------
class Noise:
    def __init__(self, seed, cell):
        self.r = random.Random(seed)
        self.cell = cell
        self.grid = {}

    def v(self, ix, iy):
        if (ix, iy) not in self.grid:
            self.grid[(ix, iy)] = self.r.random()
        return self.grid[(ix, iy)]

    def at(self, x, y):
        fx, fy = x / self.cell, y / self.cell
        ix, iy = math.floor(fx), math.floor(fy)
        tx, ty = fx - ix, fy - iy
        sx, sy = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a = self.v(ix, iy) + (self.v(ix + 1, iy) - self.v(ix, iy)) * sx
        b = self.v(ix, iy + 1) + (self.v(ix + 1, iy + 1) - self.v(ix, iy + 1)) * sx
        return a + (b - a) * sy


def fbm(noises, x, y):
    total, weight = 0.0, 0.0
    for i, n in enumerate(noises):
        w = 0.5 ** i
        total += n.at(x, y) * w
        weight += w
    return total / weight


forest_noise = [Noise(11, 9), Noise(12, 4), Noise(13, 2)]
warp_noise = [Noise(41, 7), Noise(42, 3)]
warp_noise2 = [Noise(43, 7), Noise(44, 3)]


def warped(noises, x, y, amount=7.0, angle=0.55):
    # Rotate and warp the lookup so blobs do not follow the tile grid.
    u = x * math.cos(angle) - y * math.sin(angle)
    v = x * math.sin(angle) + y * math.cos(angle)
    u += (fbm(warp_noise, x, y) - 0.5) * amount * 2
    v += (fbm(warp_noise2, x, y) - 0.5) * amount * 2
    return fbm(noises, u, v)
tone_noise = [Noise(21, 5), Noise(22, 2.5)]
edge_noise = [Noise(31, 3), Noise(32, 1.5)]


def clearing_radius(x, y):
    angle = math.atan2(y - CY, x - CX)
    return 11.5 + 2.8 * math.sin(angle * 3 + 0.7) + 1.6 * math.sin(angle * 5 + 2.1) + (fbm(edge_noise, x, y) - 0.5) * 3


def path_centre(y):
    return CX + 2.6 * math.sin((y - CY) / 5.5) + 1.4 * math.sin((y - CY) / 2.3 + 1.0)


def path_width(y):
    return 2.2 + 1.1 * (0.5 + 0.5 * math.sin(y / 3.7)) + (1.5 if y < CY + 9 else 0)


def pick(name):
    items = GROUNDS[name]
    total = sum(max(c, 0) for _, c in items) or 1
    r = random.uniform(0, total)
    for item, chance in items:
        r -= max(chance, 0)
        if r <= 0 and chance > 0:
            return item
    return items[0][0]


def blob(rng, cx, cy, discs, rmin, rmax, spread):
    """Union of discs around (cx, cy): a rounded, irregular patch."""
    tiles = set()
    x, y = cx, cy
    for _ in range(discs):
        r = rng.uniform(rmin, rmax)
        for ty in range(int(y - r - 1), int(y + r + 2)):
            for tx in range(int(x - r - 1), int(x + r + 2)):
                if (tx - x) ** 2 + (ty - y) ** 2 <= r * r:
                    tiles.add((tx, ty))
        x += rng.uniform(-spread, spread)
        y += rng.uniform(-spread, spread)
    return tiles


def break_straight_edges(ground, rng, longest=3):
    """No side of a patch runs straight for more than `longest` tiles: add a two-tile
    tooth outwards or bite two tiles inwards in the middle of any longer run."""
    patchy = lambda g: g not in (None, "grass", "dirt")
    for _ in range(12):
        changed = False
        # (dx, dy): where the outside is; runs go along the other axis.
        for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            along = (0, 1) if dx else (1, 0)
            for p, g in list(ground.items()):
                if not patchy(g) or ground.get((p[0] + dx, p[1] + dy)) == g:
                    continue
                prev = (p[0] - along[0], p[1] - along[1])
                if ground.get(prev) == g and ground.get((prev[0] + dx, prev[1] + dy)) != g:
                    continue  # not the start of a run
                run = [p]
                while True:
                    q = (run[-1][0] + along[0], run[-1][1] + along[1])
                    if ground.get(q) == g and ground.get((q[0] + dx, q[1] + dy)) != g:
                        run.append(q)
                    else:
                        break
                if len(run) <= longest:
                    continue
                i = rng.randint(1, len(run) - 2)
                a, b = run[i], run[min(i + 1, len(run) - 1)]
                if rng.random() < 0.5:
                    for t in (a, b):
                        out = (t[0] + dx, t[1] + dy)
                        if ground.get(out) == "grass":
                            ground[out] = g
                else:
                    for t in (a, b):
                        ground[t] = "grass"
                changed = True
        if not changed:
            break


def generate():
    rng = random.Random(2024)
    ground = {}
    inside = lambda p: X0 <= p[0] < X0 + W and Y0 <= p[1] < Y0 + H
    for y in range(Y0, Y0 + H):
        for x in range(X0, X0 + W):
            ground[(x, y)] = "grass"

    def clearing(x, y):
        return math.hypot(x - CX, (y - CY) * 1.1) < clearing_radius(x, y)

    def path(x, y, margin=0.0):
        return y > CY + 3 and abs(x - path_centre(y)) < path_width(y) / 2 + margin

    # Forest floor: big groves (random walks of discs) outside the clearing and the path.
    groves = 0
    while groves < 14:
        cx, cy = rng.uniform(X0, X0 + W), rng.uniform(Y0, Y0 + H)
        if math.hypot(cx - CX, cy - CY) < 17 or path(cx, cy, 5):
            continue
        for p in blob(rng, cx, cy, rng.randint(6, 12), 1.8, 4.2, 3.2):
            if inside(p) and not clearing(*p) and not path(*p, 2.5) and math.hypot(p[0] - CX, (p[1] - CY) * 1.1) > clearing_radius(*p) + 2:
                ground[p] = "forest floor"
        groves += 1
    # Dark grass satellites in the open forest, lush grass tone patches in the clearing.
    for kind, count, discs, rmin, rmax, where in (("dark grass", 8, (2, 4), 1.0, 2.2, "out"), ("lush grass", 6, (3, 5), 1.2, 2.6, "clearing")):
        made = 0
        tries = 0
        while made < count and tries < 500:
            tries += 1
            cx, cy = rng.uniform(X0 + 3, X0 + W - 3), rng.uniform(Y0 + 3, Y0 + H - 3)
            d = math.hypot(cx - CX, (cy - CY) * 1.1)
            if where == "clearing" and not (5.5 < d < clearing_radius(cx, cy) - 1.5):
                continue
            if where == "out" and (d < clearing_radius(cx, cy) + 3 or ground[(int(cx), int(cy))] != "grass"):
                continue
            for p in blob(rng, cx, cy, rng.randint(*discs), rmin, rmax, 1.6):
                if inside(p) and ground[p] == "grass" and not path(*p, 1.5) and (where == "out" or clearing(*p)):
                    ground[p] = kind
            made += 1
    # The path.
    for (x, y) in list(ground):
        if path(x, y):
            ground[(x, y)] = "dirt"
    # Serrated edges: bite into the patches and grow little teeth out of them.
    for _ in range(2):
        for (x, y), g in list(ground.items()):
            around = [ground.get((x + dx, y + dy)) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))]
            if g == "grass":
                patch = [a for a in around if a not in (None, "grass", "dirt")]
                if len(patch) >= 2 and rng.random() < 0.25:
                    ground[(x, y)] = patch[0]
            elif g != "dirt" and around.count(g) <= 3 and rng.random() < 0.18:
                ground[(x, y)] = "grass"
    # No single tiles, one-tile spurs or tiny leftovers (they come out as squares).
    for _ in range(3):
        for (x, y), g in list(ground.items()):
            if g in ("grass", "dirt"):
                continue
            same = sum(1 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)) if ground.get((x + dx, y + dy)) == g)
            if same < 2:
                ground[(x, y)] = "grass"
    break_straight_edges(ground, rng)
    seen = set()
    for p, g in list(ground.items()):
        if g in ("grass", "dirt") or p in seen:
            continue
        comp, stack = [], [p]
        seen.add(p)
        while stack:
            q = stack.pop()
            comp.append(q)
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (q[0] + dx, q[1] + dy)
                if n not in seen and ground.get(n) == g:
                    seen.add(n)
                    stack.append(n)
        if len(comp) < 10:
            for q in comp:
                ground[q] = "grass"
    return ground


def borders_for(ground):
    pieces = {}
    for (x, y), g in ground.items():
        mine = Z_ORDER[g]
        higher = sorted({ground.get((x + dx, y + dy)) for dx, dy, _ in NEIGHBOURS} - {None, g}, key=lambda n: Z_ORDER[n])
        add = []
        for h in higher:
            if Z_ORDER[h] <= mine or h not in BORDERS:
                continue
            bits = 0
            for dx, dy, bit in NEIGHBOURS:
                if ground.get((x + dx, y + dy)) == h:
                    bits |= bit
            add += [BORDERS[h][e] for e in border_table(bits)]
        if add:
            pieces[(x, y)] = add
    return pieces


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    ground = generate()
    pieces = borders_for(ground)
    symbol = {"grass": ".", "dirt": "#", "lush grass": "l", "forest floor": "F", "dark grass": "d"}
    counts = {}
    for g in ground.values():
        counts[g] = counts.get(g, 0) + 1
    print(counts, "tiles with border pieces:", len(pieces))
    if mode == "preview":
        for y in range(Y0, Y0 + H):
            print("".join(symbol[ground[(x, y)]] for x in range(X0, X0 + W)))
        return
    order = sorted(ground, key=lambda p: math.hypot(p[0] - CX, p[1] - CY) + random.random() * 3)
    tiles = [{"x": x, "y": y, "ground": pick(ground[(x, y)]), **({"add": pieces[(x, y)]} if (x, y) in pieces else {})} for (x, y) in order]
    json.dump({"ground": {f"{x},{y}": g for (x, y), g in ground.items()}}, open(os.path.join(HERE, "forest_ground.json"), "w"))
    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "forest", "version": "1"}})
    reply = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": Z, "tiles": tiles, "description": "chao da floresta"}}, 3)
    print(reply["result"]["content"][0]["text"])


if __name__ == "__main__":
    main()
