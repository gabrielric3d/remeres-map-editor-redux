"""Rebuild the druid house walls the way the wall brush does. python walls2.py preview|apply

Walls are their own ring of tiles around the floor. The piece on each ring tile comes from
the wall neighbours (wood wall only has horizontal/vertical/corner/pole, so the brush uses its
half table, which only looks at N and W): N+W = corner, N = vertical, W = horizontal, none = pole.
"""
import sys

import house
import rme_mcp

Z = 7
F = house.F
WOOD = house.WOOD
OLD_IDS = sorted(set(WOOD["h"] + WOOD["v"] + [WOOD["corner"], WOOD["pole"]] + list(house.DOOR.values()) + [6178, 6179]))

DOOR = {(1024, 1025): 6195}
WINDOWS = {(1021, 1025): 6178, (1026, 1025): 6178, (1028, 1022): 6179, (1015, 1022): 6179, (1022, 1015): 6178}
# Exterior details that sat where the new ring goes, moved one tile out.
MOVED = {(1015, 1021): ((1014, 1021), [2767]), (1015, 1023): ((1014, 1023), [2786]), (1015, 1025): ((1014, 1026), [9466])}


def ring():
    r = set()
    for (x, y) in F:
        n, s, w, e = (x, y - 1) not in F, (x, y + 1) not in F, (x - 1, y) not in F, (x + 1, y) not in F
        if n:
            r.add((x, y - 1))
        if s:
            r.add((x, y + 1))
        if w:
            r.add((x - 1, y))
        if e:
            r.add((x + 1, y))
        for a, b, d in ((n, w, (-1, -1)), (n, e, (1, -1)), (s, w, (-1, 1)), (s, e, (1, 1))):
            if a and b and (x + d[0], y + d[1]) not in F:
                r.add((x + d[0], y + d[1]))
    return r


def pieces(r, rng):
    out = {}
    for (x, y) in r:
        north, west = (x, y - 1) in r, (x - 1, y) in r
        if north and west:
            out[(x, y)] = WOOD["corner"]
        elif north:
            out[(x, y)] = rng.choice(WOOD["v"])
        elif west:
            out[(x, y)] = rng.choice(WOOD["h"])
        else:
            out[(x, y)] = WOOD["pole"]
    out.update(DOOR)
    out.update(WINDOWS)
    return out


def main():
    import random
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    rng = random.Random(11)
    r = ring()
    p = pieces(r, rng)
    for t in list(DOOR) + list(WINDOWS):
        assert t in r, t
    if mode == "preview":
        sym = {WOOD["corner"]: "+", WOOD["pole"]: "*"}
        for y in range(1013, 1028):
            row = ""
            for x in range(1012, 1031):
                if (x, y) in p:
                    i = p[(x, y)]
                    row += "D" if i in DOOR.values() else "o" if i in WINDOWS.values() else sym.get(i, "-" if i in WOOD["h"] else "|")
                else:
                    row += "_" if (x, y) in F else "."
            print(row)
        return

    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "walls2", "version": "1"}})

    def place(tiles, label):
        res = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": Z, "tiles": tiles, "description": label}}, 3)
        print(label, "->", res["result"]["content"][0]["text"])

    old = house.walls()
    tiles = [{"x": t[0], "y": t[1], "remove": OLD_IDS} for t in sorted(old) if t in F]
    for src, (dst, ids) in MOVED.items():
        tiles.append({"x": dst[0], "y": dst[1], "add": ids})
    # Ring: floor under the wall, then the piece; clockwise from the north-west.
    cx, cy = 1022, 1021
    import math
    order = sorted(p, key=lambda t: math.atan2(t[1] - cy, t[0] - cx))
    tiles += [{"x": t[0], "y": t[1], "clear": "all", "ground": 405, "add": [p[t]]} for t in order]
    place(tiles, "paredes da casa (refeitas pela regra do wall brush)")


if __name__ == "__main__":
    main()
