"""Weave the forest-floor / grass borders: in some stretches of the contour the grass
owns the edge (its border goes over the forest floor) instead of the forest floor.
python weave.py preview|apply [share]"""
import json
import os
import sys

import forest
import house
import rme_mcp

HERE = os.path.dirname(os.path.abspath(__file__))
Z = 7
FF = "forest floor"
GRASSES = ("grass", "lush grass", "dark grass")
flip_noise = [forest.Noise(901, 4.5), forest.Noise(902, 2)]


def load_ground():
    data = json.load(open(os.path.join(HERE, "forest_ground.json")))
    ground = {tuple(map(int, k.split(","))): v for k, v in data["ground"].items()}
    for p in house.F:
        ground[p] = "floor"
    for p in house.BED_A | house.BED_B | house.YARD:
        ground[p] = "dark dirt"
    for p in house.PATH_EXT:
        ground[p] = "dirt"
    return ground


def borders(ground, zof):
    pieces = {}
    for (x, y), g in ground.items():
        mine = zof((x, y))
        by_name = {}
        for dx, dy, bit in forest.NEIGHBOURS:
            q = (x + dx, y + dy)
            h = ground.get(q)
            if h is None or h == g or h not in forest.BORDERS or zof(q) <= mine:
                continue
            by_name[h] = by_name.get(h, 0) | bit
        add = []
        for h in sorted(by_name, key=lambda n: forest.Z_ORDER[n]):
            add += [forest.BORDERS[h][e] for e in forest.border_table(by_name[h])]
        if add:
            pieces[(x, y)] = add
    return pieces


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    share = float(sys.argv[2]) if len(sys.argv) > 2 else 0.25
    ground = load_ground()
    forest.Z_ORDER["floor"] = -1

    contour = [p for p, g in ground.items() if g == FF and any(ground.get((p[0] + dx, p[1] + dy)) in GRASSES for dx, dy, _ in forest.NEIGHBOURS)]
    val = {p: forest.warped(flip_noise, p[0], p[1], amount=3.0, angle=0.9) for p in contour}
    cut = sorted(val.values())[int(len(val) * (1 - share))]
    flip = {p for p in contour if val[p] >= cut}
    # Drop tiny flipped bits: a stretch needs at least 3 contour tiles (8-connected).
    seen, keep = set(), set()
    for p in flip:
        if p in seen:
            continue
        comp, stack = [], [p]
        seen.add(p)
        while stack:
            q = stack.pop()
            comp.append(q)
            for dx, dy, _ in forest.NEIGHBOURS:
                n = (q[0] + dx, q[1] + dy)
                if n in flip and n not in seen:
                    seen.add(n)
                    stack.append(n)
        if len(comp) >= 3:
            keep |= set(comp)
    flip = keep

    def z_plain(p):
        return forest.Z_ORDER[ground[p]]

    def z_woven(p):
        return 3190 if p in flip else forest.Z_ORDER[ground[p]]  # below grass, above dark grass

    before, after = borders(ground, z_plain), borders(ground, z_woven)
    changed = sorted(p for p in set(before) | set(after) if before.get(p) != after.get(p))
    print("contour", len(contour), "flipped", len(flip), "tiles changed", len(changed))
    if mode == "preview":
        sym = {"grass": ".", "dirt": "#", "lush grass": ",", FF: "f", "dark grass": "d", "floor": "_", "dark dirt": "x"}
        for y in range(forest.Y0, forest.Y0 + forest.H):
            print("".join("W" if (x, y) in flip else sym[ground[(x, y)]] for x in range(forest.X0, forest.X0 + forest.W)))
        return
    tiles = [{"x": p[0], "y": p[1], "clear": "borders", **({"add": after[p]} if p in after else {})} for p in changed]
    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "weave", "version": "1"}})
    res = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": Z, "tiles": tiles, "description": "entrelacar bordas forest floor x grama"}}, 3)
    print(res["result"]["content"][0]["text"])
    json.dump(sorted(flip), open(os.path.join(HERE, "weave_flip.json"), "w"))


if __name__ == "__main__":
    main()
