"""Turn a layout grid into place() tiles: grounds + border pieces (+ ramps/holes)."""
import json, random
import numpy as np
from layout import MOUNT, FLOOR, LAVA, GW, GH, MG, OX, OY, G, conn_cells, CONNS, DIRS
from borders import pieces_for_mask, NW, N, NE, W, E, SW, S, SE

SETS = {int(k): v["p"] for k, v in json.load(open("sets.json")).items()}
OFF8 = {NW: (-1, -1), N: (0, -1), NE: (1, -1), W: (-1, 0), E: (1, 0), SW: (-1, 1), S: (0, 1), SE: (1, 1)}

def wchoice(rnd, items):
    tot = sum(w for _, w in items)
    r = rnd.uniform(0, tot)
    for it, w in items:
        r -= w
        if r <= 0:
            return it
    return items[-1][0]

CAVE = [(351, 12), (352, 2), (353, 2), (354, 2), (355, 2)]
LAVAG = [(23850, 25), (23849, 1), (23848, 2), (23851, 2)]
MOUNTG = [(10164, 1)]

RAMP_ITEMS = {"N": (8378, 8379), "W": (8374, 8375), "S": (8376, 8377), "E": (8372, 8373)}
HOLE_ITEM = {"N": 8561, "W": 8560, "E": 8559, "S": 8562}

def glob(gx, gy):
    return OX + gx - MG, OY + gy - MG

def mask(grid, gx, gy, pred):
    m = 0
    for bit, (dx, dy) in OFF8.items():
        x, y = gx + dx, gy + dy
        if 0 <= x < GW and 0 <= y < GH and pred(grid[y, x]):
            m |= bit
    return m

def piece_ids(bset, names, rnd):
    out = []
    for nm in names:
        v = SETS[bset].get(nm)
        if v:
            out.append(rnd.choice(v))
    return out

def structure_tiles(grid, z, seed=0):
    rnd = random.Random(seed * 7919 + z)
    _, marks = conn_cells(z)
    mk = {G(*k): v for k, v in marks.items()}
    tiles = []
    for gy in range(GH):
        for gx in range(GW):
            t = int(grid[gy, gx])
            x, y = glob(gx, gy)
            tile = {"x": x, "y": y, "z": z, "clear": "all"}
            add = []
            if t == MOUNT:
                tile["ground"] = wchoice(rnd, MOUNTG)
            elif t == LAVA:
                tile["ground"] = wchoice(rnd, LAVAG)
                add += piece_ids(59, pieces_for_mask(mask(grid, gx, gy, lambda v: v == MOUNT)), rnd)
            else:
                tile["ground"] = wchoice(rnd, CAVE)
                add += piece_ids(211, pieces_for_mask(mask(grid, gx, gy, lambda v: v == LAVA)), rnd)
                add += piece_ids(59, pieces_for_mask(mask(grid, gx, gy, lambda v: v == MOUNT)), rnd)
            m = mk.get((gx, gy))
            if m:
                kind, name, d = m
                if kind == "hole":
                    add.append(HOLE_ITEM[d])
                elif kind == "ramp":
                    add.append(RAMP_ITEMS[d][0])
                elif kind == "rampbase":
                    add.append(RAMP_ITEMS[d][1])
            if add:
                tile["add"] = add
            tiles.append(tile)
    return tiles

if __name__ == "__main__":
    import sys, rme
    z = int(sys.argv[1])
    grid = np.load(f"grid{z}.npy")
    tiles = structure_tiles(grid, z)
    json.dump(tiles, open(f"tiles_struct{z}.json", "w"))
    print(len(tiles), "tiles")
    if len(sys.argv) > 2 and sys.argv[2] == "place":
        CH = 4000
        for i in range(0, len(tiles), CH):
            t, _ = rme.call("place", {"z": z, "tiles": tiles[i:i+CH], "description": f"cave structure z{z} ({i // CH + 1})"})
            print(t[:160])
