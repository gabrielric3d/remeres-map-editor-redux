"""A stream across the druid forest, with a plank bridge where it cuts the trail.
python stream.py preview|apply"""
import json
import math
import os
import random
import re
import sys

import forest
import house
import rme_mcp
import weave

HERE = os.path.dirname(os.path.abspath(__file__))
Z = 7
rng = random.Random(1040)
X0, Y0, W, H = forest.X0, forest.Y0, forest.W, forest.H
SEA = [4608] * 6 + [4609, 4610, 4611, 4612, 4613, 4614, 4615, 4616, 4617, 4618, 4619]
BANK = dict(zip(forest.EDGE_NAMES, [4644, 4645, 4646, 4647, 4652, 4653, 4654, 4655, 4648, 4649, 4651, 4650]))
DECK = [5768, 5769, 5770]
REEDS = [2773, 2775, 2779, 2773, 2779]
CATTAILS = [2774, 2776]
LILIES = [2755, 2757, 2755, 2756, 2758]
STONES_SMALL = [3329, 3333]
STONES_BIG = [3330, 3331, 3332]
width_noise = [forest.Noise(1101, 7), forest.Noise(1102, 3)]
top_noise = [forest.Noise(1111, 3.5), forest.Noise(1112, 1.7)]
bottom_noise = [forest.Noise(1121, 3.5), forest.Noise(1122, 1.7)]


def raw_centre(x):
    t = x - X0
    return Y0 + 36 + t * (24 / 63) + 2.2 * math.sin(t / 6.5 + 0.8) + 1.0 * math.sin(t / 2.9 + 2.0)


BRIDGE_X = 1025.0


def centre(x):
    # Flat where it meets the trail, so the bridge crosses it square.
    w = math.exp(-((x - BRIDGE_X) / 3.5) ** 2)
    return (1 - w) * raw_centre(x) + w * raw_centre(BRIDGE_X)


def radius(x):
    return 0.25 + 0.5 * forest.fbm(width_noise, x, 0)


