"""Floor under the wall tiles whose uncovered part (bottom-right) is inside the room:
tiles where the wall turns and the room is on the south-east diagonal. python walls4.py preview|apply"""
import random
import sys

import house
import rme_mcp
import walls2

F = house.F


def inside_tiles():
    r = walls2.ring()
    out = []
    for (x, y) in r:
        vert = (x, y - 1) in r or (x, y + 1) in r
        horz = (x - 1, y) in r or (x + 1, y) in r
        if vert and horz and (x + 1, y + 1) in F:
            out.append((x, y))
    return sorted(out)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "preview"
    p = walls2.pieces(walls2.ring(), random.Random(11))
    tiles = [{"x": t[0], "y": t[1], "clear": "all", "ground": 405, "add": [p[t]]} for t in inside_tiles()]
    if mode == "preview":
        for s in tiles:
            print(s)
        return
    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "walls4", "version": "1"}})
    res = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": 7, "tiles": tiles, "description": "piso sob os cantos de dentro da parede"}}, 3)
    print(res["result"]["content"][0]["text"])


if __name__ == "__main__":
    main()
