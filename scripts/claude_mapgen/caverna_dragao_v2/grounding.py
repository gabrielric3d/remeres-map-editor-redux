"""Grounding pass: split the cave floor into cave / foot strip / hot rock / earth patches / nests,
weave patch ownership, and emit place() tiles with every border piece."""
import json, random, math
import numpy as np
from scipy import ndimage
from layout import MOUNT, FLOOR, LAVA, GW, GH, MG, OX, OY, G, conn_cells, noise_grid, block_ok
from borders import pieces_for_mask, NW, N, NE, W, E, SW, S, SE

SETS = {int(k): v["p"] for k, v in json.load(open("sets.json")).items()}
OFF8 = {NW: (-1, -1), N: (0, -1), NE: (1, -1), W: (-1, 0), E: (1, 0), SW: (-1, 1), S: (0, 1), SE: (1, 1)}
DIRNAME = {(-1, -1): "nw", (0, -1): "n", (1, -1): "ne", (-1, 0): "w", (1, 0): "e", (-1, 1): "sw", (0, 1): "s", (1, 1): "se"}

# ground types (codes) -------------------------------------------------------
CAVE, FOOT, HOT, EARTH, NEST = 10, 11, 12, 13, 14
TYPES = {
    MOUNT: dict(z=100, border=59, ids=[(10164, 1)]),
    LAVA:  dict(z=90, border=211, ids=[(23850, 25), (23849, 1), (23848, 2), (23851, 2)]),
    HOT:   dict(z=60, border=311, ids=[(13636, 4), (13637, 2), (13638, 1), (13639, 2), (13640, 1), (13641, 2), (13642, 2), (13643, 2)]),
    FOOT:  dict(z=50, border=125, ids=[(8913, 1), (8914, 1), (8915, 1), (8916, 1), (8917, 1)]),
    NEST:  dict(z=45, border=132, ids=[(8048, 5), (8049, 3), (8050, 1), (8051, 5), (8052, 5)]),
    EARTH: dict(z=40, border=21, ids=[(101, 1)]),
    CAVE:  dict(z=10, border=7, ids=[(351, 12), (352, 2), (353, 2), (354, 2), (355, 2)]),
}
PATCH_WEAVE = (FOOT, EARTH, HOT)

NESTS = {  # local centres of sunken nests (floor 10 = dragon lords)
    8: [(33, 70, 2.3), (64, 68, 2.2)],
    9: [(11, 11, 2.3), (90, 56, 2.0)],
    10: [(48, 23, 2.6), (48, 3, 2.3), (84, 12, 2.2), (12, 14, 2.2)],
}

def wchoice(rnd, items):
    tot = sum(w for _, w in items)
    r = rnd.uniform(0, tot)
    for it, w in items:
        r -= w
        if r <= 0:
            return it
    return items[-1][0]

def keep_blocks(mask, iters=3):
    for _ in range(iters):
        mask = mask & block_ok(mask)
        # fill 1-tile gaps (non-mask cells surrounded on 3+ sides)
        nb = np.zeros(mask.shape, dtype=int)
        nb[1:, :] += mask[:-1, :]; nb[:-1, :] += mask[1:, :]; nb[:, 1:] += mask[:, :-1]; nb[:, :-1] += mask[:, 1:]
        mask = mask | (nb >= 3)
    return mask & block_ok(mask)

