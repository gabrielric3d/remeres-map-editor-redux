"""Druid forest: trees and details (stage 3). python trees.py preview|apply"""
import json
import math
import os
import random
import sys

import forest
import house
import rme_mcp

HERE = os.path.dirname(os.path.abspath(__file__))
Z = 7
rng = random.Random(77)
X0, Y0, W, H = forest.X0, forest.Y0, forest.W, forest.H
CX, CY = forest.CX, forest.CY

SPECIES = {
    "firs": [2700, 2700, 2707, 2708, 2708],
    "leafy": [2712, 2711, 7024, 2702, 2701],
    "elven": [20761, 20759, 20761, 20760],
    "birch": [8313, 8313, 8314],
}
GOLDEN = 2706
FERNS = [21183, 21185, 21187, 21189, 21191, 21184, 21186]
MUSHROOMS = [4178, 4180, 2789, 2791]
BUSHES = [2767, 2767, 2785]
FLOWERS = [4156, 4157, 4154, 4153, 2740]
STONES = [3608, 3612, 3621, 3622]
STUMP_AT = (1011, 1015)
STUMP = {(0, 0): [20899, 20895], (1, 0): [20900, 20896], (2, 0): [20901], (0, 1): [20902, 20897], (1, 1): [20904, 20898],
         (0, 2): [20903], (1, 2): [20905], (2, 2): [20906]}


