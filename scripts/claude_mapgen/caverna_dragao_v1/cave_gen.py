"""Dragon cave, 3 floors (8, 9, 10), each floor a loop, the floors joined by 4 ramps into a
bigger loop: 8 -S1(east)- 9 -S2(south)- 10 -S3(west)- 9 -S4(north-west)- 8.

python cave_gen.py preview [z]      ASCII of one floor (or all)
python cave_gen.py apply <z>        grounds + borders + ramps of that floor (RAW)
"""
import json
import math
import os
import random
import sys

import cave_util as cu

HERE = os.path.dirname(os.path.abspath(__file__))

# Staircases: (name, x, y, upper z). Lower floor: north ramp at (x,y) + its tail at (x,y+1);
# upper floor: the way down (459) at (x,y) and the landing at (x,y-1).
STAIRS = [("S1", 1052, 1012, 8), ("S2", 1028, 1037, 9), ("S3", 1000, 1022, 9), ("S4", 1008, 1000, 8)]
RAMP_UP, RAMP_TAIL, WAY_DOWN = 8378, 8379, 459  # earth ramp (north)

FLOORS = {
    8: {
        "ring": [(1008, 1000), (1020, 996), (1034, 999), (1046, 998), (1052, 1004), (1052, 1012), (1050, 1022),
                 (1042, 1031), (1028, 1034), (1014, 1032), (1003, 1026), (998, 1014), (1001, 1005)],
        "spurs": [[(1028, 1034), (1027, 1039), (1025, 1043)], [(1020, 996), (1019, 991), (1016, 988)]],
        "chambers": [(1044, 999, 6, 4), (1004, 1030, 4, 3), (1025, 1044, 4, 3), (1015, 988, 3, 2)],
        "radius": (1.1, 2.3), "core": "void",
    },
    9: {
        "ring": [(1008, 1000), (1022, 997), (1038, 1001), (1050, 1005), (1052, 1012), (1053, 1022), (1044, 1033),
                 (1028, 1037), (1014, 1036), (1002, 1030), (1000, 1022), (1001, 1010)],
        "spurs": [[(1053, 1022), (1058, 1027)], [(1014, 1036), (1011, 1042)]],
        "chambers": [(1057, 1028, 4, 4), (1010, 1043, 4, 3), (1036, 1000, 4, 3)],
        "radius": (1.2, 2.5), "core": "mountain",
    },
    10: {
        "ring": [(1000, 1022), (1004, 1010), (1016, 1004), (1032, 1004), (1046, 1010), (1052, 1024), (1046, 1036),
                 (1028, 1037), (1012, 1036)],
        "spurs": [[(1046, 1036), (1052, 1041), (1056, 1043)], [(1016, 1004), (1012, 998)]],
        "chambers": [(1056, 1044, 4, 3), (1011, 996, 4, 3)],
        "radius": (1.6, 3.0), "core": "lava",
    },
}
GROUND = {"floor": "cave", "gravel": "rocky ground", "scorched": "rocky red floor", "nest": "rough lava stone ground",
          "lava": "lava (rock soil and cave ground)", "mountain": "dark mountain"}
ZORD = {"floor": 200, "gravel": 1000, "nest": 2650, "scorched": 4300, "lava": 7700, "mountain": 10000}
OUTER = {"gravel": 11, "scorched": 311, "nest": 277, "mountain": 249}
CAVE_EDGE, LAVA_EDGE = 7, 18


class Noise1:
    def __init__(self, seed, cell):
        self.r = random.Random(seed)
        self.cell, self.v = cell, {}

    def at(self, t):
        f = t / self.cell
        i = math.floor(f)
        a, b = self.v.setdefault(i, self.r.random()), self.v.setdefault(i + 1, self.r.random())
        s = f - i
        s = s * s * (3 - 2 * s)
        return a + (b - a) * s


