"""Grass tufts in clusters. python tufts.py preview|apply

Each cluster is an irregular blob (random size, stretch and angle, denser in the middle)
of one tuft family with a few tufts of a second one mixed in. Clusters follow the
transitions (forest edge, path, house walls, rocks) and thin out in the open glades.
"""
import math
import random
import re
import sys

import forest
import house
import rme_mcp
import trees as old

Z = 7
rng = random.Random(4242)
X0, Y0, W, H = forest.X0, forest.Y0, forest.W, forest.H
CX, CY = forest.CX, forest.CY

FAMILIES = {
    "fresh": list(range(44960, 44968)),
    "dark": list(range(14845, 14853)),
    "sprigs": [6216, 6217, 6218, 6219, 20177, 20178, 20263, 20264],
    "tall": [10914, 10915, 10916],
}
# (main family, accent family, size range)
KINDS = {
    "fresh_patch": ("fresh", "dark", (5, 9)),
    "fresh_small": ("fresh", "sprigs", (3, 5)),
    "dark_patch": ("dark", "fresh", (4, 8)),
    "sprig_scatter": ("sprigs", "fresh", (3, 5)),
    "tall_edge": ("tall", "fresh", (3, 5)),
}
SAME_KIND_GAP = 7
noise = [forest.Noise(801, 9), forest.Noise(802, 4)]


def occupied_tiles():
    occ = set()
    for qx in (X0, X0 + 32):
        for qy in (Y0, Y0 + 32):
            res = rme_mcp.rpc("tools/call", {"name": "read_region", "arguments": {"x": qx, "y": qy, "z": Z, "width": 32, "height": 32, "raw": True}}, 3)
            text = res["result"]["content"][0]["text"]
            borders = {int(m) for m in re.findall(r"^\s+(\d+) \"[^\"]*\" = border", text, re.M)}
            stacks = text.split("Stacks")[1].split("Item legend")[0] if "Stacks" in text else ""
            for m in re.finditer(r"^(\d+),(\d+): ([\d ]+)$", stacks, re.M):
                ids = [int(i) for i in m.group(3).split()]
                if any(i not in borders for i in ids):
                    occ.add((int(m.group(1)), int(m.group(2))))
    return occ


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "tufts", "version": "1"}})
    _, _, ground = old.compute()
    occ = occupied_tiles()
    ring = set()
    for (x, y) in house.F:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                ring.add((x + dx, y + dy))
    blocked = occ | ring | house.BED_A | house.BED_B | house.YARD

    def ok(p):
        return p in ground and p not in blocked and ground[p] in ("grass", "lush grass", "dark grass", "forest floor")

    def near(p, pred, r=1):
        return any(pred((p[0] + dx, p[1] + dy)) for dx in range(-r, r + 1) for dy in range(-r, r + 1))

    def attraction(p):
        # Transitions pull tufts in: forest edge, path, house walls, rocks and trees on grass.
        a = 0.0
        g = ground.get(p)
        if g != "forest floor" and near(p, lambda q: ground.get(q) == "forest floor", 2):
            a += 0.5
        if near(p, lambda q: ground.get(q) == "dirt", 1):
            a += 0.45
        if near(p, lambda q: q in ring, 2):
            a += 0.5
        if near(p, lambda q: q in occ, 1):
            a += 0.25
        if g == "forest floor":
            a -= 0.35
        return a + forest.warped(noise, p[0], p[1], amount=5.0, angle=2.0) - 0.45

    cands = [p for p in ground if ok(p)]
    rng.shuffle(cands)
    centres = []
    for p in cands:
        a = attraction(p)
        if a < 0.05 and rng.random() < 0.8:
            continue  # open ground stays mostly clean
        r = max(2.2, 7.5 - a * 5.0) + rng.uniform(-0.5, 1.5)
        if all(math.hypot(p[0] - q[0], p[1] - q[1]) >= max(r, rq) for q, rq in centres):
            centres.append((p, r))

    placed = {}
    used = []
    for c, _ in centres:
        edge_forest = near(c, lambda q: ground.get(q) == "forest floor", 2) and ground.get(c) != "forest floor"
        kinds = [k for k in KINDS if (k != "tall_edge" or edge_forest)
                 and all(k != n or math.hypot(c[0] - q[0], c[1] - q[1]) >= SAME_KIND_GAP for n, q in used)]
        if not kinds:
            continue
        kind = rng.choice(kinds)
        used.append((kind, c))
        main_f, accent_f, (lo, hi) = KINDS[kind]
        n = rng.randint(lo, hi)
        angle, stretch = rng.uniform(0, math.pi), rng.uniform(1.0, 2.2)
        tries = 0
        got = []
        while len(got) < n and tries < n * 8:
            tries += 1
            # Denser in the middle: radius from a squared uniform, stretched along a random axis.
            rad = (rng.random() ** 1.6) * (1.2 + n / 4)
            t = rng.uniform(0, 2 * math.pi)
            u, v = math.cos(t) * rad * stretch, math.sin(t) * rad
            q = (round(c[0] + u * math.cos(angle) - v * math.sin(angle)), round(c[1] + u * math.sin(angle) + v * math.cos(angle)))
            if not ok(q) or q in placed:
                continue
            fam = accent_f if (got and rng.random() < 0.22) else main_f
            placed[q] = rng.choice(FAMILIES[fam])
            got.append(q)

    if mode == "preview":
        sym = {"grass": ".", "dirt": "#", "lush grass": ",", "forest floor": "f", "dark grass": "d", "floor": "_", "dark dirt": "x"}
        for y in range(Y0, Y0 + H):
            print("".join("o" if (x, y) in occ else "~" if (x, y) in placed else sym[ground[(x, y)]] for x in range(X0, X0 + W)))
        from collections import Counter
        print("clusters", len(used), Counter(k for k, _ in used), "tufts", len(placed))
        return

    tiles, done = [], set()
    for kind, c in sorted(used, key=lambda u: math.hypot(u[1][0] - CX, u[1][1] - CY)):
        for q in sorted((q for q in placed if q not in done and max(abs(q[0] - c[0]), abs(q[1] - c[1])) <= 5),
                        key=lambda q: math.hypot(q[0] - c[0], q[1] - c[1])):
            done.add(q)
            tiles.append({"x": q[0], "y": q[1], "add": [placed[q]]})
    tiles += [{"x": q[0], "y": q[1], "add": [placed[q]]} for q in placed if q not in done]
    res = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": Z, "tiles": tiles, "description": "tufos de grama por clusters"}}, 3)
    print(res["result"]["content"][0]["text"])


if __name__ == "__main__":
    main()
