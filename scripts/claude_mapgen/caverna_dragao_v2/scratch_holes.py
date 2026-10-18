import json, rme
from borders import pieces_for_mask, mask_at
SETS = {int(k): v["p"] for k, v in json.load(open("sets.json")).items()}
X0, Y0, Z = 1900, 1900, 7
# 6 test niches, each 7x6 block: mountain everywhere except a tongue of floor coming from north and ending at the hole
variants = [
  ("459 full", 459, None, "sew"), ("459 s+e", 459, None, "se"), ("8563", 351, 8563, "sew"),
  ("8564", 351, 8564, "sew"), ("8565", 351, 8565, "sew"), ("8566", 351, 8566, "sew"),
  ("8565 no pieces", 351, 8565, ""), ("8564 no pieces", 351, 8564, ""),
]
tiles = {}
for i, (nm, g, it, keep) in enumerate(variants):
    bx = X0 + i * 7
    floor = set()
    for y in range(Y0, Y0 + 4):
        floor.add((bx + 3, y)); floor.add((bx + 2, y))
    floor.add((bx + 3, Y0 + 4))  # hole tile
    hole = (bx + 3, Y0 + 4)
    for y in range(Y0 - 1, Y0 + 7):
        for x in range(bx, bx + 7):
            p = (x, y)
            t = {"x": x, "y": y, "clear": "all"}
            if p in floor:
                t["ground"] = 351
                names = pieces_for_mask(mask_at(x, y, lambda a, b: (a, b) not in floor))
                if p == hole:
                    t["ground"] = g
                    names = [n for n in names if n in keep]
                add = [SETS[59][n][0] for n in names if n in SETS[59]]
                if p == hole and it: add.append(it)
                if add: t["add"] = add
            else:
                t["ground"] = 10164
            tiles[p] = t
print(rme.call("place", {"z": Z, "tiles": list(tiles.values()), "description": "hole test"})[0][:120])