def catmull(points, closed, step=0.3):
    pts = points + points[:3] if closed else [points[0]] + points + [points[-1]]
    out = []
    n = len(points) if closed else len(points) - 1
    for i in range(n):
        p0, p1, p2, p3 = pts[i], pts[i + 1], pts[i + 2], pts[i + 3]
        seg = math.hypot(p2[0] - p1[0], p2[1] - p1[1])
        k = max(2, int(seg / step))
        for j in range(k):
            t = j / k
            t2, t3 = t * t, t * t * t
            x = 0.5 * (2 * p1[0] + (-p0[0] + p2[0]) * t + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3)
            y = 0.5 * (2 * p1[1] + (-p0[1] + p2[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3)
            out.append((x, y))
    return out


def carve(path, rmin, rmax, seed):
    n1, n2, n3, nw = Noise1(seed, 7), Noise1(seed + 1, 2.5), Noise1(seed + 2, 1.2), Noise1(seed + 3, 3.1)
    tiles = set()
    for i, (x, y) in enumerate(path):
        t = i * 0.3
        r = rmin + (rmax - rmin) * (0.5 * n1.at(t) + 0.25 * n2.at(t) + 0.25 * n3.at(t))
        # sideways wobble of the centre line, so the two walls do not mirror each other
        if 0 < i < len(path) - 1:
            tx, ty = path[i + 1][0] - path[i - 1][0], path[i + 1][1] - path[i - 1][1]
            tl = math.hypot(tx, ty) or 1
            off = (nw.at(t) - 0.5) * 1.6
            x, y = x - ty / tl * off, y + tx / tl * off
        for tx in range(int(x - r) - 1, int(x + r) + 2):
            for ty in range(int(y - r) - 1, int(y + r) + 2):
                if math.hypot(tx - x, ty - y) <= r:
                    tiles.add((tx, ty))
    return tiles


def blob(cx, cy, rx, ry, seed):
    r = random.Random(seed)
    tiles = set()
    for _ in range(5):
        ox, oy = r.uniform(-rx * 0.5, rx * 0.5), r.uniform(-ry * 0.5, ry * 0.5)
        sx, sy = r.uniform(0.55, 0.9) * rx, r.uniform(0.55, 0.9) * ry
        for tx in range(int(cx - rx) - 2, int(cx + rx) + 3):
            for ty in range(int(cy - ry) - 2, int(cy + ry) + 3):
                if ((tx - cx - ox) / sx) ** 2 + ((ty - cy - oy) / sy) ** 2 <= 1:
                    tiles.add((tx, ty))
    return tiles


def organic(cx, cy, rx, ry, seed):
    """Union of discs scattered in an ellipse, smoothed: a round-ish, irregular patch."""
    r = random.Random(seed)
    tiles = set()
    for _ in range(9):
        a, d = r.uniform(0, 2 * math.pi), r.uniform(0, 0.75)
        ox, oy = math.cos(a) * d * rx, math.sin(a) * d * ry
        rad = r.uniform(0.9, 1.5) * min(rx, ry) * 0.6 + 0.6
        for tx in range(int(cx + ox - rad) - 1, int(cx + ox + rad) + 2):
            for ty in range(int(cy + oy - rad) - 1, int(cy + oy + rad) + 2):
                if math.hypot(tx - cx - ox, ty - cy - oy) <= rad:
                    tiles.add((tx, ty))
    return smooth(tiles, set(), 1) or tiles


def inside(poly, x, y):
    c = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi + 1e-9) + xi:
            c = not c
        j = i
    return c


def n4(p):
    x, y = p
    return [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]


def tidy(floor, keep_out=frozenset()):
    """No diagonal-only links, no 1-tile rock walls between two floors, no tiny pillars."""
    for _ in range(10):
        changed = False
        for (x, y) in list(floor):
            for dx, dy in ((1, 1), (1, -1)):
                if (x + dx, y + dy) in floor and (x + dx, y) not in floor and (x, y + dy) not in floor:
                    floor.add((x + dx, y))
                    changed = True
        xs = [p[0] for p in floor]
        ys = [p[1] for p in floor]
        for x in range(min(xs) - 1, max(xs) + 2):
            for y in range(min(ys) - 1, max(ys) + 2):
                p = (x, y)
                if p in floor or p in keep_out:
                    continue
                if ((x - 1, y) in floor and (x + 1, y) in floor) or ((x, y - 1) in floor and (x, y + 1) in floor):
                    floor.add(p)
                    changed = True
        if not changed:
            break
    # Fill holes of 1-3 tiles enclosed by floor.
    xs = [p[0] for p in floor]
    ys = [p[1] for p in floor]
    seen = set()
    for x in range(min(xs), max(xs) + 1):
        for y in range(min(ys), max(ys) + 1):
            p = (x, y)
            if p in floor or p in seen:
                continue
            comp, stack, open_ = [], [p], False
            seen.add(p)
            while stack:
                q = stack.pop()
                comp.append(q)
                if len(comp) > 3:
                    open_ = True
                for n in n4(q):
                    if n not in floor and n not in seen:
                        if not (min(xs) <= n[0] <= max(xs) and min(ys) <= n[1] <= max(ys)):
                            open_ = True
                            continue
                        seen.add(n)
                        stack.append(n)
            if not open_ and not (set(comp) & keep_out):
                floor |= set(comp)
    return floor


def break_runs(floor, rng, protect):
    """The mapper's rule: no side of the floor runs straight for 3+ tiles; a tooth of floor
    pushed into the rock breaks it (never leaving a 1-tile rock wall)."""
    def makes_strip(t):
        x, y = t
        for ax, ay in ((1, 0), (0, 1)):
            for s in (1, -1):
                mid, far = (x + s * ax, y + s * ay), (x + 2 * s * ax, y + 2 * s * ay)
                if mid not in floor and far in floor:
                    return True
        return False

    for _ in range(80):
        added = False
        for d in ((0, -1), (0, 1), (-1, 0), (1, 0)):
            along = (1, 0) if d[1] else (0, 1)
            edge = sorted((p for p in floor if (p[0] + d[0], p[1] + d[1]) not in floor),
                          key=lambda p: (p[1], p[0]) if d[1] else (p[0], p[1]))
            run = []
            for p in edge + [None]:
                if p is not None and run and (p[0] - run[-1][0], p[1] - run[-1][1]) == along:
                    run.append(p)
                    continue
                if len(run) >= 4:
                    # Step, not tooth: from 2-3 tiles in, the rest of the run (2+ tiles) moves
                    # one tile, into the rock or the rock into the floor: the wall stairs down.
                    for k in rng.sample((2, 3), 2):
                        part = run[k:]
                        if len(part) < 2:
                            continue
                        grow = [(p[0] + d[0], p[1] + d[1]) for p in part]
                        if all(t not in protect and not makes_strip(t) for t in grow):
                            floor |= set(grow)
                            added = True
                            break
                        if all(p not in protect and (p[0] - d[0], p[1] - d[1]) in floor and (p[0] - 2 * d[0], p[1] - 2 * d[1]) in floor for p in part):
                            for p in part:
                                floor.discard(p)
                            added = True
                            break
                run = [p] if p is not None else []
        if not added:
            break
    return floor


def smooth(floor, keep, rounds=2):
    """Cellular smoothing: organic outlines with natural diagonals, no 1-tile noise."""
    for _ in range(rounds):
        xs = [p[0] for p in floor]
        ys = [p[1] for p in floor]
        new = set()
        for x in range(min(xs) - 1, max(xs) + 2):
            for y in range(min(ys) - 1, max(ys) + 2):
                n = sum((x + dx, y + dy) in floor for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy)
                if (x, y) in keep or n >= 5 or ((x, y) in floor and n >= 4):
                    new.add((x, y))
        floor = new
    return floor


def main_piece(floor):
    start = next(iter(floor))
    best = set()
    left = set(floor)
    while left:
        start = next(iter(left))
        comp, stack = {start}, [start]
        while stack:
            q = stack.pop()
            for n in n4(q):
                if n in left and n not in comp:
                    comp.add(n)
                    stack.append(n)
        left -= comp
        if len(comp) > len(best):
            best = comp
    return best


def stair_tiles(z):
    """Tiles that must be floor on this z for the ramps (landing, hole, ramp, tail)."""
    need = set()
    for name, x, y, up in STAIRS:
        if z == up:
            need |= {(x, y), (x, y - 1), (x - 1, y), (x + 1, y)}
        elif z == up + 1:
            need |= {(x, y), (x, y + 1), (x - 1, y + 1), (x + 1, y + 1)}
    return need


def layout(z):
    spec = FLOORS[z]
    rng = random.Random(900 + z)
    rmin, rmax = spec["radius"]
    floor = carve(catmull(spec["ring"], True), rmin, rmax, 100 + z)
    for i, s in enumerate(spec["spurs"]):
        floor |= carve(catmull(s, False), rmin * 0.9, rmax * 0.85, 200 + z * 10 + i)
    for i, (cx, cy, rx, ry) in enumerate(spec["chambers"]):
        floor |= blob(cx, cy, rx, ry, 300 + z * 10 + i)
    for p in stair_tiles(z):
        floor |= {(p[0] + dx, p[1] + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)}
    keep = set()
    for p in stair_tiles(z):
        keep |= {(p[0] + dx, p[1] + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)}
    floor = smooth(floor, keep)
    floor = tidy(floor)
    floor = break_runs(floor, rng, keep)
    floor = tidy(floor)
    floor = main_piece(floor)
    # Core: inside the ring polygon, not floor.
    xs = [p[0] for p in floor]
    ys = [p[1] for p in floor]
    core = {(x, y) for x in range(min(xs), max(xs) + 1) for y in range(min(ys), max(ys) + 1)
            if (x, y) not in floor and inside(spec["ring"], x + 0.5, y + 0.5)}
    # Only the core part fully cut off from the outside rock counts.
    return floor, core


def lava_veins(floor, core, rng, avoid, count):
    """Narrow lava cracks leaving the mountain into the floor, diagonal, never cutting the way."""
    edge = [p for p in floor if any(n in core for n in n4(p)) and min(math.hypot(p[0] - a[0], p[1] - a[1]) for a in avoid) > 4]
    rng.shuffle(edge)
    veins, used = set(), []
    cx = sum(p[0] for p in core) / len(core)
    cy = sum(p[1] for p in core) / len(core)
    for start in edge:
        if len(used) >= count:
            break
        if any(math.hypot(start[0] - u[0], start[1] - u[1]) < 7 for u in used):
            continue
        dx, dy = start[0] - cx, start[1] - cy
        dl = math.hypot(dx, dy) or 1
        dx, dy = dx / dl, dy / dl
        path, (x, y) = [start], start
        length = rng.randint(2, 4)
        for _ in range(length - 1):
            fx, fy = x + dx + rng.uniform(-0.7, 0.7), y + dy + rng.uniform(-0.7, 0.7)
            step = (round(fx) - x, round(fy) - y)
            if step == (0, 0):
                break
            if abs(step[0]) + abs(step[1]) == 2:  # diagonal: go through an orthogonal tile
                step = (step[0], 0) if rng.random() < 0.5 else (0, step[1])
            nx, ny = x + step[0], y + step[1]
            # keep 2 tiles of floor between the crack and the far wall
            axes = (((dx > 0) - (dx < 0), 0), (0, (dy > 0) - (dy < 0)))
            if any((nx + ax * k, ny + ay * k) not in floor for ax, ay in axes for k in (1, 2)):
                break
            x, y = nx, ny
            path.append((x, y))
        if len(path) >= 2:
            veins |= set(path)
            used.append(start)
    return veins


def build(z):
    rng = random.Random(4000 + z)
    floor, core = layout(z)
    stairs = stair_tiles(z)
    avoid = [(x, y) for _, x, y, _ in STAIRS]
    kind = {p: "floor" for p in floor}
    spec = FLOORS[z]
    # Void pockets shut inside the ring become part of the core.
    xs = [p[0] for p in floor]
    ys = [p[1] for p in floor]
    outside, stack = set(), [(min(xs) - 1, min(ys) - 1)]
    while stack:
        q = stack.pop()
        if q in outside or q in floor or q in core or not (min(xs) - 1 <= q[0] <= max(xs) + 1 and min(ys) - 1 <= q[1] <= max(ys) + 1):
            continue
        outside.add(q)
        stack.extend(n4(q))
    core |= {(x, y) for x in range(min(xs), max(xs) + 1) for y in range(min(ys), max(ys) + 1)
             if (x, y) not in floor and (x, y) not in outside}
    cx = sum(p[0] for p in core) / len(core)
    cy = sum(p[1] for p in core) / len(core)
    if spec["core"] == "mountain":
        for p in core:
            kind[p] = "mountain"
        # Lava rivers cutting the mountain on the diagonal, centre out to its edge.
        rivers = [
            [(cx - 19, cy - 12), (cx - 9, cy - 5), (cx - 2, cy + 1), (cx + 8, cy + 5), (cx + 20, cy + 12)],  # the long one
            [(cx + 1, cy + 2), (cx - 3, cy + 9), (cx - 8, cy + 17)],  # branch south-west
            [(cx + 15, cy - 13), (cx + 10, cy - 7), (cx + 5, cy - 3)],  # short arm from the north-east
        ]
        for i, pts in enumerate(rivers):
            pts = [(x + rng.uniform(-1.5, 1.5), y + rng.uniform(-1.5, 1.5)) for x, y in pts]
            path = catmull(pts, False, 0.25)
            for j, (x, y) in enumerate(path):
                # meander sideways and breathe in width, so no stretch is a ruled line
                if 0 < j < len(path) - 1:
                    tx, ty = path[j + 1][0] - path[j - 1][0], path[j + 1][1] - path[j - 1][1]
                    tl = math.hypot(tx, ty) or 1
                    off = 0.8 * math.sin(j * 0.12 + i * 2.1)
                    x, y = x - ty / tl * off, y + tx / tl * off
                w = 1.25 + 0.35 * math.sin(j * 0.15 + i)
                for tx in range(int(x) - 2, int(x) + 3):
                    for ty in range(int(y) - 2, int(y) + 3):
                        if (tx, ty) in core and math.hypot(tx - x, ty - y) <= w:
                            kind[(tx, ty)] = "lava"
        # Smooth the rivers (no pixel steps) and drop mountain slivers left inside the lava.
        molten = {p for p in core if kind[p] == "lava"}
        molten = smooth(molten, set(), 1) & core
        for p in core:
            kind[p] = "lava" if p in molten else "mountain"
        for _ in range(3):
            for p in [q for q in core if kind[q] == "mountain"]:
                if sum(kind.get(n) == "lava" for n in n4(p)) >= 3 or                         (kind.get((p[0] - 1, p[1])) == "lava" and kind.get((p[0] + 1, p[1])) == "lava") or                         (kind.get((p[0], p[1] - 1)) == "lava" and kind.get((p[0], p[1] + 1)) == "lava"):
                    kind[p] = "lava"
        for p in lava_veins(floor, core, rng, avoid, 7):
            kind[p] = "lava"
        # a lava pool in the east chamber, with scorched ground round it
        pool = {p for p in organic(1057, 1028, 2.2, 1.8, 77) if p in floor and p not in stairs}
        for p in pool:
            kind[p] = "lava"
    elif spec["core"] == "lava":
        # Shore: lava bays biting into the ledge and points of ground pushing into the lake,
        # so the lake edge is not one regular staircase.
        near_void = {p for p in floor if any((p[0] + dx, p[1] + dy) in outside for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2))}
        shore = [p for p in floor if any(n in core for n in n4(p)) and p not in stairs]
        rng.shuffle(shore)
        picked = []
        for p in shore:
            if len(picked) >= 14:
                break
            if any(math.hypot(p[0] - q[0], p[1] - q[1]) < 5 for q in picked):
                continue
            picked.append(p)
            bay = len(picked) % 2 == 0
            ox, oy = p[0] - cx, p[1] - cy
            ol = math.hypot(ox, oy) or 1
            k = 0.8 if bay else -1.2
            patch = organic(p[0] + ox / ol * k, p[1] + oy / ol * k, rng.uniform(1.2, 2.0), rng.uniform(1.0, 1.6), 900 + len(picked))
            if bay:
                for q in patch:
                    if q in floor and q not in near_void and q not in stairs:
                        floor.discard(q)
                        core.add(q)
                        kind.pop(q, None)
            else:
                for q in patch:
                    if q in core and math.hypot(q[0] - cx, q[1] - cy) > 7:
                        core.discard(q)
                        floor.add(q)
                        kind[q] = "floor"
        for p in core:
            kind[p] = "lava"
        island = {p for p in organic(cx, cy, 3.6, 2.8, 55) if p in core}
        for p in island:
            kind[p] = "nest"
        # A natural bridge of dark stone from the south shore to the nest (the focal point).
        target = min((p for p in floor if p[1] > cy + 2), key=lambda p: math.hypot(p[0] - cx, p[1] - cy))
        steps = int(math.hypot(target[0] - cx, target[1] - cy) * 3)
        for j in range(steps + 1):
            t = j / max(1, steps)
            x = cx + (target[0] - cx) * t + math.sin(t * 5) * 0.8
            y = cy + (target[1] - cy) * t
            for tx in (math.floor(x), math.floor(x) + 1):
                q = (tx, round(y))
                if q in core:
                    kind[q] = "nest"
                    island.add(q)
        # Islets of dark rock in a diagonal band across the lake (never touching the shore).
        shore = {p for p in core if any(n in floor for n in n4(p)) or any((p[0] + dx, p[1] + dy) in floor for dx in (-1, 0, 1) for dy in (-1, 0, 1))}
        for i, t in enumerate((-0.8, -0.45, 0.4, 0.75)):
            bx, by = cx + t * 22 + rng.uniform(-2, 2), cy + t * 12 + rng.uniform(-2, 2)
            rock = {p for p in blob(bx, by, rng.uniform(1.6, 2.8), rng.uniform(1.3, 2.2), 60 + i) if p in core and p not in island}
            rock = {p for p in rock if not any((p[0] + dx, p[1] + dy) in floor for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2))}
            for p in rock:
                kind[p] = "mountain"
    # Void shut inside (not reachable from outside the floor plan) takes the core's ground.
    xs2 = [p[0] for p in kind]
    ys2 = [p[1] for p in kind]
    reach, stack = set(), [(min(xs2) - 1, min(ys2) - 1)]
    while stack:
        q = stack.pop()
        if q in reach or q in kind or not (min(xs2) - 1 <= q[0] <= max(xs2) + 1 and min(ys2) - 1 <= q[1] <= max(ys2) + 1):
            continue
        reach.add(q)
        stack.extend(n4(q))
    for x in range(min(xs2), max(xs2) + 1):
        for y in range(min(ys2), max(ys2) + 1):
            if (x, y) not in kind and (x, y) not in reach:
                around = [kind.get(n) for n in n4((x, y)) if kind.get(n)]
                kind[(x, y)] = max(set(around), key=around.count) if around else "floor"
    # Mountain never meets the void: those tiles fall back to plain rock (void).
    for p in [q for q, k in kind.items() if k == "mountain"]:
        if any((p[0] + dx, p[1] + dy) not in kind for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
            del kind[p]
    # Gravel: 1-2 tile strips along some rock walls (never touching the void).
    gravel_noise = Noise1(700 + z, 5)
    for p in floor:
        if kind[p] != "floor" or p in stairs:
            continue
        near_wall = any((p[0] + dx, p[1] + dy) not in kind for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2))
        touching = any((p[0] + dx, p[1] + dy) not in kind for dx in (-1, 0, 1) for dy in (-1, 0, 1))
        if False and near_wall and not touching and gravel_noise.at(p[0] * 0.7 + p[1] * 1.3) > 0.62:  # light gravel clashed with the brown cave
            kind[p] = "gravel"
    # Scorched ground round the lava (z 9 and 10), 1 tile, patchy.
    if spec["core"] in ("mountain", "lava"):
        sc = Noise1(800 + z, 3)
        for p in list(kind):
            if kind[p] in ("floor", "gravel") and p not in stairs:
                near_lava = any(kind.get((p[0] + dx, p[1] + dy)) == "lava" for dx in (-1, 0, 1) for dy in (-1, 0, 1))
                touching_void = any((p[0] + dx, p[1] + dy) not in kind for dx in (-1, 0, 1) for dy in (-1, 0, 1))
                if near_lava and not touching_void and sc.at(p[0] * 1.1 + p[1] * 0.6) > 0.45:
                    kind[p] = "scorched"
    return kind


