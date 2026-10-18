import json, sys, rme
items = [int(a) for a in sys.argv[1].split(",")]
base = int(sys.argv[2]) if len(sys.argv) > 2 else 351
X0, Y0, Z = 1900, 1900, 7
tiles = []
cols = 10
for dy in range(-1, (len(items)+cols-1)//cols*3):
    for dx in range(-1, cols*3):
        tiles.append({"x": X0+dx, "y": Y0+dy, "ground": base})
idx = {}
for i, it in enumerate(items):
    x = X0 + (i % cols) * 3 + 1
    y = Y0 + (i // cols) * 3 + 1
    idx[(x, y)] = it
out = []
for t in tiles:
    k = (t["x"], t["y"])
    if k in idx:
        it = idx[k]
        try:
            out.append(dict(t, add=[it]))
        except Exception:
            out.append(t)
    else:
        out.append(t)
bad = []
try:
    print(rme.call("place", {"z": Z, "tiles": out, "description": "preview"})[0][:200])
except RuntimeError as e:
    print(e)
for (x, y), it in idx.items():
    print(it, "->", x, y)