def compute():
    data = json.load(open(os.path.join(HERE, "forest_ground.json")))
    ground = {tuple(map(int, k.split(","))): v for k, v in data["ground"].items()}
    for p in house.F:
        ground[p] = "floor"
    for p in house.BED_A | house.BED_B | house.YARD:
        ground[p] = "dark dirt"
    for p in house.PATH_EXT:
        ground[p] = "dirt"

    # Keep-out: house (+2), garden, stump (+2), path (+1).
    keep = set()
    for (x, y) in list(house.F) + list(house.BED_A | house.BED_B | house.YARD):
        for dx in range(-2, 3):
            for dy in range(-2, 3):
                keep.add((x + dx, y + dy))
    for dx in range(-2, 5):
        for dy in range(-2, 5):
            keep.add((STUMP_AT[0] + dx, STUMP_AT[1] + dy))
    path = {p for p, g in ground.items() if g == "dirt"}
    near_path = {(x + dx, y + dy) for (x, y) in path for dx in (-1, 0, 1) for dy in (-1, 0, 1)} - path

    def clearing(x, y):
        return math.hypot(x - CX, (y - CY) * 1.1) < forest.clearing_radius(x, y) - 0.5

    # Groves: connected forest-floor areas, each with one species.
    groves, seen = [], set()
    for p, g in ground.items():
        if g != "forest floor" or p in seen:
            continue
        comp, stack = [], [p]
        seen.add(p)
        while stack:
            q = stack.pop()
            comp.append(q)
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (q[0] + dx, q[1] + dy)
                if n not in seen and ground.get(n) == "forest floor":
                    seen.add(n)
                    stack.append(n)
        groves.append(comp)
    grove_of = {}
    for i, comp in enumerate(groves):
        cxg = sum(p[0] for p in comp) / len(comp)
        cyg = sum(p[1] for p in comp) / len(comp)
        d = math.hypot(cxg - CX, cyg - CY)
        kind = "elven" if d < 24 else rng.choice(["firs", "firs", "leafy", "leafy", "birch"] if len(comp) < 60 else ["firs", "leafy"])
        for p in comp:
            grove_of[p] = kind

    trees = {}

    def free_for_tree(p):
        if p in keep or p in path or p in near_path or ground.get(p) in (None, "floor", "dark dirt", "dirt"):
            return False
        return all((p[0] + dx, p[1] + dy) not in trees for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))

    cells = sorted(ground, key=lambda p: rng.random())
    for p in cells:
        g = ground[p]
        if clearing(*p):
            continue
        if g == "forest floor":
            chance = 0.62
            kind = grove_of[p]
        else:
            # Grass: soft fringes around groves, sparse elsewhere.
            near_grove = any(ground.get((p[0] + dx, p[1] + dy)) == "forest floor" for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2))
            chance = 0.22 if near_grove else 0.05
            kind = next((grove_of[(p[0] + dx, p[1] + dy)] for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2) if (p[0] + dx, p[1] + dy) in grove_of), rng.choice(["firs", "leafy"]))
        if rng.random() < chance and free_for_tree(p):
            trees[p] = rng.choice(SPECIES[kind])
    # A few trees on the clearing rim (never in the middle), and the golden trio by the stump.
    for p in cells:
        d = math.hypot(p[0] - CX, (p[1] - CY) * 1.1)
        if forest.clearing_radius(*p) - 2.5 < d < forest.clearing_radius(*p) - 0.5 and rng.random() < 0.12 and free_for_tree(p):
            trees[p] = rng.choice(SPECIES["elven"])
    for p in ((1016, 1012), (1009, 1019), (1015, 1019)):
        if free_for_tree(p):
            trees[p] = GOLDEN

    # Details, in small clusters.
    details = {}

    def cluster(center, pool, n, radius, allowed):
        for _ in range(n):
            q = (center[0] + rng.randint(-radius, radius), center[1] + rng.randint(-radius, radius))
            if q in trees or q in details or q in keep or q in path or not allowed(q):
                continue
            details[q] = [rng.choice(pool)]

    floor_tiles = [p for p, g in ground.items() if g == "forest floor"]
    for p in rng.sample(floor_tiles, len(floor_tiles) // 9):
        cluster(p, FERNS, rng.randint(2, 4), 1, lambda q: ground.get(q) == "forest floor")
    for p in rng.sample(floor_tiles, len(floor_tiles) // 40):
        cluster(p, MUSHROOMS, rng.randint(2, 3), 1, lambda q: ground.get(q) in ("forest floor", "grass"))
    edge = sorted(near_path, key=lambda q: rng.random())
    for p in edge[: len(edge) // 5]:
        cluster(p, BUSHES + FLOWERS[:2], rng.randint(1, 3), 1, lambda q: q in near_path and ground.get(q) == "grass")
    rim = [p for p in ground if ground[p] in ("grass", "lush grass") and forest.clearing_radius(*p) - 4 < math.hypot(p[0] - CX, (p[1] - CY) * 1.1) < forest.clearing_radius(*p)]
    for p in rng.sample(rim, len(rim) // 12):
        cluster(p, FLOWERS, rng.randint(3, 5), 1, lambda q: ground.get(q) in ("grass", "lush grass"))
    grass_tiles = [p for p, g in ground.items() if g == "grass" and not clearing(*p)]
    for p in rng.sample(grass_tiles, len(grass_tiles) // 60):
        cluster(p, STONES + [3608], rng.randint(1, 3), 1, lambda q: ground.get(q) == "grass")
    # Around the stump: mushrooms and a couple of stones, denser near it.
    for dx, dy in ((-1, 1), (3, 0), (3, 2), (1, 3), (-1, 3), (4, 1)):
        q = (STUMP_AT[0] + dx, STUMP_AT[1] + dy)
        details[q] = [rng.choice(MUSHROOMS + [3608])]
    return trees, details, ground


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    trees, details, ground = compute()
    if mode == "preview":
        sym = {"grass": ".", "dirt": "#", "lush grass": "l", "forest floor": "f", "dark grass": "d", "floor": "_", "dark dirt": "x"}
        for y in range(Y0, Y0 + H):
            print("".join("T" if (x, y) in trees else "*" if (x, y) in details else sym[ground[(x, y)]] for x in range(X0, X0 + W)))
        print("trees", len(trees), "details", len(details))
        return

    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "trees", "version": "1"}})

    def place(tiles, label):
        r = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": Z, "tiles": tiles, "description": label}}, 3)
        print(label, "->", r["result"]["content"][0]["text"])

    stump = [{"x": STUMP_AT[0] + dx, "y": STUMP_AT[1] + dy, "add": ids} for (dx, dy), ids in STUMP.items()]
    place(stump, "tronco sagrado")
    # Trees grove by grove, from the house outwards.
    order = sorted(trees, key=lambda p: (math.hypot(p[0] - CX, p[1] - CY) // 6, rng.random()))
    place([{"x": p[0], "y": p[1], "add": [trees[p]]} for p in order], "arvores da floresta")
    order = sorted(details, key=lambda p: math.hypot(p[0] - CX, p[1] - CY))
    place([{"x": p[0], "y": p[1], "add": ids} for p, ids in ((p, details[p]) for p in order)], "detalhes da floresta")


if __name__ == "__main__":
    main()
