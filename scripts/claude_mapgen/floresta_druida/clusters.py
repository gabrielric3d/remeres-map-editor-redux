"""Druid forest vegetation by CLUSTERS (the mapper's method). python clusters.py preview|apply

Hand-composed groups (trees + ferns + mushrooms + stones...) placed at varying distances:
dense woods, medium, open glades; the same cluster never repeats close to itself.
"""
import math
import random
import sys

import forest
import house
import rme_mcp
import trees as old

Z = 7
rng = random.Random(2026)
X0, Y0, W, H = forest.X0, forest.Y0, forest.W, forest.H
CX, CY = forest.CX, forest.CY

SPECIES = {
    "firs": [2700, 2707, 2708, 2708],
    "leafy": [2712, 2711, 7024, 2702, 2701],
    "birch": [20761, 20759, 2701],  # light accent (8313/8314 are leafless)
    "elven": [20761, 20759, 20760],
}
POOL = {
    "f": [21183, 21185, 21187, 21189, 21191, 21184, 21186],  # ferns
    "m": [4178, 4180, 2789, 2791],                              # mushrooms
    "b": [2767, 2767, 2785],                                    # bushes
    "w": [4156, 4157, 4154, 4153, 2740],                        # flowers
    "s": [3612, 3621, 3622],                                    # small stones
    "S": [3608],                                                # stone pile
}

# role: T = main tree of the zone, t = accent tree (birch/elven), others from POOL.
TEMPLATES = {
    "clump": [("T", 0, 0), ("T", 2, 0), ("T", 1, 1), ("T", 0, 2), ("f", 1, 2), ("f", 2, 1), ("m", 3, 1)],
    "pair_ferns": [("T", 0, 0), ("T", 2, 1), ("f", 1, 0), ("f", 1, 1), ("f", 0, 1), ("f", 2, 2)],
    "fern_bed": [("f", 0, 0), ("f", 1, 0), ("f", 2, 1), ("f", 0, 1), ("f", 1, 2), ("m", 2, 0), ("s", 3, 2)],
    "lone_tree": [("T", 0, 0), ("b", 1, 1), ("w", -1, 1), ("w", 2, 0)],
    "rocks": [("S", 0, 0), ("s", 1, 0), ("s", 0, 1), ("f", -1, 0), ("f", 1, 1)],
    "mushrooms": [("m", 0, 0), ("m", 1, 1), ("m", -1, 1), ("f", 0, 2), ("t", 2, -1)],
    "thicket": [("T", 0, 0), ("T", 1, 0), ("T", 0, 1), ("T", 2, 1), ("T", 1, 2), ("b", 3, 0), ("b", 2, 2), ("f", -1, 1)],
    "accent_trio": [("t", 0, 0), ("t", 2, 0), ("t", 1, 2), ("w", 1, 1), ("w", 3, 1)],
    "flowers": [("w", 0, 0), ("w", 1, 0), ("w", 0, 1), ("w", 2, 1), ("w", 1, 2), ("b", 3, 1)],
    "bushes": [("b", 0, 0), ("b", 1, 1), ("T", 2, 0), ("w", 0, 1)],
    "grove_rocks": [("T", 0, 0), ("T", 3, 1), ("f", 1, 1), ("f", 2, 0), ("m", 2, 1), ("s", 1, -1)],
    "dense_band": [("T", 0, 0), ("T", 1, 1), ("T", 3, 1), ("T", 2, 2), ("T", 4, 2), ("f", 1, 2), ("f", 3, 0)],
    "big_and_small": [("T", 0, 0), ("T", 1, 2), ("t", 3, 1), ("b", 2, 0), ("m", 0, 2)],
}
BY_ZONE = {
    "dense": ["clump", "thicket", "dense_band", "grove_rocks", "pair_ferns", "fern_bed", "mushrooms", "big_and_small"],
    "medium": ["lone_tree", "bushes", "accent_trio", "rocks", "mushrooms", "pair_ferns", "big_and_small", "fern_bed"],
    "open": ["flowers", "lone_tree", "rocks", "bushes", "accent_trio"],
    "clearing": ["flowers", "rocks"],
}
SAME_TEMPLATE_GAP = 13  # a cluster never repeats closer than this

