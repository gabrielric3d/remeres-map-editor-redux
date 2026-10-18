import json, random, rme
from borders import pieces_for_mask, mask_at
SETS = {int(k): v["p"] for k, v in json.load(open("sets.json")).items()}
random.seed(3)
Z = 7
X0, Y0 = 1900, 1900
tiles = {}
def put(x, y, ground=None, add=None):
    t = tiles.setdefault((x, y), {"x": x, "y": y, "z": Z, "add": []})
    if ground is not None: t["ground"] = ground
    if add: t["add"].extend(add)
def pick(v): return random.choice(v) if isinstance(v, list) else v
def outer(pts, bid):
    ins = lambda x, y: (x, y) in pts
    cand = {(x+dx, y+dy) for (x, y) in pts for dx in (-1,0,1) for dy in (-1,0,1)} - pts
    for (x, y) in cand:
        for nm in pieces_for_mask(mask_at(x, y, ins)):
            if nm in SETS[bid]: put(x, y, add=[pick(SETS[bid][nm])])
def inner(pts, bid):
    for (x, y) in pts:
        m = mask_at(x, y, lambda a, b: (a, b) not in pts)
        for nm in pieces_for_mask(m):
            if nm in SETS[bid]: put(x, y, add=[pick(SETS[bid][nm])])
SHAPE = ["..XX...", ".XXXX..", ".XXXXX.", "..XX..."]
def block(bx, by, floor, mat, bid, mode="outer"):
    for dy in range(7):
        for dx in range(8):
            put(bx+dx, by+dy, ground=pick(floor))
    pts = set()
    for r, row in enumerate(SHAPE):
        for c, ch in enumerate(row):
            if ch == "X":
                pts.add((bx+c, by+1+r))
    for p in pts: put(p[0], p[1], ground=pick(mat))
    if bid:
        (outer if mode == "outer" else inner)(pts, bid)
CAVE = [351]*6 + [352, 353, 354, 355]
rowA = [(919, 10), (34673, 300), (46614, 249), (55637, 307), (56812, 320), (51835, 266), (10164, 59), (17854, 61), (19141, 66), (15114, 60)]
for i, (mat, bid) in enumerate(rowA):
    block(X0 + i*9, Y0, CAVE, mat, bid)
rowB = [(598, 18, "inner"), (598, 18, "outer"), (23850, 211, "outer"), (49667, 277, "outer"), (49708, 314, "outer"), (13636, 311, "outer"), (49707, None, "outer"), (48470, 329, "outer"), (49660, 276, "outer"), (50100, 271, "outer")]
for i, (mat, bid, mode) in enumerate(rowB):
    block(X0 + i*9, Y0 + 8, CAVE, mat, bid, mode)
floors = [[351,352,353,354,355], [9021,9022,9023,9024,9025], list(range(34817, 34831)), list(range(4405, 4419)), [11145,11146,11147,11148,11149,11150,11151,11152], [8048,8049,8050,8051,8052], list(range(4570, 4580)), [8913,8914,8915,8916,8917], [11490,11491], [12912], [101]*20+list(range(5711,5724)), [836], [17625,17626,17627,17628], list(range(10766,10775)), [7348], [49660], list(range(42628,42636)), [49694], [50100], [25549,25704,25705,25706,25707,25708], [11561], [45581], list(range(39984,39994)), [11154,11155,11156,11157,11158,11160], list(range(56264,56274)), [15114,15115,15065], list(range(25011,25018)), [50374], [26620,26621,26622,26623], list(range(10617,10625)), list(range(11505,11510)), list(range(13636,13644)), [49667], [49708], [49707], [598,600,601], [23850]*10+[23849,23848,23851], [17854,17855,17856,17857], [280]]
for i, f in enumerate(floors):
    cx = X0 + (i % 20) * 5
    cy = Y0 + 16 + (i // 20) * 4
    for dy in range(3):
        for dx in range(4):
            put(cx+dx, cy+dy, ground=pick(f))
print(len(tiles))
t, _ = rme.call("place", {"z": Z, "tiles": list(tiles.values()), "description": "preview board (temporary)"})
print(t[:300])
json.dump({"floors": floors}, open("preview_floors.json", "w"))
