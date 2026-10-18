"""Druid house (stage 2). python house.py preview|apply"""
import json
import os
import random
import sys

import forest
import rme_mcp

HERE = os.path.dirname(os.path.abspath(__file__))
Z = 7
rng = random.Random(5)

# ---- footprint (floor tiles) --------------------------------------------------
F = set()
for x in range(1020, 1028):
    for y in range(1018, 1025):
        F.add((x, y))
F -= {(1026, 1018), (1027, 1018)}  # stepped north-east corner
F |= {(x, y) for x in range(1021, 1024) for y in range(1016, 1018)}  # alcove for the bed
F |= {(x, y) for x in range(1016, 1020) for y in range(1020, 1025)}  # herb annex

WOOD = {"h": [6140, 6140, 6140, 6136], "v": [6142, 6142, 6142, 6133], "corner": 6141, "pole": 6139}
DOOR = {(1024, 1025): 6195}
WINDOWS = {(1021, 1025): 6178, (1026, 1025): 6178, (1028, 1022): 6179, (1016, 1022): 6179, (1022, 1016): 6178}

# ---- ground changes around the house ------------------------------------------
BED_A = {(1031, 1018), (1032, 1018), (1030, 1019), (1031, 1019), (1032, 1019), (1033, 1019), (1031, 1020), (1032, 1020), (1033, 1020)}
BED_B = {(1030, 1022), (1031, 1022), (1032, 1022), (1031, 1023), (1032, 1023), (1033, 1023), (1032, 1024), (1033, 1024)}
YARD = {(1029, 1026), (1030, 1025), (1031, 1025), (1030, 1026), (1031, 1026), (1032, 1026), (1033, 1026), (1029, 1027), (1030, 1027),
        (1031, 1027), (1032, 1027), (1033, 1027), (1030, 1028), (1031, 1028), (1032, 1028), (1033, 1028), (1031, 1029), (1032, 1029),
        (1033, 1029), (1032, 1030)}
PATH_EXT = {(1023, 1026), (1024, 1026), (1025, 1026)}
CAULDRON = [(0, 0, 13101), (1, 0, 13102), (2, 0, 13103), (0, 1, 13104), (1, 1, 13105), (2, 1, 13106), (0, 2, 13109), (1, 2, 13108), (2, 2, 13107), (2, 3, 13010)]
CAULDRON_AT = (1030, 1026)


def walls():
    comps = {}
    for (x, y) in F:
        if (x, y - 1) not in F:
            comps.setdefault((x, y), set()).add("N")
        if (x - 1, y) not in F:
            comps.setdefault((x, y), set()).add("W")
        if (x + 1, y) not in F:
            comps.setdefault((x + 1, y), set()).add("W")
        if (x, y + 1) not in F:
            comps.setdefault((x, y + 1), set()).add("N")
    pieces = {}
    for t, c in comps.items():
        if c == {"N", "W"}:
            pieces[t] = WOOD["corner"]
        elif c == {"N"}:
            pieces[t] = rng.choice(WOOD["h"])
        else:
            pieces[t] = rng.choice(WOOD["v"])
    # Poles where a horizontal wall from the west and a vertical wall from the north meet.
    xs = [p[0] for p in comps] + [p[0] for p in F]
    ys = [p[1] for p in comps] + [p[1] for p in F]
    for x in range(min(xs), max(xs) + 2):
        for y in range(min(ys), max(ys) + 2):
            if (x, y) in comps:
                continue
            if "N" in comps.get((x - 1, y), ()) and "W" in comps.get((x, y - 1), ()):
                pieces[(x, y)] = WOOD["pole"]
    pieces.update({t: i for t, i in DOOR.items()})
    pieces.update({t: i for t, i in WINDOWS.items()})
    return pieces


INTERIOR = {
    (1022, 1017): [18027], (1022, 1018): [18028],  # simple bed (two pieces)
    (1023, 1017): [15445],
    (1021, 1019): [13797],
    (1024, 1019): [1718], (1025, 1019): [1720],
    (1025, 1021): [26354], (1025, 1022): [40887], (1026, 1021): [40889],
    (1027, 1023): [13827], (1027, 1020): [13832],
    (1021, 1024): [15445], (1026, 1024): [7677],
    (1025, 1018): [12822],
    (1017, 1021): [7665], (1018, 1021): [15441], (1017, 1023): [10803], (1019, 1023): [7670], (1018, 1024): [13802],
    (1017, 1020): [4202], (1016, 1023): [4205],
}

