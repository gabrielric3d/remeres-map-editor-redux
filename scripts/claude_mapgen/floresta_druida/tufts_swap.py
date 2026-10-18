"""Swap the low grass tufts placed by tufts.py for 6216-6219, the less visible one the more
frequent (the mapper's preference): 6219 ~60, 6218 ~45, 6217 ~25, 6216 ~5. Same tiles, so the
clusters keep their shape; the tall grass (10914-10916) stays. Reads the map, no preview mode.
python tufts_swap.py

Run once by hand during the session (it was a one-off snippet); saved here as it is.
"""
import random, re, rme_mcp
OLD = set(range(44960, 44968)) | set(range(14845, 14853)) | {6216, 6217, 6218, 6219, 20177, 20178, 20263, 20264}
WEIGHTS = [(6219, 60), (6218, 45), (6217, 25), (6216, 5)]
rng = random.Random(6219)
def pick():
    r = rng.uniform(0, sum(w for _, w in WEIGHTS))
    for i, w in WEIGHTS:
        r -= w
        if r <= 0:
            return i
    return WEIGHTS[0][0]
rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "tufts2", "version": "1"}})
found = {}
for qx in (992, 1024):
    for qy in (992, 1024):
        t = rme_mcp.rpc("tools/call", {"name": "read_region", "arguments": {"x": qx, "y": qy, "z": 7, "width": 32, "height": 32, "raw": True}}, 3)["result"]["content"][0]["text"]
        stacks = t.split("Stacks")[1].split("Item legend")[0]
        for m in re.finditer(r"^(\d+),(\d+): ([\d ]+)$", stacks, re.M):
            ids = [int(i) for i in m.group(3).split()]
            old = [i for i in ids if i in OLD]
            if old:
                found[(int(m.group(1)), int(m.group(2)))] = old
from collections import Counter
tiles = []
new = Counter()
for p in sorted(found, key=lambda q: (abs(q[0] - 1024) + abs(q[1] - 1022))):
    n = pick(); new[n] += 1
    tiles.append({"x": p[0], "y": p[1], "remove": found[p], "add": [n]})
print("tiles", len(tiles), dict(new))
r = rme_mcp.rpc("tools/call", {"name": "place", "arguments": {"z": 7, "tiles": tiles, "description": "tufos 6216-6219 (menos visivel mais frequente)"}}, 3)
print(r["result"]["content"][0]["text"])
