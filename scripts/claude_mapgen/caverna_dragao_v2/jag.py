"""Break straight contour runs (>= MAXRUN+1 tiles) with 2-wide teeth/notches."""
import random
import numpy as np
from scipy import ndimage
from layout import MOUNT, FLOOR, LAVA, GW, GH, G, conn_cells, block_ok, block3_ok

def straight_runs(grid, a, b, minlen):
    """Edges where a cell of type a has a neighbour of type b on side s. Returns list of runs:
    (side, fixed coord, start, length) for runs >= minlen."""
    runs = []
    A = grid == a
    Bm = np.isin(grid, b)
    for side, (dx, dy) in (("n", (0, -1)), ("s", (0, 1)), ("w", (-1, 0)), ("e", (1, 0))):
        nb = np.zeros_like(A)
        if dy == -1: nb[1:, :] = Bm[:-1, :]
        if dy == 1: nb[:-1, :] = Bm[1:, :]
        if dx == -1: nb[:, 1:] = Bm[:, :-1]
        if dx == 1: nb[:, :-1] = Bm[:, 1:]
        E = A & nb
        if dy != 0:   # horizontal runs along rows
            for gy in range(GH):
                row = E[gy]; x = 0
                while x < GW:
                    if row[x]:
                        s = x
                        while x < GW and row[x]: x += 1
                        if x - s >= minlen: runs.append((side, gy, s, x - s))
                    else: x += 1
        else:
            for gx in range(GW):
                col = E[:, gx]; y = 0
                while y < GH:
                    if col[y]:
                        s = y
                        while y < GH and col[y]: y += 1
                        if y - s >= minlen: runs.append((side, gx, s, y - s))
                    else: y += 1
    return runs

_EX = {}
def valid(grid, z, forced_idx):
    if z not in _EX:
        ex = np.zeros(grid.shape, dtype=bool)
        for (gx, gy) in forced_idx:
            ex[max(0, gy-1):gy+2, max(0, gx-1):gx+2] = True
        _EX[z] = ex
    ex = _EX[z]
    fl = grid == FLOOR
    if (fl & ~block3_ok(fl) & ~ex).any(): return False
    nf = grid != FLOOR
    mt = grid == MOUNT
    if (mt & ~block_ok(nf) & ~ex).any(): return False
    lv = grid == LAVA
    if (lv & ~block_ok(lv) & ~ex).any(): return False
    for (gx, gy), v in forced_idx.items():
        if grid[gy, gx] != v: return False
    lab, n = ndimage.label(fl)
    return n == 1

def jaggedize(grid, z, maxrun=3, seed=0, rounds=12):
    rnd = random.Random(seed)
    forced, _ = conn_cells(z)
    fidx = {G(*k): v for k, v in forced.items()}
    # protect a ring around forced cells
    prot = set()
    for (gx, gy) in fidx:
        for dx in range(-2, 3):
            for dy in range(-2, 3):
                prot.add((gx+dx, gy+dy))
    for r in range(rounds):
        runs = straight_runs(grid, FLOOR, [MOUNT, LAVA], maxrun+1) + straight_runs(grid, LAVA, [FLOOR], maxrun+1)
        if not runs:
            break
        rnd.shuffle(runs)
        changed = 0
        for side, fixed, start, L in runs:
            done = False
            for mode in rnd.sample(["tooth", "notch"], 2):
                k = 2 if mode == "tooth" else 3
                ps = list(range(-1 if mode == "notch" else 0, max(1, L - k + 2)))
                mid = [p for p in ps if 1 <= p <= L - k - 1] or ps
                rnd.shuffle(mid)
                for p in mid[:4]:
                    cells = []
                    for i in range(p, p + k):
                        if side in ("n", "s"):
                            gx, gy = start + i, fixed
                            o = -1 if side == "n" else 1
                            c = (gx, gy) if mode == "tooth" else (gx, gy + o)
                            src = (gx, gy + o) if mode == "tooth" else (gx, gy)
                        else:
                            gx, gy = fixed, start + i
                            o = -1 if side == "w" else 1
                            c = (gx, gy) if mode == "tooth" else (gx + o, gy)
                            src = (gx + o, gy) if mode == "tooth" else (gx, gy)
                        cells.append((c, src))
                    if any(c in prot for c, _ in cells): continue
                    if any(not (3 <= c[0] < GW-3 and 3 <= c[1] < GH-3) for c, _ in cells): continue
                    old = [grid[c[1], c[0]] for c, _ in cells]
                    news = [grid[sr[1], sr[0]] for _, sr in cells]
                    for (c, _), nv in zip(cells, news):
                        grid[c[1], c[0]] = nv
                    if valid(grid, z, fidx):
                        changed += 1; done = True; break
                    for (c, _), o_ in zip(cells, old):
                        grid[c[1], c[0]] = o_
                if done: break
        if not changed:
            break
    left = straight_runs(grid, FLOOR, [MOUNT, LAVA], maxrun+1) + straight_runs(grid, LAVA, [FLOOR], maxrun+1)
    return grid, left
