import re, json, rme
ids = [1,3,7,10,11,13,18,30,42,51,59,60,61,66,121,125,127,132,134,139,143,146,149,151,154,163,203,204,211,230,232,249,266,270,271,276,277,280,300,307,308,311,314,315,320,329]
out = {}
for b in ids:
    t, _ = rme.call("list_borders", {"border_id": b})
    d = {}
    for m in re.finditer(r"\b(n|e|s|w|cnw|cne|csw|cse|dnw|dne|dse|dsw) ([0-9()/\-]+|-)", t):
        nm, v = m.group(1), m.group(2)
        if v == "-":
            continue
        vals = [int(x.split("(")[0]) for x in v.split("/") if x and x[0].isdigit()]
        d[nm] = vals
    first = t.splitlines()[0] if t else ""
    out[b] = {"desc": first, "p": d}
json.dump(out, open("sets.json", "w"), indent=0)
for b, v in out.items():
    print(b, v["desc"][:150], {k: v["p"][k] for k in list(v["p"])[:4]})
