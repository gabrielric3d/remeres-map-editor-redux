"""Fix the left grey patch on floor 6 (x 4097-4103, y 3915-3919).

Usage: python fix_patch.py plan|apply
"""
import json
import re
import sys

import rme_mcp

Z = 6
PATCH_AREA = {"x": 4096, "y": 3914, "width": 9, "height": 7}
GREY = 45352
GREY_BORDER = "250"
CRACK_BORDER = "46"
BORDER_IDS = list(range(45353, 45364)) + list(range(7079, 7091))  # sets 250 and 46
# Tiles that carry pieces of those two sets around/inside the patch today.
TILES_WITH_PIECES = [
    (4097, 3915), (4098, 3915), (4099, 3915), (4100, 3915), (4101, 3915), (4102, 3915),
    (4097, 3916), (4098, 3916), (4099, 3916), (4100, 3916), (4102, 3916), (4103, 3916),
    (4097, 3917), (4098, 3917), (4099, 3917), (4100, 3917), (4101, 3917), (4102, 3917), (4103, 3917),
    (4098, 3918), (4099, 3918), (4100, 3918), (4102, 3918), (4103, 3918),
    (4098, 3919), (4099, 3919), (4100, 3919), (4101, 3919), (4102, 3919),
]
NOTCH = (4099, 3916)  # mud tile cutting into the top of the patch


def call(tool, args):
    rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "fix_patch", "version": "1"}})
    reply = rme_mcp.rpc("tools/call", {"name": tool, "arguments": args}, 7)
    result = reply.get("result", {})
    text = "\n".join(b.get("text", "") for b in result.get("content", []) if b.get("type") == "text")
    return text, result.get("isError", False)


def ready_tiles(text):
    match = re.search(r"Ready for place[^\n]*:\n(\[.*\])", text, re.S)
    return json.loads(match.group(1)) if match else []


def borders(border_id, edges=None):
    args = dict(PATCH_AREA, z=Z, border_id=border_id, ground=[GREY])
    if edges:
        args["edges"] = edges
    return call("border_pieces", args)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "plan"
    if mode == "plan":
        # The patch as it will be once the notch is filled: ask with explicit points.
        points = [[4098, 3916], [4099, 3916], [4100, 3916], [4101, 3916],
                  [4099, 3917], [4100, 3917], [4101, 3917], [4102, 3917],
                  [4099, 3918], [4100, 3918], [4101, 3918]]
        for label, border_id, edges in (("GREY BORDER", GREY_BORDER, None), ("CRACK south-west", CRACK_BORDER, ["e", "dne", "cne", "n"])):
            args = {"border_id": border_id, "points": points, "z": Z}
            if edges:
                args["edges"] = edges
            text, err = call("border_pieces", args)
            print("=====", label, "(error)" if err else "")
            print(text.split("Ready for place")[0])
        return

    # 1. Fill the notch and strip every piece of sets 250 and 46 from the area.
    tiles = [{"x": x, "y": y, "remove": BORDER_IDS} for (x, y) in TILES_WITH_PIECES]
    for t in tiles:
        if (t["x"], t["y"]) == NOTCH:
            t["ground"] = GREY
    text, err = call("place", {"z": Z, "tiles": tiles, "description": "limpa bordas da mancha"})
    print("clear:", text, "(error)" if err else "")
    # 2. Complete grey border around the patch (as it is on the map now).
    text, err = borders(GREY_BORDER)
    grey = ready_tiles(text)
    print("grey border tiles:", len(grey), "(error)" if err else "")
    text, err = call("place", {"z": Z, "tiles": grey, "description": "borda da pedra cinza"})
    print("place grey:", text, "(error)" if err else "")
    # 3. The cliff only along the south-west side, on top of the grey border.
    text, err = borders(CRACK_BORDER, sys.argv[2].split(",") if len(sys.argv) > 2 else ["e", "dne", "cne", "n"])
    crack = ready_tiles(text)
    print("crack tiles:", len(crack), "(error)" if err else "")
    text, err = call("place", {"z": Z, "tiles": crack, "description": "fenda no lado sudoeste"})
    print("place crack:", text, "(error)" if err else "")


if __name__ == "__main__":
    main()