def border_items(kind):
    items = {}
    walk = {p for p, k in kind.items() if k not in ("lava", "mountain")}
    # 1. Rock edge on the void around every walkable tile (cave border).
    void_ring = {(x + dx, y + dy) for (x, y) in walk for dx, dy, _ in cu.NEIGHBOURS} - set(kind)
    for p, ids in cu.pieces_around(walk, CAVE_EDGE, on=void_ring).items():
        items.setdefault(p, []).extend(ids)
    # 2. Outer borders of the higher grounds onto the lower ones (floor < gravel < nest < scorched < mountain).
    for p, k in kind.items():
        mine = ZORD[k]
        higher = sorted({kind.get((p[0] + dx, p[1] + dy)) for dx, dy, _ in cu.NEIGHBOURS} - {None, k}, key=lambda n: ZORD[n])
        for h in higher:
            if ZORD[h] <= mine or h not in OUTER:
                continue
            if k == "lava":
                continue  # lava keeps its own stone rim, nothing drawn over it
            bits = 0
            for dx, dy, bit in cu.NEIGHBOURS:
                if kind.get((p[0] + dx, p[1] + dy)) == h:
                    bits |= bit
            bs = cu.border_set(OUTER[h])
            items.setdefault(p, []).extend(bs[e] for e in cu.border_table(bits) if e in bs)
    # 3. Lava rim on the lava tiles, toward every walkable neighbour.
    lava = {p for p, k in kind.items() if k == "lava"}
    rim_to = walk | {p for p, k in kind.items() if k == "mountain"}
    for p, ids in cu.pieces_inside(lava, LAVA_EDGE, rim_to).items():
        items.setdefault(p, []).extend(ids)
    return items


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    zs = [int(sys.argv[2])] if len(sys.argv) > 2 else [8, 9, 10]
    sym = {"floor": ".", "gravel": ":", "scorched": "%", "nest": "N", "lava": "~", "mountain": "M"}
    for z in zs:
        kind = build(z)
        stairs_here = {}
        for name, x, y, up in STAIRS:
            if z == up:
                stairs_here[(x, y)] = "v"
            elif z == up + 1:
                stairs_here[(x, y)] = "^"
        if mode == "preview":
            xs = [p[0] for p in kind]
            ys = [p[1] for p in kind]
            print(f"== floor {z}: {len(kind)} tiles, x {min(xs)}-{max(xs)}, y {min(ys)}-{max(ys)}")
            for y in range(min(ys), max(ys) + 1):
                print(y, "".join(stairs_here.get((x, y)) or sym.get(kind.get((x, y)), " ") for x in range(min(xs), max(xs) + 1)))
            continue
        rng = random.Random(5000 + z)
        items = border_items(kind)
        tiles = []
        cx = sum(p[0] for p in kind) / len(kind)
        cy = sum(p[1] for p in kind) / len(kind)
        for p in sorted(set(kind) | set(items), key=lambda q: math.atan2(q[1] - cy, q[0] - cx)):
            spec = {"x": p[0], "y": p[1], "clear": "all"}
            if p in kind:
                spec["ground"] = cu.pick(GROUND[kind[p]], rng)
            add = list(items.get(p, []))
            if p in stairs_here:
                add.append(WAY_DOWN if stairs_here[p] == "v" else RAMP_UP)
            for name, x, y, up in STAIRS:
                if z == up + 1 and p == (x, y + 1):
                    add.append(RAMP_TAIL)
            if add:
                spec["add"] = add
            tiles.append(spec)
        # Anything left in the area from an earlier version goes (empty rock).
        done = {(t["x"], t["y"]) for t in tiles}
        tiles += [{"x": x, "y": y, "clear": "all"} for x in range(988, 1066) for y in range(980, 1054) if (x, y) not in done]
        print(cu.place(z, tiles, f"andar {z}: chao, bordas e rampas"))
        json.dump({f"{p[0]},{p[1]}": k for p, k in kind.items()}, open(os.path.join(HERE, f"cave_z{z}.json"), "w"))


if __name__ == "__main__":
    main()
