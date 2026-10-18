"""Reshape the dirt trail (house door -> bridge -> south edge) so its sides never run straight
for more than 2 tiles: each side steps a column every 1-2 rows, rhythm and direction varied.
python trail.py preview|apply"""
import json
import math
import os
import random
import re
import sys

import forest
import house
import rme_mcp
import stream
import weave

HERE = os.path.dirname(os.path.abspath(__file__))
Z = 7
rng = random.Random(2468)
X0, Y0, W, H = forest.X0, forest.Y0, forest.W, forest.H
DIRT = "dirt"


def read_grounds(x0, y0, w, h):
    """{(x, y): ground id} straight from the map."""
    out = {}
    for qx in range(x0, x0 + w, 32):
        for qy in range(y0, y0 + h, 32):
            ww, hh = min(32, x0 + w - qx), min(32, y0 + h - qy)
            res = rme_mcp.rpc("tools/call", {"name": "read_region", "arguments": {"x": qx, "y": qy, "z": Z, "width": ww, "height": hh, "raw": True}}, 3)
            text = res["result"]["content"][0]["text"]
            legend = {m.group(1): int(m.group(2)) for m in re.finditer(r"^\s+(\S) = (\d+) ", text.split("Ground legend:")[1].split("Stacks")[0], re.M)}
            rows = text.split("Ground grid")[1].splitlines()[1:hh + 1]
            for dy, row in enumerate(rows):
                for dx, ch in enumerate(row):
                    if ch in legend:
                        out[(qx + dx, qy + dy)] = legend[ch]
    return out