def edges(x):
    # Each bank wanders on its own, so neither runs straight for long.
    r = radius(x)
    return r + 0.9 * (forest.fbm(top_noise, x, 0) - 0.5), r + 0.9 * (forest.fbm(bottom_noise, x, 0) - 0.5)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    ground = weave.load_ground()
    forest.Z_ORDER["floor"] = -1
    flip = {tuple(p) for p in json.load(open(os.path.join(HERE, "weave_flip.json")))}

    # ---- water ---------------------------------------------------------------
    # Each bank is COMPOSED the way the mapper does it: a sequence of short patterns laid
    # over the centre line, never the same pattern twice in a row, so no stretch repeats
    # the one before and no side runs straight for long. Offsets go out of the water into
    # the land (0 = the plain band, -1 = a point of land pushing into the stream).
    PATTERNS = {
        "flat": lambda r: [0] * r.randint(1, 2),
        "notch": lambda r: [1],
        "small bay": lambda r: [1, 2, 1],
        "bay": lambda r: r.choice(([1, 2, 2, 1], [1, 2, 2, 2, 1], [1, 1, 2, 1])),
        "point": lambda r: r.choice(([-1], [-1, -1])),
        "wiggle": lambda r: [1, 0, 1],
    }

    def compose(seed):
        r = random.Random(seed)
        out, last = [], None
        while len(out) < W + 8:
            weights = {"flat": 3, "notch": 0.8, "small bay": 1.4, "bay": 1.2, "point": 1.0, "wiggle": 0.4}
            options = [k for k in PATTERNS if k != last and not (last == "bay" and k == "small bay") and not (last == "small bay" and k == "bay")]
            kind = r.choices(options, [weights[k] for k in options])[0]
            last = kind
            out += PATTERNS[kind](r)
        return {X0 - 2 + i: o for i, o in enumerate(out)}

    top_off, bottom_off = compose(81), compose(82)
    bx = round(BRIDGE_X)
    bridge_zone = set(range(bx - 2, bx + 3))
    path_tiles = {p for p, g in ground.items() if g == "dirt"}
    keep_dry = set(house.F) | house.BED_A | house.BED_B | house.YARD
    keep_dry |= {(x + dx, y + dy) for (x, y) in keep_dry for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2)}
    keep_dry |= {p for p in path_tiles if p[0] not in bridge_zone}
    spans = {}
    for x in range(X0, X0 + W):
        rc = round(centre(x))
        ot, ob = (0, 0) if x in bridge_zone else (top_off[x], bottom_off[x])
        if ot + ob > 2:  # bays on both banks at once would make a pond
            if ot >= ob:
                ob = max(0, 2 - ot)
            else:
                ot = max(0, 2 - ob)
        if x in bridge_zone:
            ob = 1  # two rows under the bridge
        a, b = rc - ot, rc + ob
        if a > b:  # a point of land never closes the stream
            a = b
        # Only into free land: pull a bank back if it would reach the trail or the house.
        while a < rc and any((x, y) in keep_dry for y in range(a, rc + 1)):
            a += 1
        while b > rc and any((x, y) in keep_dry for y in range(rc + 1, b + 1)):
            b -= 1
        if x - 1 in spans:  # share a row with the column before: the stream never breaks
            pa, pb = spans[x - 1]
            if a > pb:
                a = pb
            if b < pa:
                b = pa
        spans[x] = (a, b)
    water = {(x, y) for x, (a, b) in spans.items() for y in range(a, b + 1) if (x, y) in ground}
    # No diagonal-only links, no 1-tile land strips between two waters.
    changed = True
    while changed:
        changed = False
        for (x, y) in list(water):
            for dx, dy in ((1, 1), (1, -1)):
                q = (x + dx, y + dy)
                if q in water and (x + dx, y) not in water and (x, y + dy) not in water:
                    water.add((x + dx, y) if abs(centre(x + dx) - y) < abs(centre(x) - (y + dy)) else (x, y + dy))
                    changed = True
        for p in list(ground):
            if p in water:
                continue
            x, y = p
            if ((x - 1, y) in water and (x + 1, y) in water) or ((x, y - 1) in water and (x, y + 1) in water):
                water.add(p)
                changed = True
    water = {p for p in water if p in ground}
    # Last pass on the final shape: no bank runs straight for more than 2 tiles, north,
    # south, east or west (the mapper's rule). Each longer run gets a 1-2 tile tooth of
    # water pushed into the land, so the bank pieces turn into corners and diagonals.
    bridge_zone = set(range(round(BRIDGE_X) - 2, round(BRIDGE_X) + 3))  # square around the deck

    def makes_strip(t):
        # Land left 1 tile wide between this new water and other water.
        x, y = t
        for ax, ay in ((1, 0), (0, 1)):
            for sgn in (1, -1):
                mid, far = (x + sgn * ax, y + sgn * ay), (x + 2 * sgn * ax, y + 2 * sgn * ay)
                if mid not in water and far in water:
                    return True
        return False

    for _ in range(60):
        added = False
        for d in ((0, -1), (0, 1), (-1, 0), (1, 0)):
            along = (1, 0) if d[1] else (0, 1)
            edge = sorted((p for p in water if (p[0] + d[0], p[1] + d[1]) not in water and (p[0] + d[0], p[1] + d[1]) in ground),
                          key=lambda p: (p[1], p[0]) if d[1] else (p[0], p[1]))
            run = []
            for p in edge + [None]:
                if p is not None and run and (p[0] - run[-1][0], p[1] - run[-1][1]) == along:
                    run.append(p)
                    continue
                if len(run) >= 3:
                    inner = list(range(1, len(run) - 1))
                    rng.shuffle(inner)
                    # Inner teeth first; a tooth at an end shortens the run to 2.
                    for i in [i for i in inner + [0, len(run) - 1] if run[i][0] not in bridge_zone]:
                        t = (run[i][0] + d[0], run[i][1] + d[1])
                        if t in ground and t not in water and not makes_strip(t):
                            water.add(t)
                            added = True
                            break
                run = [p] if p is not None else []
        if not added:
            break

    forbidden = set(house.F) | house.BED_A | house.BED_B | house.YARD | {(house.CAULDRON_AT[0] + dx, house.CAULDRON_AT[1] + dy) for dx, dy, _ in house.CAULDRON}
    near = {(x + dx, y + dy) for (x, y) in water for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2)}
    assert not (near & forbidden), "stream too close to the house"

    # ---- bridge where the stream cuts the trail -------------------------------
    path_cols = sorted({x for (x, y) in water if ground[(x, y)] == "dirt"})
    assert path_cols, "the stream does not cross the trail"
    bridge_cols = [round(BRIDGE_X) - 1, round(BRIDGE_X)]

    for p in water:
        ground[p] = "water"
    forest.Z_ORDER["water"] = -5
    bank_tiles = {(x + dx, y + dy) for (x, y) in water for dx, dy, _ in forest.NEIGHBOURS} - water
    bank_tiles = {p for p in bank_tiles if p in ground}

    deck = set()
    for c in bridge_cols:
        rows = sorted(y for (x, y) in water if x == c)
        if rows:
            deck |= {(c, y) for y in range(rows[0] - 1, rows[-1] + 2)}

    # The trail runs into both ends of the bridge: dirt on the two tiles past each end.
    extra_dirt = set()
    for c in bridge_cols:
        rows = sorted(y for (x, y) in water if x == c)
        extra_dirt |= {(c, rows[0] - 1), (c, rows[0] - 2), (c, rows[-1] + 1), (c, rows[-1] + 2)}
    for p in extra_dirt:
        ground[p] = "dirt"
    ring = {(x + dx, y + dy) for (x, y) in extra_dirt for dx, dy, _ in forest.NEIGHBOURS} - water - extra_dirt - bank_tiles
    ring = {p for p in ring if p in ground}

    land = {p: g for p, g in ground.items() if g != "water"}
    land_borders = weave.borders(land, lambda p: 3190 if p in flip else forest.Z_ORDER[land[p]])

    def bank_edges(p):
        bits = 0
        for dx, dy, bit in forest.NEIGHBOURS:
            if (p[0] + dx, p[1] + dy) in water:
                bits |= bit
        return forest.border_table(bits)

    # By hand where the shape still runs straight: 3+ bank tiles in a row with the same
    # single straight piece get a small corner stacked on the inner ones (alternating
    # sides), a little spit of bank into the water that breaks the line without a seam.
    STACK = {"s": ("cse", "csw"), "n": ("cne", "cnw"), "e": ("cne", "cse"), "w": ("cnw", "csw")}
    extra_corner = {}
    straight = {p: bank_edges(p)[0] for p in bank_tiles if len(bank_edges(p)) == 1 and bank_edges(p)[0] in STACK and p not in deck}
    for side, (c1, c2) in STACK.items():
        along = (1, 0) if side in ("n", "s") else (0, 1)
        for p in straight:
            if straight[p] != side or straight.get((p[0] - along[0], p[1] - along[1])) == side:
                continue
            run = [p]
            while straight.get((run[-1][0] + along[0], run[-1][1] + along[1])) == side:
                run.append((run[-1][0] + along[0], run[-1][1] + along[1]))
            # (stacked corners over a straight piece leave a loose sliver of bank in
            # the water: never do it, the shape of the water breaks the runs instead)

    def bank_pieces(p):
        return [BANK[e] for e in bank_edges(p)] + ([BANK[extra_corner[p]]] if p in extra_corner else [])

    if mode == "preview":
        sym = {"grass": ".", "dirt": "#", "lush grass": ",", "forest floor": "f", "dark grass": "d", "floor": "_", "dark dirt": "x", "water": "~"}
        for y in range(Y0, Y0 + H):
            print("".join("B" if (x, y) in deck else sym[ground[(x, y)]] for x in range(X0, X0 + W)))
        print("water", len(water), "banks", len(bank_tiles), "bridge cols", bridge_cols, "deck", len(deck))
        return

    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "stream", "version": "1"}})

    def place(tiles, label):
        res = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": Z, "tiles": tiles, "description": label}}, 3)
        print(label, "->", res["result"]["content"][0]["text"])

    # 0. Leftovers: any sea ground on the map outside this stream (from an older version
    #    that an undo did not take back) goes back to its land ground and borders.
    sea_ids = set(SEA)
    leftover = set()
    for qx in (X0, X0 + 32):
        for qy in (Y0, Y0 + 32):
            res = rme_mcp.rpc("tools/call", {"name": "read_region", "arguments": {"x": qx, "y": qy, "z": Z, "width": 32, "height": 32, "raw": True}}, 3)
            text = res["result"]["content"][0]["text"]
            legend = {m.group(1): int(m.group(2)) for m in re.finditer(r"^\s+(\S) = (\d+) ", text.split("Ground legend:")[1].split("Stacks")[0], re.M)}
            rows = text.split("Ground grid")[1].splitlines()[1:33]
            for dy, row in enumerate(rows):
                for dx, ch in enumerate(row):
                    p = (qx + dx, qy + dy)
                    if (legend.get(ch) in sea_ids and p not in water) or (legend.get(ch) in DECK and p not in deck):
                        leftover.add(p)
            # Bank pieces off the current banks, water plants/stones off the water.
            bank_ids = set(BANK.values())
            wet_ids = set(LILIES) | set(STONES_SMALL) | set(STONES_BIG)
            stacks = text.split("Stacks")[1].split("Item legend")[0] if "Stacks" in text else ""
            for m in re.finditer(r"^(\d+),(\d+): ([\d ]+)$", stacks, re.M):
                p, ids = (int(m.group(1)), int(m.group(2))), {int(i) for i in m.group(3).split()}
                if (ids & bank_ids and p not in bank_tiles) or (ids & wet_ids and p not in water):
                    leftover.add(p)
    leftover -= water | deck
    print("leftover tiles", len(leftover))
    if mode == "fix":
        place([{"x": p[0], "y": p[1], "clear": "all", "ground": forest.pick(land[p]),
                **({"add": land_borders.get(p, []) + (bank_pieces(p) if p in bank_tiles else [])} if land_borders.get(p) or p in bank_tiles else {})}
               for p in sorted(leftover)], "limpa restos de versoes antigas do riacho")
        return

    # 1. Water and banks, flowing from west to east.
    tiles = []
    order = sorted(water | bank_tiles | leftover, key=lambda p: (p[0] + (p[1] - centre(p[0])) * 0.3, p not in water))
    for p in order:
        if p in water:
            tiles.append({"x": p[0], "y": p[1], "clear": "all", "ground": rng.choice(SEA)})
        elif p in extra_dirt:
            # Bridge ends: no bank piece over the planks, they land on the shore.
            tiles.append({"x": p[0], "y": p[1], "clear": "all", "ground": forest.pick("dirt"), "add": land_borders.get(p, []) + ([] if p in deck else bank_pieces(p))})
        else:
            # Bank (or leftover) tile: its own land ground back, its land borders, the bank.
            tiles.append({"x": p[0], "y": p[1], "clear": "all", "ground": forest.pick(land[p]) if land.get(p) not in ("floor",) else 405,
                          **({"add": land_borders.get(p, []) + bank_pieces(p)} if land_borders.get(p) or bank_pieces(p) else {})})
    for p in sorted(extra_dirt - bank_tiles):
        tiles.append({"x": p[0], "y": p[1], "clear": "all", "ground": forest.pick("dirt"), **({"add": land_borders[p]} if p in land_borders else {})})
    for p in sorted(ring):
        tiles.append({"x": p[0], "y": p[1], "clear": "borders", **({"add": land_borders[p]} if p in land_borders else {})})
    place(tiles, "riacho: agua e barranco")
    # 2. The bridge: planks from bank to bank, two tiles wide.
    place([{"x": p[0], "y": p[1], "add": [rng.choice(DECK)]} for p in sorted(deck, key=lambda q: (q[1], q[0]))], "ponte de tabuas")

    # 3. Riverside clusters: reeds on the banks, lilies and stones in the water,
    #    spaced unevenly, same kind never repeated right next to itself.
    deco = {}
    free_bank = [p for p in bank_tiles if p not in deck and land.get(p) not in ("floor", "dark dirt") and abs(p[0] - bridge_cols[0]) > 2]
    free_water = [p for p in water if p not in deck and abs(p[0] - bridge_cols[0]) > 2]
    centres, used = [], []
    pool = [("reeds", free_bank), ("reeds", free_bank), ("cattails", free_bank), ("lilies", free_water), ("stones", free_water)]
    cands = [(k, p) for k, lst in pool for p in lst]
    rng.shuffle(cands)
    for kind, c in cands:
        r = rng.uniform(3.0, 7.5)
        if any(math.hypot(c[0] - q[0], c[1] - q[1]) < max(r, rq) for q, rq in centres):
            continue
        if any(k == kind and math.hypot(c[0] - q[0], c[1] - q[1]) < 9 for k, q in used):
            continue
        centres.append((c, r))
        used.append((kind, c))
        spots = sorted((p for p in (free_bank if kind in ("reeds", "cattails") else free_water) if max(abs(p[0] - c[0]), abs(p[1] - c[1])) <= 2),
                       key=lambda p: math.hypot(p[0] - c[0], p[1] - c[1]) + rng.random())
        n = {"reeds": rng.randint(3, 5), "cattails": rng.randint(2, 3), "lilies": rng.randint(2, 4), "stones": rng.randint(2, 3)}[kind]
        for i, p in enumerate(spots[:n]):
            if p in deco:
                continue
            if kind == "reeds":
                deco[p] = rng.choice(REEDS if rng.random() < 0.8 else CATTAILS)
            elif kind == "cattails":
                deco[p] = rng.choice(CATTAILS if rng.random() < 0.7 else REEDS)
            elif kind == "lilies":
                deco[p] = rng.choice(LILIES)
            else:
                deco[p] = rng.choice(STONES_BIG) if i == 0 else rng.choice(STONES_SMALL)
    print("riverside clusters", len(used))
    place([{"x": p[0], "y": p[1], "add": [i]} for p, i in sorted(deco.items(), key=lambda kv: kv[0][0])], "beira do riacho")
    json.dump({"water": sorted(water), "deck": sorted(deck), "banks": sorted(bank_tiles)}, open(os.path.join(HERE, "stream.json"), "w"))


if __name__ == "__main__":
    main()
