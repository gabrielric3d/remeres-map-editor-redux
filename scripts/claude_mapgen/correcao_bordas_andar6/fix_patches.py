"""Redo the border of a grey stone patch on floor 6 the way the mapper does it.

Usage: python fix_patches.py x0 y0 x1 y1 [apply]
(x0,y0)-(x1,y1) = bounding box of the grey patch. Without "apply" it only prints the plan.

Technique (read from the mapper's own version at x 4102-4108, y 3899-3903):
  west/north: grey border 250 over the mud; east: the mud's THICK border 46 over the grey
  edge; south: the mud's normal border 44 over the grey bottom. One owner per stretch.
"""
import json
import re
import sys

import rme_mcp

Z = 6
GREY = 45352
GREY_EDGES = ["e", "cse", "dse", "cne", "dne"]  # soft edge on the west side only
THICK_EDGES = ["n", "e", "dne", "cne", "cse", "dnw"]  # mud higher on the north and east
NORMAL_EDGES = ["s", "csw", "dsw", "dse"]  # grey tiles with mud to the south
SET_IDS = {"250": range(45353, 45364), "46": range(7079, 7091), "44": range(7067, 7079)}


def call(tool, args):
    reply = rme_mcp.rpc("tools/call", {"name": tool, "arguments": args}, 9)
    result = reply.get("result", {})
    return "\n".join(b.get("text", "") for b in result.get("content", []) if b.get("type") == "text")


def lua(code):
    return call("run_lua", {"code": code, "description": "read"})


def ready(text):
    m = re.search(r"Ready for place[^\n]*:\n(\[.*\])", text, re.S)
    return json.loads(m.group(1)) if m else []


def main():
    x0, y0, x1, y1 = map(int, sys.argv[1:5])
    apply = len(sys.argv) > 5 and sys.argv[5] == "apply"
    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "fix", "version": "1"}})

    # Tiles of the box (+1) as "x,y,ground,item ids".
    dump = lua(f"""
for y={y0 - 1},{y1 + 1} do for x={x0 - 1},{x1 + 1} do
  local t = app.map:getTile(x,y,{Z})
  if t and t.ground then
    local ids = {{}}
    for _, it in ipairs(t.items) do ids[#ids+1] = it.id end
    print(x..","..y..","..t.ground.id..","..table.concat(ids, " "))
  end
end end""")
    tiles = {}
    for line in dump.splitlines():
        parts = line.split(",")
        if len(parts) == 4:
            tiles[(int(parts[0]), int(parts[1]))] = (int(parts[2]), [int(i) for i in parts[3].split()] if parts[3] else [])
    grey = {p for p, (g, _) in tiles.items() if g == GREY}
    # Notches: mud with grey on both sides (W+E or N+S) inside the box become grey.
    notches = [p for p, (g, _) in tiles.items() if g != GREY and x0 <= p[0] <= x1 and y0 <= p[1] <= y1 and
               (((p[0] - 1, p[1]) in grey and (p[0] + 1, p[1]) in grey) or ((p[0], p[1] - 1) in grey and (p[0], p[1] + 1) in grey))]
    grey |= set(notches)
    print("patch tiles:", len(grey), "notches filled:", notches)

    strip = []
    for p, (g, items) in tiles.items():
        remove = [i for i in items if i in SET_IDS["250"] or i in SET_IDS["46"] or (p in grey and i in SET_IDS["44"])]
        spec = {"x": p[0], "y": p[1]}
        if remove:
            spec["remove"] = remove
        if p in notches:
            spec["ground"] = GREY
        if len(spec) > 2:
            strip.append(spec)
    points = [[x, y] for (x, y) in sorted(grey)]
    plans = [
        ("250", {"border_id": "250", "points": points, "z": Z, "edges": GREY_EDGES}),
        ("46", {"border_id": "46", "points": points, "z": Z, "align": "inner", "edges": THICK_EDGES}),
        ("44", {"border_id": "44", "points": points, "z": Z, "align": "inner", "edges": NORMAL_EDGES}),
    ]
    results = []
    thick_tiles = set()
    for name, args in plans:
        text = call("border_pieces", args)
        pieces = ready(text)
        if name == "46":
            thick_tiles = {(t["x"], t["y"]) for t in pieces}
        if name == "44":
            pieces = [t for t in pieces if (t["x"], t["y"]) not in thick_tiles]  # the thick edge wins that tile
        if name == "46":
            # The mapper's corner: where a thick top edge ends on the left, the mud
            # tile west of it gets the thick small corner (cne) on top of the grey
            # "e", and the grey corner above that tile goes away.
            grey_pieces = dict(((t["x"], t["y"]), t) for t in results[0][1])
            for t in list(pieces):
                if 7079 in t["add"] and (t["x"] - 1, t["y"]) not in grey:
                    west = (t["x"] - 1, t["y"])
                    corner = grey_pieces.get(west)
                    if corner is not None:
                        corner["add"] = corner["add"] + [7084]
                    else:
                        results[0][1].append({"x": west[0], "y": west[1], "z": Z, "add": [7084]})
                    above = (west[0], west[1] - 1)
                    results[0] = (results[0][0], [g for g in results[0][1] if (g["x"], g["y"]) != above])
        results.append((name, pieces))
        print(f"border {name}: {len(pieces)} tile(s):", " ".join(f"{t['x']},{t['y']}:{t['add']}" for t in pieces))
    print("strip:", len(strip), "tile(s)")
    if not apply:
        return
    print(call("place", {"z": Z, "tiles": strip, "description": "limpa bordas da mancha"}))
    for name, pieces in results:
        if pieces:
            print(call("place", {"z": Z, "tiles": pieces, "description": f"borda {name} da mancha"}))


if __name__ == "__main__":
    main()
