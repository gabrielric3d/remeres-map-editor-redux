#!/usr/bin/env python3
"""Converte uma pasta de dados no layout modular do upstream para o formato
plano que este editor carrega.

O upstream espalha os materiais em subpastas e amarra tudo com
`<include folder="..." subfolders="true">` mais um palettes.xml; o nosso
Materials::unserializeMaterials so conhece `<include file="...">` na mesma
pasta e tira a categoria do tileset do proprio XML (`<terrain>`, `<raw>`, ...),
nao do nome da pasta.

Entra:                              Sai (na mesma pasta):
    materials.xml (modular)             materials.xml (includes planos)
    borders/*.xml                       borders.xml
    brushes/*.xml                       brushes.xml
    creatures/*.xml                     creatures.xml
    tilesets/<categoria>/*.xml          tilesets.xml
    items/items.xml                     items.xml (so com --items)

O items.xml fica de fora por padrao: ele e o par do items.otb e tem de vir da
mesma pasta que ele, senao os server ids nao batem.

Uso:
    python scripts/convert_modular_materials.py data/1310
    python scripts/convert_modular_materials.py data/1310 --items --clean
"""

import argparse
import os
import shutil
import sys
import xml.etree.ElementTree as ET

# Nome da pasta -> nome do no de categoria que Tileset::loadCategory entende.
CATEGORY_BY_FOLDER = {
    "terrain": "terrain",
    "doodad": "doodad",
    "items": "items",
    "raw": "raw",
    "collections": "collections",
    "terrain_and_raw": "terrain_and_raw",
    "collections_and_raw": "collections_and_raw",
    "collections_and_terrain": "collections_and_terrain",
    "doodad_and_raw": "doodad_and_raw",
    "items_and_raw": "items_and_raw",
    "creatures": "creatures",
}

HEADER = '<?xml version="1.0" encoding="utf-8"?>\n'