def build_types(grid, z, seed):
    rnd = random.Random(seed)
    fl = grid == FLOOR
    t = np.where(fl, CAVE, grid).astype(np.int16)
    forced, marks = conn_cells(z)
    near_conn = np.zeros(grid.shape, dtype=bool)
    for (lx, ly) in marks:
        gx, gy = G(lx, ly)
        near_conn[max(0, gy-2):gy+3, max(0, gx-2):gx+3] = True
    d_m = ndimage.distance_transform_edt(grid != MOUNT)
    d_l = ndimage.distance_transform_edt(grid != LAVA)
    # hot rock around lava ------------------------------------------------------
    r_hot = np.clip(1.5 + 1.1 * noise_grid(seed + 11, 3.5, 2), 0.9, 2.9)
    hot = fl & (d_l <= r_hot) & ~near_conn
    # foot strip along the walls -------------------------------------------------
    active = noise_grid(seed + 12, 9.0, 2) > -0.15
    r_foot = np.clip(1.6 + 1.2 * noise_grid(seed + 13, 4.0, 2), 1.0, 2.8)
    foot = fl & active & (d_m <= r_foot) & ~near_conn
    # tongues: short lines leaving the wall, perpendicular to it
    gy_, gx_ = np.gradient(d_m)
    wall = np.argwhere(fl & (d_m <= 1.01) & active & ~near_conn)
    rnd.shuffle(wall)
    tongue = np.zeros(grid.shape, dtype=bool)
    for (cy, cx) in wall[: max(1, len(wall) // 11)]:
        vx, vy = gx_[cy, cx], gy_[cy, cx]
        L = math.hypot(vx, vy)
        if L < 1e-6:
            continue
        vx, vy = vx / L, vy / L
        ln = rnd.randint(3, 5)
        # slight bend
        bend = rnd.uniform(-0.5, 0.5)
        for k in range(ln):
            px = cx + vx * k + (-vy) * bend * k / ln
            py = cy + vy * k + vx * bend * k / ln
            for ox in (0, 1):
                for oy in (0, 1):
                    xx, yy = int(round(px)) + ox - (1 if vx < 0 else 0), int(round(py)) + oy - (1 if vy < 0 else 0)
                    if 0 <= xx < GW and 0 <= yy < GH and fl[yy, xx] and not near_conn[yy, xx]:
                        tongue[yy, xx] = True
    foot = foot | tongue
    # earth patches in open floor ------------------------------------------------
    e = noise_grid(seed + 14, 6.5, 2) + 0.35 * noise_grid(seed + 15, 2.5, 2)
    earth = fl & (e > 0.42) & (d_m >= 2.5) & (d_l >= 3.0) & ~near_conn
    # nests ------------------------------------------------------------------------
    nest = np.zeros(grid.shape, dtype=bool)
    ys, xs = np.mgrid[0:GH, 0:GW]
    nn = noise_grid(seed + 16, 2.2, 2)
    for (lx, ly, r) in NESTS.get(z, []):
        gx, gy = G(lx, ly)
        nest |= (np.hypot(xs - gx, (ys - gy) * 1.15) + nn * 0.9 < r)
    nest &= fl & ~near_conn & (d_m >= 1.5)
    # priorities & cleanup -----------------------------------------------------------
    hot = keep_blocks(hot)
    foot = keep_blocks(foot & ~hot)
    nest = keep_blocks(nest & ~hot & ~foot)
    earth = keep_blocks(earth & ~hot & ~foot & ~nest)
    t[earth] = EARTH
    t[nest] = NEST
    t[foot] = FOOT
    t[hot] = HOT
    t[~fl] = grid[~fl]
    return t

def weave_flags(t, seed, share=0.22):
    """Return a bool grid: patch tiles on the contour whose ownership is inverted (cave border on them)."""
    inv = np.zeros(t.shape, dtype=bool)
    n = noise_grid(seed + 21, 3.2, 2)
    for P in PATCH_WEAVE:
        pm = t == P
        cave = t == CAVE
        adj = np.zeros(t.shape, dtype=bool)
        adj[1:, :] |= cave[:-1, :]; adj[:-1, :] |= cave[1:, :]; adj[:, 1:] |= cave[:, :-1]; adj[:, :-1] |= cave[:, 1:]
        contour = pm & adj
        vals = n[contour]
        if vals.size == 0:
            continue
        thr = np.quantile(vals, 1.0 - share)
        cand = contour & (n > thr)
        # drop runs shorter than 3 contour tiles
        lab, k = ndimage.label(cand, structure=np.ones((3, 3)))
        for i in range(1, k + 1):
            if (lab == i).sum() >= 3:
                inv |= lab == i
    return inv

def compute_pieces(t, inv):
    """pieces[(gx,gy)] = list of (z_owner, item_id)."""
    ez = np.zeros(t.shape, dtype=np.int16)
    for code, d in TYPES.items():
        ez[t == code] = d["z"]
    ez[inv] = 5
    rnd = random.Random(99)
    pieces = {}
    for gy in range(GH):
        for gx in range(GW):
            me = int(t[gy, gx])
            if me == MOUNT:
                continue
            owners = {}
            for bit, (dx, dy) in OFF8.items():
                x, y = gx + dx, gy + dy
                if not (0 <= x < GW and 0 <= y < GH):
                    continue
                o = int(t[y, x])
                if o == me or ez[y, x] <= ez[gy, gx]:
                    continue
                if me == LAVA and o != MOUNT:
                    continue
                owners[o] = owners.get(o, 0) | bit
            lst = []
            for o, m in owners.items():
                bset = TYPES[o]["border"]
                for nm in pieces_for_mask(m):
                    ids = SETS[bset].get(nm)
                    if ids:
                        lst.append((TYPES[o]["z"], nm, o, rnd.choice(ids)))
            if lst:
                pieces[(gx, gy)] = lst
    return pieces

def weave_corners(t, inv, pieces):
    """Corner fixes at every switch between an inverted and a normal patch tile along the contour."""
    rnd = random.Random(7)
    def add(cell, owner, name, top=True):
        ids = SETS[TYPES[owner]["border"]].get(name)
        if not ids:
            return
        z = 999 if top else TYPES[owner]["z"]
        pieces.setdefault(cell, []).append((z, name, owner, rnd.choice(ids)))
    def remove(cell, owner, name):
        if cell in pieces:
            pieces[cell] = [p for p in pieces[cell] if not (p[2] == owner and p[1] == name)]
    for gy in range(1, GH - 1):
        for gx in range(1, GW - 1):
            if not inv[gy, gx]:
                continue
            P = int(t[gy, gx])
            for ax, ay in ((1, 0), (-1, 0), (0, 1), (0, -1)):      # neighbour along the contour
                x2, y2 = gx + ax, gy + ay
                if int(t[y2, x2]) != P or inv[y2, x2]:
                    continue
                for sx, sy in ((-ay, ax), (ay, -ax)):              # side where the cave lies
                    q1 = (gx + sx, gy + sy); q2 = (x2 + sx, y2 + sy)
                    if int(t[q1[1], q1[0]]) != CAVE or int(t[q2[1], q2[0]]) != CAVE:
                        continue
                    p1, p2 = (gx, gy), (x2, y2)
                    # Q1 loses the patch corner pointing to P2
                    remove(q1, P, "c" + DIRNAME[(p2[0] - q1[0], p2[1] - q1[1])])
                    # P2 gets the cave corner pointing to Q1
                    add(p2, CAVE, "c" + DIRNAME[(q1[0] - p2[0], q1[1] - p2[1])])
                    # Q2 gets the cave corner pointing to P1, on top of the patch border
                    add(q2, CAVE, "c" + DIRNAME[(p1[0] - q2[0], p1[1] - q2[1])])
    return pieces

def glob(gx, gy):
    return OX + gx - MG, OY + gy - MG

RAMP_ITEMS = {"N": (8378, 8379), "W": (8374, 8375), "S": (8376, 8377), "E": (8372, 8373)}
HOLE_ITEM = {"N": 8561, "W": 8560, "E": 8559, "S": 8562}

def ground_tiles(grid, z, seed=0, weave=True):
    t = build_types(grid, z, seed * 31 + z)
    inv = weave_flags(t, seed * 31 + z) if weave else np.zeros(t.shape, dtype=bool)
    pieces = compute_pieces(t, inv)
    if weave:
        pieces = weave_corners(t, inv, pieces)
    _, marks = conn_cells(z)
    mk = {G(*k): v for k, v in marks.items()}
    rnd = random.Random(seed * 7919 + z)
    tiles = []
    for gy in range(GH):
        for gx in range(GW):
            code = int(t[gy, gx])
            x, y = glob(gx, gy)
            tile = {"x": x, "y": y, "z": z, "clear": "all", "ground": wchoice(rnd, TYPES[code]["ids"])}
            lst = sorted(pieces.get((gx, gy), []), key=lambda p: p[0])
            add = [p[3] for p in lst]
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
    return tiles, t, inv

if __name__ == "__main__":
    import sys, rme
    z = int(sys.argv[1])
    grid = np.load(f"grid{z}.npy")
    tiles, t, inv = ground_tiles(grid, z)
    np.save(f"types{z}.npy", t)
    np.save(f"inv{z}.npy", inv)
    json.dump(tiles, open(f"tiles_ground{z}.json", "w"))
    cnt = {k: int((t == k).sum()) for k in (CAVE, FOOT, HOT, EARTH, NEST)}
    print(z, "types", cnt, "inverted", int(inv.sum()), "tiles", len(tiles))
    if len(sys.argv) > 2 and sys.argv[2] == "place":
        CH = 4000
        for i in range(0, len(tiles), CH):
            r, _ = rme.call("place", {"z": z, "tiles": tiles[i:i+CH], "description": f"cave grounding z{z} ({i // CH + 1})"})
            print(r[:120])
