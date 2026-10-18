import numpy as np, json, sys
from layout import build, render, FLOOR, LAVA
from jag import jaggedize, straight_runs
from PIL import Image
imgs = []
for z in (8, 9, 10):
    g, marks = build(z)
    before = len(straight_runs(g, FLOOR, [0, LAVA], 4)) + len(straight_runs(g, LAVA, [FLOOR], 4))
    g, left = jaggedize(g, z, seed=z, rounds=20)
    print(z, "runs>=4 before", before, "after", len(left), left[:10])
    np.save(f"grid{z}.npy", g)
    json.dump({f"{k[0]},{k[1]}": v for k, v in marks.items()}, open(f"marks{z}.json", "w"))
    render(g, marks, f"shots/layout{z}.png")
    imgs.append(Image.open(f"shots/layout{z}.png"))
wtot = sum(i.width for i in imgs) + 20
out = Image.new("RGB", (wtot, imgs[0].height), (255, 255, 255))
x = 0
for i in imgs:
    out.paste(i, (x, 0)); x += i.width + 10
out.save("shots/layout_all.png")
