import json, random, rme
from borders import pieces_for_mask, mask_at
SETS = {int(k): v["p"] for k, v in json.load(open("sets.json")).items()}
rnd = random.Random(5)
X0, Y0, Z = 1900, 1900, 7
C = [
 ([8913, 8914, 8915, 8916, 8917], 125), ([8048, 8049, 8050, 8051, 8052], 132), ([11145]*8 + [11146, 11147, 11148, 11149, 11150, 11151, 11152], 134),
 ([101]*30 + list(range(5711, 5727)), 21), (list(range(4570, 4580)), 11), (list(range(13636, 13644)), 311),
 ([49667], 277), ([11154, 11155, 11156, 11157, 11158, 11160], 146), ([17625, 17626, 17627, 17628], 163), ([12912], 143),
]
SHAPE = ["...XX....", "..XXXX...", ".XXXXXX..", "..XXXXX..", "...XX...."]
tiles = {}
def put(x, y, g=None, add=None):
    t = tiles.setdefault((x, y), {"x": x, "y": y, "clear": "all"})
    if g is not None: t["ground"] = g
    if add: t.setdefault("add", []).extend(add)
for i, (gs, bid) in enumerate(C):
    bx = X0 + (i % 5) * 11; by = Y0 + (i // 5) * 9
    for y in range(by, by + 9):
        for x in range(bx, bx + 11):
            put(x, y, g=rnd.choice([351]*6 + [352, 353, 354, 355]))
    pts = set()
    for r, row in enumerate(SHAPE):
        for c, ch in enumerate(row):
            if ch == "X": pts.add((bx + 1 + c, by + 2 + r))
    for p in pts: put(p[0], p[1], g=rnd.choice(gs))
    cand = {(x+dx, y+dy) for (x, y) in pts for dx in (-1, 0, 1) for dy in (-1, 0, 1)} - pts
    for (x, y) in cand:
        for nm in pieces_for_mask(mask_at(x, y, lambda a, b: (a, b) in pts)):
            if nm in SETS[bid]: put(x, y, add=[rnd.choice(SETS[bid][nm])])
print(rme.call("place", {"z": Z, "tiles": list(tiles.values()), "description": "patch test"})[0][:90])