density_noise = [forest.Noise(501, 11), forest.Noise(502, 5)]
species_noise = [forest.Noise(601, 13), forest.Noise(602, 6)]
BASE = {"forest floor": 1.0, "dark grass": 0.55, "grass": 0.32, "lush grass": 0.28}


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    old_trees, old_details, ground = old.compute()

    keep = set()
    for (x, y) in list(house.F) + list(house.BED_A | house.BED_B | house.YARD):
        for dx in range(-2, 3):
            for dy in range(-2, 3):
                keep.add((x + dx, y + dy))
    sx, sy = old.STUMP_AT
    stump_zone = {(sx + dx, sy + dy) for dx in range(-2, 5) for dy in range(-2, 5)}
    keep |= stump_zone
    path = {p for p, g in ground.items() if g == "dirt"}
    near_path = {(x + dx, y + dy) for (x, y) in path for dx in (-1, 0, 1) for dy in (-1, 0, 1)} - path

    def in_clearing(x, y):
        return math.hypot(x - CX, (y - CY) * 1.1) < forest.clearing_radius(x, y) - 0.5

    def density(x, y):
        if in_clearing(x, y):
            return 0.0
        n = forest.warped(density_noise, x, y, amount=5.0, angle=1.1)
        g = ground.get((x, y))
        return BASE.get(g, 0.0) * ((0.6 + 1.0 * n) if g == "forest floor" else (0.25 + 1.5 * n))

    def zone(x, y):
        if in_clearing(x, y):
            return "clearing"
        d = density(x, y)
        return "dense" if d > 0.75 else "medium" if d > 0.38 else "open"

    def species(x, y):
        if math.hypot(x - CX, y - CY) < 19:
            return "elven", "birch"
        n = forest.warped(species_noise, x, y, amount=4.0, angle=0.3)
        return ("firs", "birch") if n < 0.42 else ("leafy", "birch") if n < 0.62 else ("leafy", "elven")

    def ok_tree(p):
        return (p in ground and p not in keep and p not in path and p not in near_path and not in_clearing(*p)
                and ground[p] not in ("floor", "dark dirt", "dirt"))

    def ok_detail(p):
        return p in ground and p not in keep and p not in path and ground[p] not in ("floor", "dark dirt", "dirt")

    # Cluster centres: Poisson-like sampling with a radius that follows the density,
    # plus open glades where most candidates are dropped.
    cands = [(x, y) for x in range(X0 + 1, X0 + W - 1) for y in range(Y0 + 1, Y0 + H - 1)]
    rng.shuffle(cands)
    centres = []
    for p in cands:
        if p in keep or p in path:
            continue
        d = density(*p)
        z = zone(*p)
        if z == "clearing":
            if rng.random() > 0.02:
                continue
            r = 7.0
        else:
            if d < 0.2 and rng.random() < 0.85:
                continue  # open glade
            r = 1.7 + (1.0 - min(d, 1.0)) * 8.0 + rng.uniform(-0.4, 1.4)
        if all(math.hypot(p[0] - q[0], p[1] - q[1]) >= max(r, rq) for q, rq, _ in centres):
            centres.append((p, r, z))

    placed = {}  # tile -> list of ids
    trees_at = set()
    used = []  # (template, centre)

    # Canopy: the forest-floor woods, filled with a density that swings with the noise
    # (packed parts, thinner parts), softer at the patch edges; never a full 2x2 block.
    canopy_noise = [forest.Noise(701, 7), forest.Noise(702, 3)]
    for p in sorted(ground, key=lambda q: rng.random()):
        g = ground[p]
        edge = any(ground.get((p[0] + dx, p[1] + dy)) != "forest floor" for dx in (-1, 0, 1) for dy in (-1, 0, 1))
        if g == "forest floor":
            n = forest.warped(canopy_noise, p[0], p[1], amount=4.0, angle=0.8)
            chance = max(0.08, min(0.62, 0.05 + 0.75 * n)) * (0.55 if edge else 1.0)
        elif g in ("grass", "dark grass") and any(ground.get((p[0] + dx, p[1] + dy)) == "forest floor" for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
            chance = 0.1
        else:
            continue
        if rng.random() >= chance or not ok_tree(p) or p in trees_at:
            continue
        x, y = p
        if any(all(q in trees_at for q in ((x + a, y + b), (x + a + 1, y + b), (x + a, y + b + 1), (x + a + 1, y + b + 1)) if q != p)
               for a in (-1, 0) for b in (-1, 0)):
            continue
        main_sp, accent_sp = species(x, y)
        sp = accent_sp if rng.random() < 0.12 else main_sp
        placed[p] = [rng.choice(SPECIES[sp])]
        trees_at.add(p)
    for (c, r, z) in centres:
        options = [t for t in BY_ZONE[z] if all(name != t or math.hypot(c[0] - q[0], c[1] - q[1]) >= SAME_TEMPLATE_GAP for name, q in used)]
        if not options:
            continue
        name = rng.choice(options)
        used.append((name, c))
        main_sp, accent_sp = species(*c)
        fx, fy, swap = rng.choice((1, -1)), rng.choice((1, -1)), rng.random() < 0.5
        for role, dx, dy in TEMPLATES[name]:
            if rng.random() < 0.15:
                continue  # never the exact same shape twice
            if swap:
                dx, dy = dy, dx
            q = (c[0] + dx * fx, c[1] + dy * fy)
            if role in ("T", "t"):
                if not ok_tree(q) or q in trees_at or q in placed:
                    continue
                item = rng.choice(SPECIES[main_sp if role == "T" else accent_sp])
                trees_at.add(q)
            else:
                if not ok_detail(q) or q in placed:
                    continue
                if role in ("f", "m") and z == "clearing":
                    continue
                item = rng.choice(POOL[role])
            placed[q] = [item]

    if mode == "preview":
        sym = {"grass": ".", "dirt": "#", "lush grass": ",", "forest floor": "f", "dark grass": "d", "floor": "_", "dark dirt": "x"}
        for y in range(Y0, Y0 + H):
            print("".join("T" if (x, y) in trees_at else "*" if (x, y) in placed else sym[ground[(x, y)]] for x in range(X0, X0 + W)))
        from collections import Counter
        print("clusters", len(used), Counter(n for n, _ in used))
        print("trees", len(trees_at), "details", len(placed) - len(trees_at))
        return

    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "clusters", "version": "1"}})

    def place(tiles, label):
        res = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": Z, "tiles": tiles, "description": label}}, 3)
        print(label, "->", res["result"]["content"][0]["text"])

    # 1. Take off the old evenly spread trees and details (the stump accents stay).
    remove = {}
    for p, i in old_trees.items():
        remove.setdefault(p, []).append(i)
    for p, ids in old_details.items():
        if p not in stump_zone:
            remove.setdefault(p, []).extend(ids)
    if mode != "reapply":
        place([{"x": p[0], "y": p[1], "remove": ids} for p, ids in sorted(remove.items())], "remove arvores espalhadas")
    # 2. Clusters, one after the other, from the house outwards.
    tiles = []
    done = set()
    for name, c in sorted(used, key=lambda u: math.hypot(u[1][0] - CX, u[1][1] - CY)):
        near = sorted((q for q in placed if q not in done and max(abs(q[0] - c[0]), abs(q[1] - c[1])) <= 5),
                      key=lambda q: math.hypot(q[0] - c[0], q[1] - c[1]))
        for q in near:
            done.add(q)
            tiles.append({"x": q[0], "y": q[1], "add": placed[q]})
    for q in placed:
        if q not in done:
            tiles.append({"x": q[0], "y": q[1], "add": placed[q]})
    place(tiles, "floresta por clusters")


if __name__ == "__main__":
    main()
