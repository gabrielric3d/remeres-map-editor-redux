import shutil
import zipfile
import sys
from pathlib import Path

src_files = [
    Path(r"B:\Github\blacktalon-workspace\server\data\items\items.otb"),
    Path(r"B:\Github\blacktalon-workspace\server\data\items\items.xml"),
    Path(r"B:\Github\blacktalon-workspace\client\data\things\1098\tibia.spr"),
    Path(r"B:\Github\blacktalon-workspace\client\data\things\1098\tibia.dat"),
    Path(r"B:\Github\blacktalon-workspace\client\data\things\1098\tibia.otfi"),
]

extra_files = [
    (Path(r"B:\Github\blacktalon-workspace\server\data\scripts\creatures_table.json"),
     Path(r"B:\Github\rme_redux\data")),
]

dest = Path(r"B:\Github\rme_redux\data\1098")
dest.mkdir(parents=True, exist_ok=True)

for f in src_files:
    if f.exists():
        shutil.copy2(f, dest / f.name)
        print(f"Copiado: {f.name}")
    else:
        print(f"Nao encontrado: {f}")

for src, dst_dir in extra_files:
    dst_dir.mkdir(parents=True, exist_ok=True)
    if src.exists():
        shutil.copy2(src, dst_dir / src.name)
        print(f"Copiado: {src.name}")
    else:
        print(f"Nao encontrado: {src}")

# Zipar com barra de progresso
zip_path = dest.parent / "BT_SPRDAT_UPDATED.zip"
files_to_zip = [(f, f.name) for f in dest.iterdir()]
for src, dst_dir in extra_files:
    copied = dst_dir / src.name
    if copied.exists():
        files_to_zip.append((copied, copied.name))
total_size = sum(f.stat().st_size for f, _ in files_to_zip)
written = 0

print(f"\nZipando {total_size / 1024 / 1024:.1f} MB...")

with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
    for f, arcname in files_to_zip:
        size = f.stat().st_size
        zf.write(f, arcname)
        written += size
        pct = written * 100 // total_size
        bar = "#" * (pct // 2) + "-" * (50 - pct // 2)
        sys.stdout.write(f"\r[{bar}] {pct}% - {arcname}")
        sys.stdout.flush()

print(f"\n\nZip criado: {zip_path}")
input("\nPressione Enter para fechar...")