def read_text(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def write_text(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def merge_single_file(folder, out_path, root_tag="materials"):
    """Junta os .xml de uma subpasta num arquivo so, com o root que usamos.

    Quando a pasta tem um arquivo unico, o conteudo e copiado como esta - so o
    root muda, se precisar. Reserializar um brushes.xml de 1 MB perderia
    comentarios e mexeria em toda a indentacao sem necessidade.
    """
    if not os.path.isdir(folder):
        return None

    files = sorted(f for f in os.listdir(folder) if f.lower().endswith(".xml"))
    if not files:
        return None

    merged = ET.Element(root_tag)
    for name in files:
        root = ET.parse(os.path.join(folder, name)).getroot()
        for child in root:
            merged.append(child)

    indent(merged)
    write_text(out_path, HEADER + ET.tostring(merged, encoding="unicode") + "\n")
    return len(files)


def indent(element, level=0):
    pad = "\n" + "\t" * level
    if len(element):
        if not (element.text or "").strip():
            element.text = pad + "\t"
        for child in element:
            indent(child, level + 1)
        if not (child.tail or "").strip():
            child.tail = pad
    if level and not (element.tail or "").strip():
        element.tail = pad


def palette_order(base):
    """Ordem dos arquivos de tileset declarada no palettes.xml, se houver.

    So serve para a saida sair na mesma ordem do upstream; a categoria vem da
    pasta, nao da paleta.
    """
    path = os.path.join(base, "palettes.xml")
    if not os.path.isfile(path):
        return []

    order = []
    for include in ET.parse(path).getroot().iter("include"):
        target = include.get("file")
        if target and target not in order:
            order.append(target.replace("\\", "/"))
    return order


def collect_tilesets(base):
    root = os.path.join(base, "tilesets")
    if not os.path.isdir(root):
        return [], []

    found = {}
    unknown = []
    for folder in sorted(os.listdir(root)):
        folder_path = os.path.join(root, folder)
        if not os.path.isdir(folder_path):
            continue

        category = CATEGORY_BY_FOLDER.get(folder.lower())
        if category is None:
            unknown.append(folder)
            continue

        for name in sorted(os.listdir(folder_path)):
            if not name.lower().endswith(".xml"):
                continue
            found["tilesets/{}/{}".format(folder, name)] = (category, os.path.join(folder_path, name))

    ordered = [key for key in palette_order(base) if key in found]
    ordered += [key for key in sorted(found) if key not in ordered]
    return [(key, found[key][0], found[key][1]) for key in ordered], unknown


def build_tilesets(base, out_path):
    entries, unknown = collect_tilesets(base)
    if not entries:
        return 0, unknown, 0

    materials = ET.Element("materials")
    renamed = 0
    for rel, category, path in entries:
        source = ET.parse(path).getroot()
        name = source.get("name") or os.path.splitext(os.path.basename(path))[0]

        tileset = ET.SubElement(materials, "tileset", {"name": name})
        node = ET.SubElement(tileset, category)
        for child in source:
            # O upstream tem um "<ite fromid=... />" solto em raw/Others.xml; o
            # nosso loader ignoraria a linha em silencio e o item sumiria da
            # paleta sem aviso nenhum.
            tag = child.tag
            if tag not in ("brush", "item", "creature"):
                if tag.startswith("ite"):
                    tag = "item"
                    renamed += 1
                else:
                    continue
            ET.SubElement(node, tag, dict(child.attrib))

    indent(materials)
    write_text(out_path, HEADER + ET.tostring(materials, encoding="unicode") + "\n")
    return len(entries), unknown, renamed


def build_materials(base, includes, out_path):
    source = os.path.join(base, "materials.xml")
    metaitems = []
    if os.path.isfile(source):
        for node in ET.parse(source).getroot():
            if node.tag == "metaitem" and node.get("id"):
                metaitems.append(node.get("id"))

    lines = [HEADER, "<materials>\n"]
    if metaitems:
        lines.append("\n\t<!-- Itens que so existem no editor -->\n")
        lines.extend('\t<metaitem id="{}"/>\n'.format(item) for item in metaitems)
    lines.append("\n")
    lines.extend('\t<include file="{}"/>\n'.format(name) for name in includes)
    lines.append("\n</materials>\n")
    write_text(out_path, "".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", help="pasta de dados da versao, ex.: data/1310")
    parser.add_argument("--items", action="store_true", help="tambem achatar items/items.xml (so quando o otb vem da mesma pasta)")
    parser.add_argument("--clean", action="store_true", help="apagar as subpastas ja convertidas")
    args = parser.parse_args()

    base = os.path.abspath(args.folder)
    if not os.path.isdir(base):
        sys.exit("Pasta inexistente: " + base)

    includes = []
    converted = []

    for folder, out_name in (("borders", "borders.xml"), ("brushes", "brushes.xml")):
        count = merge_single_file(os.path.join(base, folder), os.path.join(base, out_name))
        if count:
            includes.append(out_name)
            converted.append(folder)
            print("{}: {} arquivo(s) -> {}".format(folder, count, out_name))

    result = build_tilesets(base, os.path.join(base, "tilesets.xml"))
    if result[0]:
        count, unknown, renamed = result
        includes.append("tilesets.xml")
        converted.append("tilesets")
        print("tilesets: {} arquivo(s) -> tilesets.xml".format(count))
        if renamed:
            print("  ! {} no(s) com nome invalido corrigido(s) para <item>".format(renamed))
        for folder in unknown:
            print("  ! subpasta sem categoria conhecida, ignorada: tilesets/{}".format(folder))

    # creatures.xml e items.xml nao entram no materials.xml: o editor os carrega
    # pelo nome, direto da raiz da pasta da versao.
    count = merge_single_file(os.path.join(base, "creatures"), os.path.join(base, "creatures.xml"), root_tag="creatures")
    if count:
        converted.append("creatures")
        print("creatures: {} arquivo(s) -> creatures.xml".format(count))

    if args.items:
        count = merge_single_file(os.path.join(base, "items"), os.path.join(base, "items.xml"), root_tag="items")
        if count:
            converted.append("items")
            print("items: {} arquivo(s) -> items.xml".format(count))

    if not includes:
        sys.exit("Nada para converter: a pasta nao tem borders/, brushes/ nem tilesets/.")

    build_materials(base, includes, os.path.join(base, "materials.xml"))
    print("materials.xml reescrito com {} include(s)".format(len(includes)))

    if args.clean:
        for folder in converted:
            shutil.rmtree(os.path.join(base, folder))
            print("removido: {}/".format(folder))


if __name__ == "__main__":
    main()
