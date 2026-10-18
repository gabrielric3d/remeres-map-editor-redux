"""Weave, end of a forest-floor side on a grass tile: grass tile G has normal forest floor on
side a (the forest-floor edge runs along that side), the next tile along the edge (side b)
is a flipped forest-floor tile (grass owns there) and the diagonal a+b is normal forest
floor. The forest-floor edge would run straight to the tile end; its small corner a+b,
stacked on G, bends it. python weave4.py preview|apply"""
import json
import os
import sys

import forest
import rme_mcp
import weave
from weave2 import DIRS, corner, opposite

HERE = os.path.dirname(os.path.abspath(__file__))
FF = weave.FF


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    ground = weave.load_ground()
    flip = {tuple(p) for p in json.load(open(os.path.join(HERE, "weave_flip.json")))}

    def normal_ff(p):
        return ground.get(p) == FF and p not in flip

    adds = {}
    for G, g in ground.items():
        if g not in weave.GRASSES:
            continue
        for a, (ax, ay) in DIRS.items():
            if not normal_ff((G[0] + ax, G[1] + ay)):
                continue
            for b, (bx, by) in DIRS.items():
                if b in (a, opposite(a)):
                    continue
                if (G[0] + bx, G[1] + by) in flip and normal_ff((G[0] + ax + bx, G[1] + ay + by)):
                    piece = forest.BORDERS[FF][corner(a, b)]
                    if piece not in adds.setdefault(G, []):
                        adds[G].append(piece)
    print("tiles", len(adds))
    for p in sorted(adds):
        print(p, adds[p])
    if mode == "preview":
        return
    tiles = [{"x": p[0], "y": p[1], "add": ids} for p, ids in sorted(adds.items())]
    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "weave4", "version": "1"}})
    res = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": weave.Z, "tiles": tiles, "description": "cantos do forest floor no fim das retas"}}, 3)
    print(res["result"]["content"][0]["text"])


if __name__ == "__main__":
    main()
