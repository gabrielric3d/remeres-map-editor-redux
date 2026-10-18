"""Shared helpers for the dragon cave: ground/border data from data/1098 and the editor's
border table, plus a tiny MCP wrapper."""
import os
import random
import re
import xml.etree.ElementTree as ET

import rme_mcp

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "data", "1098")
EDGES = ["n", "e", "s", "w", "cnw", "cne", "csw", "cse", "dnw", "dne", "dse", "dsw"]
N, S, E, Wd, NW, NE, SW, SE = 2, 64, 16, 8, 1, 4, 32, 128
NEIGHBOURS = [(-1, -1, NW), (0, -1, N), (1, -1, NE), (-1, 0, Wd), (1, 0, E), (-1, 1, SW), (0, 1, S), (1, 1, SE)]


def _xml(name):
    text = open(os.path.join(DATA, name), encoding="latin-1").read()
    return ET.fromstring(text)


_borders = None
_grounds = None


def border_set(bid):
    global _borders
    if _borders is None:
        _borders = {}
        for b in _xml("borders.xml").iter("border"):
            edges = {}
            for bi in b.iter("borderitem"):
                if bi.get("item"):
                    edges[bi.get("edge")] = int(bi.get("item"))
                else:  # variants: keep the most common one
                    opts = [(int(i.get("chance", "1")), int(i.get("id"))) for i in bi.iter("item")]
                    if opts:
                        edges[bi.get("edge")] = max(opts)[1]
            _borders[int(b.get("id"))] = edges
    return _borders[int(bid)]


def ground_brush(name):
    global _grounds
    if _grounds is None:
        _grounds = {}
        for br in _xml("grounds.xml").iter("brush"):
            if br.get("type") != "ground":
                continue
            items = [(int(i.get("id")), int(i.get("chance", "1"))) for i in br.findall("item")]
            borders = [(b.get("align"), b.get("to"), int(b.get("id").split(",")[0])) for b in br.findall("border") if b.get("id")]
            _grounds[br.get("name")] = {"items": items, "borders": borders, "z": int(br.get("z-order", "0"))}
    return _grounds[name]


def pick(name, rng=random):
    items = [(i, c) for i, c in ground_brush(name)["items"] if c > 0]
    total = sum(c for _, c in items)
    r = rng.uniform(0, total)
    for i, c in items:
        r -= c
        if r <= 0:
            return i
    return items[0][0]


def border_table(bits):
    hn, hs, he, hw = bool(bits & N), bool(bits & S), bool(bits & E), bool(bits & Wd)
    out, used = [], set()
    if hn and hw and not hs and not he:
        out.append("dnw"); used |= {"n", "w"}
    if hn and he and not hs and not hw:
        out.append("dne"); used |= {"n", "e"}
    if hs and hw and not hn and not he:
        out.append("dsw"); used |= {"s", "w"}
    if hs and he and not hn and not hw:
        out.append("dse"); used |= {"s", "e"}
    for flag, name in ((hn, "n"), (hs, "s"), (he, "e"), (hw, "w")):
        if flag and name not in used:
            out.append(name)
    if bits & NW and not hn and not hw:
        out.append("cnw")
    if bits & NE and not hn and not he:
        out.append("cne")
    if bits & SW and not hs and not hw:
        out.append("csw")
    if bits & SE and not hs and not he:
        out.append("cse")
    return out


def pieces_around(patch, bid, on=None):
    """Outer border of `patch` (set of tiles) with border set bid: pieces on the tiles around it
    (restricted to `on` when given). Returns {tile: [item ids]}."""
    bs = border_set(bid)
    ring = {(x + dx, y + dy) for (x, y) in patch for dx, dy, _ in NEIGHBOURS} - patch
    if on is not None:
        ring &= on
    out = {}
    for (x, y) in ring:
        bits = 0
        for dx, dy, bit in NEIGHBOURS:
            if (x + dx, y + dy) in patch:
                bits |= bit
        ids = [bs[e] for e in border_table(bits) if e in bs]
        if ids:
            out[(x, y)] = ids
    return out


def pieces_inside(patch, bid, outside):
    """Inner border: pieces on the patch's own edge tiles, facing `outside` tiles."""
    bs = border_set(bid)
    out = {}
    for (x, y) in patch:
        bits = 0
        for dx, dy, bit in NEIGHBOURS:
            if (x + dx, y + dy) in outside:
                bits |= bit
        if bits:
            ids = [bs[e] for e in border_table(bits) if e in bs]
            if ids:
                out[(x, y)] = ids
    return out


_init = False


def call(name, args):
    global _init
    if not _init:
        rme_mcp.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "cave", "version": "1"}})
        _init = True
    r = rme_mcp.rpc("tools/call", {"name": name, "arguments": args}, 3)
    return r["result"]["content"][0]["text"]


def place(z, tiles, label):
    out = []
    for i in range(0, len(tiles), 4500):
        out.append(call("place", {"z": z, "tiles": tiles[i:i + 4500], "description": label}))
    return "\n".join(out)
