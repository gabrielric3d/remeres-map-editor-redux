"""Regera as entradas "Task Rooms" de boss_teleport.lua a partir da
1_bossroom_table.lua do servidor.

Uso:
    python gen_from_task_rooms.py [caminho/para/task/1_bossroom_table.lua]

De cada sala de BOSSES_ROOMS extrai a chave (nome da sala), a
virtualRoom.playerPosition (posicao usada no teleporte), a
virtualRoom.bossPosition e o tpExit (ambos so aparecem no tooltip).
Salas comentadas no arquivo do servidor sao ignoradas.
"""

import io
import re
import sys

DEFAULT_SRC = r"B:\Github\blacktalon-workspace\server\data\scripts\task\1_bossroom_table.lua"

POS_RE = re.compile(r"Position\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)")
ROOM_RE = re.compile(r'^    \["([^"]+)"\]\s*=\s*\{')
FIELD_RE = re.compile(r"^        (\w+)\s*=")


def parse(path):
    text = io.open(path, encoding="utf-8", errors="replace").read()
    start = text.find("BOSSES_ROOMS")
    if start > 0:
        text = text[start:]

    rooms = []
    cur = None
    in_virtual_room = False

    for line in text.split("\n"):
        if line.strip().startswith("--"):
            continue

        room = ROOM_RE.match(line)
        if room:
            cur = {"name": room.group(1), "player": None, "boss": None, "exit": None}
            rooms.append(cur)
            in_virtual_room = False
            continue

        if cur is None:
            continue

        field = FIELD_RE.match(line)
        if field:
            in_virtual_room = field.group(1) == "virtualRoom"

        if "tpExit" in line and cur["exit"] is None:
            pos = POS_RE.search(line)
            if pos:
                cur["exit"] = tuple(int(v) for v in pos.groups())

        if in_virtual_room:
            for key, field_name in (("player", "playerPosition"), ("boss", "bossPosition")):
                if field_name in line and cur[key] is None:
                    pos = POS_RE.search(line)
                    if pos:
                        cur[key] = tuple(int(v) for v in pos.groups())

    return rooms


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC
    rooms = parse(src)

    skipped = []
    for room in rooms:
        if not room["player"]:
            skipped.append(room["name"])
            continue
        extra = ""
        if room["boss"]:
            extra += ", bx = %d, by = %d, bz = %d" % room["boss"]
        if room["exit"]:
            extra += ", ex = %d, ey = %d, ez = %d" % room["exit"]
        print('    { group = "Task Rooms", name = "%s", x = %d, y = %d, z = %d%s },'
              % (room["name"].replace('"', '\\"'), room["player"][0], room["player"][1],
                 room["player"][2], extra))

    if skipped:
        sys.stderr.write("sem playerPosition (ignorados): %s\n" % skipped)


if __name__ == "__main__":
    main()