def side(seed, target, rows, fixed, outward=-1):
    """One side of the trail, row by row: never the same column more than 2 rows running,
    steps of one column, drifting toward target(y)."""
    r = random.Random(seed)
    out, run, last_step = {}, 0, 0
    prev = None
    for y in rows:
        if y in fixed:
            v = fixed[y]
        elif prev is None:
            v = round(target(y))
        else:
            t = target(y)
            must = run >= 2
            if must or r.random() < 0.45:
                toward = 1 if t > prev else -1 if t < prev else r.choice((-1, 1))
                step = toward if (abs(t - prev) > 0.6 or r.random() < 0.6) else -toward
                # No 1-row spikes: turning back needs 2 rows at the same column first.
                if last_step and step == -last_step and run < 2:
                    step = last_step if must else 0
                v = prev + step
            else:
                v = prev
            # Look ahead to the next fixed row (door, bridge head): head for it early, one
            # column per row at most, so the side arrives in steps instead of a jump.
            ahead = [f for f in fixed if 0 < f - y <= 4]
            if ahead:
                yf = min(ahead)
                gap = fixed[yf] - v
                if abs(gap) > yf - y - 1:
                    v = prev + (1 if fixed[yf] > prev else -1 if fixed[yf] < prev else 0)
        if prev is not None and v != prev:
            last_step = 1 if v > prev else -1
        run = run + 1 if v == prev else 1
        out[y] = v
        prev = v
    return out


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "trail", "version": "1"}})
    ground = weave.load_ground()
    sdata = json.load(open(os.path.join(HERE, "stream.json")))
    water = {tuple(p) for p in sdata["water"]}
    deck = {tuple(p) for p in sdata["deck"]}
    banks = {tuple(p) for p in sdata["banks"]}
    for p in water:
        ground[p] = "water"
    # Where the dirt is today (from the map itself).
    ids = read_grounds(1008, 1024, 32, 32)
    dirt_ids = {i for i, _ in forest.GROUNDS[DIRT]}
    old_dirt = {p for p, i in ids.items() if i in dirt_ids}
    # The map is the truth: tiles the ground file still calls dirt but are not dirt now.
    name_of = {i: name for name, items in forest.GROUNDS.items() for i, _ in items}
    for p, i in ids.items():
        if ground.get(p) == DIRT and i not in dirt_ids and i in name_of:
            ground[p] = name_of[i]
    for p in old_dirt:
        ground[p] = DIRT

    deck_rows = sorted({y for (x, y) in deck})
    top_deck, bottom_deck = deck_rows[0], deck_rows[-1]
    cols = sorted({x for (x, y) in deck})

    # Centre line: door -> bridge (north part), bridge -> south edge (south part).
    def centre_north(y):
        t = (y - 1026) / (top_deck - 1 - 1026)
        return 1024 + (cols[0] + 0.5 - 1024) * t + 1.6 * math.sin(t * math.pi)

    def centre_south(y):
        t = (y - (bottom_deck + 1)) / (Y0 + H - 1 - (bottom_deck + 1))
        return cols[0] + 0.5 - 4.0 * math.sin(t * math.pi * 0.9) + 2.2 * math.sin(t * math.pi * 2.6) + 1.0 * t

    def build(attempt):
        north_rows = list(range(1026, top_deck))
        south_rows = list(range(bottom_deck + 1, Y0 + H))
        trail = set()
        for rows, centre, seed in ((north_rows, centre_north, 11 + 1000 * attempt), (south_rows, centre_south, 12 + 1000 * attempt)):
            north = rows is north_rows
            fixed_l, fixed_r = {}, {}
            if north:
                fixed_l.update({1026: 1023, 1027: 1023})
                fixed_r.update({1026: 1025, 1027: 1025})
            # Bridge head: the row against the planks opens to 4 (one past each deck column),
            # and the rows before it flare toward that gradually (a funnel, not a bar).
            head = rows[-1] if north else rows[0]
            fixed_l[head], fixed_r[head] = cols[0] - 1, cols[-1] + 1

            def near_head(y):
                return abs(y - head) <= 3

            wl, wr = forest.Noise(seed * 7 + 1, 2.6), forest.Noise(seed * 7 + 2, 2.1)

            def target_left(y):
                return cols[0] - 1 if near_head(y) else centre(y) - 1.0 - 1.0 * wl.at(0, y)

            left = side(seed, target_left, rows, fixed_l)

            def target_right(y):
                t = cols[-1] + 1 if near_head(y) else centre(y) + 1.0 + 1.0 * wr.at(0, y)
                return min(left[y] + 3, max(left[y] + 1, t))  # 2 to 4 wide, by the target

            right = side(seed + 100, target_right, rows, fixed_r)
            for y in rows:
                a2, b2 = left[y], max(right[y], left[y] + 1)
                trail |= {(x, y) for x in range(a2, b2 + 1)}
        return trail

    def faults(t):
        """Straight runs of 3+ along any side (the bridge planks count as trail) and 1-row spikes."""
        dirt = set(t) | deck
        bad = 0
        for d in ((0, -1), (0, 1), (-1, 0), (1, 0)):
            along = (1, 0) if d[1] else (0, 1)
            edge = {p for p in t if (p[0] + d[0], p[1] + d[1]) not in dirt and (p[0] + d[0], p[1] + d[1]) in ground and p[1] >= 1027}
            for p in edge:
                if (p[0] - along[0], p[1] - along[1]) in edge:
                    continue
                n = 1
                while (p[0] + n * along[0], p[1] + n * along[1]) in edge:
                    n += 1
                bad += n >= 3
        lo, hi = {}, {}
        for (x, y) in t:
            lo[y], hi[y] = min(lo.get(y, x), x), max(hi.get(y, x), x)
        for y in lo:
            if y - 1 in lo and y + 1 in lo:
                bad += (lo[y] < lo[y - 1] and lo[y] < lo[y + 1]) or (hi[y] > hi[y - 1] and hi[y] > hi[y + 1])
        return bad

    for attempt in range(300):
        trail = build(attempt)
        if faults(trail) == 0:
            break
    print("attempt", attempt, "faults", faults(trail))
    # Stay off the house, the yard and the water (the bridge ends are the only contact).
    blocked = set(house.F) | house.BED_A | house.BED_B | house.YARD | water | deck
    blocked |= {(house.CAULDRON_AT[0] + dx, house.CAULDRON_AT[1] + dy) for dx, dy, _ in house.CAULDRON}
    trail = {p for p in trail if p in ground and p not in blocked}
    # The wall ring stays: dirt only south of the house (door row and below).
    trail = {p for p in trail if p[1] >= 1026}

    added = trail - old_dirt
    removed = old_dirt - trail - deck
    new_ground = dict(ground)
    for p in trail:
        new_ground[p] = DIRT
    for p in removed:
        near = [ground.get((p[0] + dx, p[1] + dy)) for dx in (-1, 0, 1) for dy in (-1, 0, 1)]
        near = [g for g in near if g not in (None, DIRT, "water", "floor", "dark dirt")]
        new_ground[p] = max(set(near), key=near.count) if near else "grass"

    if mode == "preview":
        sym = {"grass": ".", DIRT: "#", "lush grass": ",", "forest floor": "f", "dark grass": "d", "floor": "_", "dark dirt": "x", "water": "~"}
        for y in range(1022, Y0 + H):
            print(y, "".join("B" if (x, y) in deck else sym.get(new_ground.get((x, y)), "?") for x in range(1008, 1042)))
        print("added", len(added), "removed", len(removed))
        return

    # Borders for every tile that changed and its neighbours (weave-aware, water ignored).
    flip = {tuple(p) for p in json.load(open(os.path.join(HERE, "weave_flip.json")))}
    forest.Z_ORDER["floor"] = -1
    forest.Z_ORDER["water"] = -5
    land = {p: g for p, g in new_ground.items() if g != "water"}
    land_borders = weave.borders(land, lambda p: 3190 if p in flip else forest.Z_ORDER[land[p]])

    def bank_pieces(p):
        bits = 0
        for dx, dy, bit in forest.NEIGHBOURS:
            if (p[0] + dx, p[1] + dy) in water:
                bits |= bit
        return [stream.BANK[e] for e in forest.border_table(bits)] if bits else []

    changed = added | removed
    around = {(x + dx, y + dy) for (x, y) in changed for dx, dy, _ in forest.NEIGHBOURS} - changed - deck - water
    tiles = []
    for p in sorted(changed, key=lambda q: (q[1], q[0])):
        add = land_borders.get(p, []) + (bank_pieces(p) if p in banks else [])
        tiles.append({"x": p[0], "y": p[1], "clear": "all", "ground": forest.pick(new_ground[p]), **({"add": add} if add else {})})
    for p in sorted(around, key=lambda q: (q[1], q[0])):
        if p not in land:
            continue
        add = land_borders.get(p, []) + (bank_pieces(p) if p in banks else [])
        tiles.append({"x": p[0], "y": p[1], "clear": "borders", **({"add": add} if add else {})})
    res = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": Z, "tiles": tiles, "description": "estrada de terra com bordas quebradas"}}, 3)
    print(res["result"]["content"][0]["text"])


if __name__ == "__main__":
    main()