EXTERIOR = {
    (1022, 1026): [17742], (1026, 1026): [17741],  # glowing mushrooms by the door
    (1034, 1021): [8422],  # druid statue between the beds
    (1015, 1025): [9466],  # rain barrel
    (1017, 1026): [2767], (1019, 1026): [2768, 4156], (1015, 1021): [2767], (1015, 1023): [2786],
    (1021, 1027): [4154], (1027, 1027): [4157],
    (1029, 1017): [2767], (1029, 1024): [2786], (1034, 1018): [2768],
}
HERBS = [2760, 2799, 2802, 2804, 2760, 2799]


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    data = json.load(open(os.path.join(HERE, "forest_ground.json")))
    ground = {tuple(map(int, k.split(","))): v for k, v in data["ground"].items()}
    for p in F:
        ground[p] = "floor"
    for p in BED_A | BED_B | YARD:
        ground[p] = "dark dirt"
    for p in PATH_EXT:
        ground[p] = "dirt"
    wall_pieces = walls()

    if mode == "preview":
        sym = {"grass": ".", "dirt": "#", "lush grass": "l", "forest floor": "F", "dark grass": "d", "floor": "_", "dark dirt": "x"}
        for y in range(1012, 1034):
            row = ""
            for x in range(1008, 1040):
                if (x, y) in wall_pieces:
                    i = wall_pieces[(x, y)]
                    row += "D" if i in DOOR.values() else "o" if i in WINDOWS.values() else "+" if i == WOOD["corner"] else "*" if i == WOOD["pole"] else "-" if i in WOOD["h"] else "|"
                else:
                    row += sym[ground[(x, y)]]
            print(row)
        return

    # Borders: recompute around the house with floor out of the game.
    forest.Z_ORDER["floor"] = -1
    area = [(x, y) for x in range(1008, 1042) for y in range(1010, 1036)]
    sub = {p: ground[p] for p in area}
    pieces = forest.borders_for({p: g for p, g in ground.items()})
    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "house", "version": "1"}})

    def place(tiles, label):
        r = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": Z, "tiles": tiles, "description": label}}, 3)
        print(label, "->", r["result"]["content"][0]["text"])

    # 1. Grounds: floor, garden beds, yard, path to the door, and fresh borders around them.
    tiles = []
    for p in sorted(area, key=lambda q: (abs(q[0] - 1024) + abs(q[1] - 1022))):
        g = sub[p]
        if g == "floor":
            tiles.append({"x": p[0], "y": p[1], "clear": "all", "ground": 405})
            continue
        spec = {"x": p[0], "y": p[1], "clear": "borders"}
        if p in BED_A | BED_B | YARD or p in PATH_EXT:
            spec["ground"] = forest.pick(g)
        add = [i for i in pieces.get(p, [])]
        if add:
            spec["add"] = add
        tiles.append(spec)
    place(tiles, "chao da casa e do jardim")
    # 2. Walls, door, windows (walls go round clockwise from the north-west).
    order = sorted(wall_pieces, key=lambda t: (t[1] > 1020, t[0] if t[1] <= 1020 else -t[0]))
    place([{"x": t[0], "y": t[1], "add": [wall_pieces[t]]} for t in order], "paredes da casa do druida")
    # 3. Interior.
    place([{"x": t[0], "y": t[1], "add": ids} for t, ids in INTERIOR.items()], "interior da casa")
    # 4. Outside: cauldron, herbs in the beds, lights, bushes, statue.
    outside = [{"x": CAULDRON_AT[0] + dx, "y": CAULDRON_AT[1] + dy, "add": [i]} for dx, dy, i in CAULDRON]
    for p in sorted(BED_A | BED_B):
        outside.append({"x": p[0], "y": p[1], "add": [rng.choice(HERBS)]})
    outside += [{"x": t[0], "y": t[1], "add": ids} for t, ids in EXTERIOR.items()]
    place(outside, "jardim, caldeirao e detalhes da casa")


if __name__ == "__main__":
    main()
