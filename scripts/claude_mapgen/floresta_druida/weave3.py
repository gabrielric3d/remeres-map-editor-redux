"""Weave, concave case: a flipped tile F (grass owns its edge on side s) next to a normal
forest-floor tile N (direction d) where N+s is forest floor too. The grass edge on F runs
straight into the forest floor; the forest-floor small corner toward s+d, stacked OVER the
grass border on F, bends it. python weave3.py preview|apply"""
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
    adds = {}
    for F in flip:
        for d, (dx, dy) in DIRS.items():
            N = (F[0] + dx, F[1] + dy)
            if ground.get(N) != FF or N in flip:
                continue
            for s, (sx, sy) in DIRS.items():
                if s in (d, opposite(d)):
                    continue
                G1, NS = (F[0] + sx, F[1] + sy), (N[0] + sx, N[1] + sy)
                if ground.get(G1) in weave.GRASSES and ground.get(NS) == FF:
                    piece = forest.BORDERS[FF][corner(s, d)]
                    if piece not in adds.setdefault(F, []):
                        adds[F].append(piece)
    print("tiles", len(adds))
    for p in sorted(adds)[:10]:
        print(p, adds[p])
    if mode == "preview":
        return
    tiles = [{"x": p[0], "y": p[1], "add": ids} for p, ids in sorted(adds.items())]
    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "weave3", "version": "1"}})
    res = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": weave.Z, "tiles": tiles, "description": "cantos do forest floor nos bolsoes"}}, 3)
    print(res["result"]["content"][0]["text"])


if __name__ == "__main__":
    main()
