"""Close the weave with CORNERS where the border owner switches. python weave2.py preview|apply

Switch point: flipped forest-floor tile F (grass owns its edge) next to a normal one N
(forest floor owns), both with grass G1 = F+s / G2 = N+s on the same side s.
  - N gets the grass small corner toward G1 (the grass line curves round instead of ending);
  - G1 loses the forest-floor corner that N threw on it;
  - G2 gets the grass small corner stacked OVER the forest-floor edge, on the side of G1.
"""
import json
import os
import sys

import forest
import rme_mcp
import weave

HERE = os.path.dirname(os.path.abspath(__file__))
FF = weave.FF
DIRS = {"n": (0, -1), "s": (0, 1), "w": (-1, 0), "e": (1, 0)}


def corner(a, b):
    # corner piece name for the diagonal a+b (a, b are orthogonal direction names)
    v = "n" if "n" in (a, b) else "s"
    h = "w" if "w" in (a, b) else "e"
    return "c" + v + h


def opposite(d):
    return {"n": "s", "s": "n", "w": "e", "e": "w"}[d]


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    ground = weave.load_ground()
    forest.Z_ORDER["floor"] = -1
    flip = {tuple(p) for p in json.load(open(os.path.join(HERE, "weave_flip.json")))}
    after = weave.borders(ground, lambda p: 3190 if p in flip else forest.Z_ORDER[ground[p]])
    new = {p: list(v) for p, v in after.items()}

    def add(p, piece):
        lst = new.setdefault(p, [])
        if piece not in lst:
            lst.append(piece)

    switches = 0
    for F in flip:
        for d, (dx, dy) in DIRS.items():
            N = (F[0] + dx, F[1] + dy)
            if ground.get(N) != FF or N in flip:
                continue
            for s, (sx, sy) in DIRS.items():
                if s in (d, opposite(d)):
                    continue
                G1, G2 = (F[0] + sx, F[1] + sy), (N[0] + sx, N[1] + sy)
                g1, g2 = ground.get(G1), ground.get(G2)
                if g1 not in weave.GRASSES or g2 not in weave.GRASSES:
                    continue
                switches += 1
                back = opposite(d)  # from N (or G2) toward F's column
                add(N, forest.BORDERS[g1][corner(s, back)])
                ff_corner = forest.BORDERS[FF][corner(opposite(s), d)]
                if ff_corner in new.get(G1, []):
                    new[G1].remove(ff_corner)
                add(G2, forest.BORDERS[g1][corner(opposite(s), back)])

    changed = sorted(p for p in set(after) | set(new) if after.get(p) != new.get(p))
    print("switch points", switches, "tiles changed", len(changed))
    if mode == "preview":
        for p in changed[:12]:
            print(p, ground[p], after.get(p), "->", new.get(p))
        return
    tiles = [{"x": p[0], "y": p[1], "clear": "borders", **({"add": new[p]} if new.get(p) else {})} for p in changed]
    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "weave2", "version": "1"}})
    res = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": weave.Z, "tiles": tiles, "description": "cantos do entrelacamento"}}, 3)
    print(res["result"]["content"][0]["text"])


if __name__ == "__main__":
    main()
