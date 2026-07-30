#!/usr/bin/env python3
"""Inspect items.otb: parse server IDs and report selected ones.

Standalone OTB binary reader (no deps). Usage:
    python otb_inspect.py <items.otb> [id1 id2 ...]
If no ids given, prints summary (count, max id, ids in 30000-30099 range).
"""
import sys, struct

NODE_START, NODE_END, ESCAPE = 0xFE, 0xFF, 0xFD
ITEM_ATTR_SERVERID = 0x10
ITEM_ATTR_CLIENTID = 0x11
ITEM_ATTR_NAME     = 0x12

GROUPS = {0:"NONE",1:"GROUND",2:"CONTAINER",3:"WEAPON",4:"AMMO",5:"ARMOR",
          6:"CHARGES",7:"TELEPORT",8:"MAGICFIELD",9:"WRITEABLE",10:"KEY",
          11:"SPLASH",12:"FLUID",13:"DOOR",14:"DEPRECATED",15:"PODIUM"}

def parse_tree(data):
    # First 4 bytes = identifier/flags; node tree starts at the first 0xFE.
    i = data.index(NODE_START)
    stack, root = [], None
    while i < len(data):
        b = data[i]; i += 1
        if b == NODE_START:
            node_type = data[i]; i += 1
            node = {"type": node_type, "props": bytearray(), "children": []}
            if stack: stack[-1]["children"].append(node)
            else: root = node
            stack.append(node)
        elif b == NODE_END:
            if stack: stack.pop()
        elif b == ESCAPE:
            stack[-1]["props"].append(data[i]); i += 1
        else:
            stack[-1]["props"].append(b)
    return root

def parse_item(node):
    p = node["props"]
    info = {"group": node["type"], "server_id": None, "client_id": None, "name": None}
    if len(p) < 4:
        return info
    off = 4  # skip u32 flags
    while off + 3 <= len(p):
        attr = p[off]; off += 1
        dlen = struct.unpack_from("<H", p, off)[0]; off += 2
        chunk = p[off:off+dlen]; off += dlen
        if attr == ITEM_ATTR_SERVERID and dlen == 2:
            info["server_id"] = struct.unpack("<H", chunk)[0]
        elif attr == ITEM_ATTR_CLIENTID and dlen == 2:
            info["client_id"] = struct.unpack("<H", chunk)[0]
        elif attr == ITEM_ATTR_NAME:
            info["name"] = chunk.decode("latin-1", "replace")
    return info

def main():
    if len(sys.argv) < 2:
        print(__doc__); return
    path = sys.argv[1]
    wanted = set(int(x) for x in sys.argv[2:])
    with open(path, "rb") as f:
        data = f.read()
    root = parse_tree(data)
    items = [parse_item(n) for n in root["children"]]
    by_id = {it["server_id"]: it for it in items if it["server_id"] is not None}
    sids = [s for s in by_id]
    print(f"File: {path}")
    print(f"Total item nodes: {len(items)} | distinct server ids: {len(by_id)} | max id: {max(sids) if sids else 0}")
    rng = sorted(s for s in sids if 30000 < s < 30100)
    print(f"IDs in 30000-30099 range present in OTB: {rng if rng else 'NONE'}")
    print(f"IDs in 80-99 range present in OTB:       {sorted(s for s in sids if 80 <= s <= 99)}")
    if wanted:
        print("\nRequested ids:")
        for w in sorted(wanted):
            it = by_id.get(w)
            if it:
                g = GROUPS.get(it["group"], it["group"])
                print(f"  {w}: PRESENT  group={g}  clientid={it['client_id']}  name={it['name']!r}")
            else:
                print(f"  {w}: ABSENT (not in this OTB)")
            # also check the remapped target
            if 30000 < w < 30100:
                t = w - 30000
                tt = by_id.get(t)
                tg = GROUPS.get(tt['group'], tt['group']) if tt else None
                print(f"      -> remap target {t}: " + (f"PRESENT group={tg} clientid={tt['client_id']}" if tt else "ABSENT"))

if __name__ == "__main__":
    main()
