"""Ring tiles that face outside get the outside ground back (floor only where the wall's
uncovered part faces the room). python walls3.py preview|apply"""
import json
import os
import random
import sys

import forest
import house
import rme_mcp
import walls2

HERE = os.path.dirname(os.path.abspath(__file__))
F = house.F


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    data = json.load(open(os.path.join(HERE, "forest_ground.json")))
    ground = {tuple(map(int, k.split(","))): v for k, v in data["ground"].items()}
    for p in F:
        ground[p] = "floor"
    for p in house.BED_A | house.BED_B | house.YARD:
        ground[p] = "dark dirt"
    for p in house.PATH_EXT:
        ground[p] = "dirt"
    forest.Z_ORDER["floor"] = -1
    borders = forest.borders_for(ground)

    r = walls2.ring()
    p = walls2.pieces(r, random.Random(11))
    # Wall tiles keep the outside ground; floor only on inner junctions the room wraps (S and E).
    outside = [t for t in r if not ((t[0] + 1, t[1]) in F and (t[0], t[1] + 1) in F)]
    tiles = []
    for t in sorted(outside):
        spec = {"x": t[0], "y": t[1], "clear": "all", "ground": forest.pick(ground[t])}
        spec["add"] = list(borders.get(t, [])) + [p[t]]
        tiles.append(spec)
    if mode == "preview":
        for s in tiles:
            print(s, ground[(s["x"], s["y"])])
        return
    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "walls3", "version": "1"}})
    res = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": 7, "tiles": tiles, "description": "chao de fora sob as paredes leste/sul"}}, 3)
    print(res["result"]["content"][0]["text"])


if __name__ == "__main__":
    main()
