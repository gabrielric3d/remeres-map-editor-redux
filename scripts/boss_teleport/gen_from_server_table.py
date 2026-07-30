"""Regera o bloco BOSSES de boss_teleport.lua a partir da table.lua do servidor.

Uso:
    python gen_from_server_table.py [caminho/para/leverboss/table.lua]

Imprime as linhas prontas para colar no bloco BOSSES do boss_teleport.lua.
Extrai, de cada entrada de configQuest: o nome do boss (boss.name), o action id
da alavanca e a centerPos (virtualRoom.centerPos; entradas "(room)" saem de
room.centerPos quando difere). Entradas sem centerPos sao listadas no stderr.
"""

import io
import re
import sys

DEFAULT_SRC = r"B:\Github\blacktalon-workspace\server\data\scripts\actions\leverboss\table.lua"

GROUP_MAP = {
    "Daily Bosses": "Daily",
    "INFERNO": "Inferno",
    "ETERNAL DEPTHS": "Depths",
    "QUEST BOSSES": "Quest",
}

ENTRY_RE = re.compile(r"^    \[(\d+)\]\s*=\s*\{\s*(?:--\s*(.*))?$")
POS_RE = re.compile(r"Position\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)")
SECTION_RE = re.compile(r"^        (\w+)\s*=")
BANNER_RE = re.compile(r"^\s*--\s*!+\s*(.+?)\s*!+\s*$")


def parse(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    end = text.find("\nportalTable")
    if end > 0:
        text = text[:end]

    entries = []
    cur = None
    section = None
    group = "Outros"

    for line in text.split("\n"):
        banner = BANNER_RE.match(line)
        if banner and set(banner.group(1)) != {"!"}:
            group = GROUP_MAP.get(banner.group(1).strip(), banner.group(1).strip())
            continue

        entry = ENTRY_RE.match(line)
        if entry:
            cur = {
                "id": int(entry.group(1)),
                "comment": (entry.group(2) or "").strip(),
                "group": group,
                "name": None,
                "vpos": None,
                "rpos": None,
            }
            entries.append(cur)
            section = None
            continue

        if cur is None:
            continue

        sec = SECTION_RE.match(line)
        if sec:
            section = sec.group(1)

        if section == "boss" and cur["name"] is None:
            name = re.match(r'^\s*name\s*=\s*"([^"]*)"', line)
            if name:
                cur["name"] = name.group(1)

        if section in ("virtualRoom", "room") and "centerPos" in line:
            pos = POS_RE.search(line)
            if pos and not line.strip().startswith("--"):
                key = "vpos" if section == "virtualRoom" else "rpos"
                if cur[key] is None:
                    cur[key] = tuple(int(v) for v in pos.groups())

    return entries


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC
    entries = parse(src)

    rows = []
    skipped = []
    for e in entries:
        name = e["name"] or e["comment"] or "ID %d" % e["id"]
        if not (e["vpos"] or e["rpos"]):
            skipped.append((e["id"], name))
            continue
        rows.append((e["group"], e["id"], name, e["vpos"] or e["rpos"]))
        if e["vpos"] and e["rpos"] and e["rpos"] != e["vpos"]:
            rows.append((e["group"], e["id"], name + " (room)", e["rpos"]))

    last = None
    for group, eid, name, (x, y, z) in rows:
        if group != last:
            print("\n    -- %s" % group)
            last = group
        print('    { group = "%s", id = %d, name = "%s", x = %d, y = %d, z = %d },'
              % (group, eid, name.replace('"', '\\"'), x, y, z))

    if skipped:
        sys.stderr.write("sem centerPos (ignorados): %s\n" % skipped)


if __name__ == "__main__":
    main()
