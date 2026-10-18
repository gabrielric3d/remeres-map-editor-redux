"""RME auto-border table (ground_brush_arrays.cpp) + helpers."""
NW, N, NE, W, E, SW, S, SE = 1, 2, 4, 8, 16, 32, 64, 128
OFFS = {NW: (-1, -1), N: (0, -1), NE: (1, -1), W: (-1, 0), E: (1, 0), SW: (-1, 1), S: (0, 1), SE: (1, 1)}

def pieces_for_mask(m):
    """Return list of piece names for an 8-neighbour mask (like RME)."""
    hn, hs, he, hw = bool(m & N), bool(m & S), bool(m & E), bool(m & W)
    out = []
    nu = su = eu = wu = False
    if hn and hw and not hs and not he: out.append("dnw"); nu = wu = True
    if hn and he and not hs and not hw: out.append("dne"); nu = eu = True
    if hs and hw and not hn and not he: out.append("dsw"); su = wu = True
    if hs and he and not hn and not hw: out.append("dse"); su = eu = True
    if hn and not nu: out.append("n")
    if hs and not su: out.append("s")
    if he and not eu: out.append("e")
    if hw and not wu: out.append("w")
    if (m & NW) and not hn and not hw: out.append("cnw")
    if (m & NE) and not hn and not he: out.append("cne")
    if (m & SW) and not hs and not hw: out.append("csw")
    if (m & SE) and not hs and not he: out.append("cse")
    return out

def mask_at(x, y, inside):
    """inside(x,y)->bool. Mask of neighbours that are inside."""
    m = 0
    for bit, (dx, dy) in OFFS.items():
        if inside(x + dx, y + dy):
            m |= bit
    return m

# border sets: name -> {piece: id or [ids]}
SETS = {}
def reg(bid, **kw):
    SETS[bid] = kw
reg(10, n=4456, e=4457, s=4458, w=4459, cnw=4460, cne=4461, csw=4463, cse=4462, dnw=4464, dne=4465, dse=4466, dsw=4467)
reg(7, n=4796, e=4797, s=4798, w=4799, cnw=4800, cne=4801, csw=4802, cse=4803, dnw=4804, dne=4805, dse=4807, dsw=4806)
reg(18, n=4808, e=4809, s=4810, w=4811, cnw=4812, cne=4813, csw=4814, cse=4815, dnw=4816, dne=4817, dse=4819, dsw=4818)
reg(211, n=15503, e=15504, s=15505, w=15506, cnw=15509, cne=15510, csw=15508, cse=15507, dnw=15513, dne=15514, dse=15511, dsw=15512)
reg(300, n=34661, e=34662, s=34663, w=34664, cnw=34665, cne=34666, csw=34667, cse=34668, dnw=34669, dne=34670, dse=34672, dsw=34671)
reg(249, n=45316, e=45315, s=45317, w=45314, cnw=45320, cne=45319, csw=45318, cse=45321, dnw=45322, dne=45324, dse=45325, dsw=45323)
reg(277, n=49662, e=49665, s=49661, w=49666, cnw=49664, cne=49670, csw=49663, cse=49668, dnw=49683, dne=49681, dse=49680, dsw=49682)
reg(314, n=49710, e=49721, s=49718, w=49715, cnw=49714, cne=49712, csw=49717, cse=49720, dnw=49713, dne=49711, dse=49719, dsw=49716)
reg(311, n=13621, e=13622, s=13620, w=13623, cnw=13630, cne=13631, csw=13629, cse=13628, dnw=13626, dne=13627, dse=13624, dsw=13625)
reg(307, n=51632, e=51629, s=51630, w=51633, cnw=51636, cne=51635, csw=51634, cse=51631, dnw=51637, dne=51639, dse=51640, dsw=51638)
reg(320, n=56802, e=56801, s=56803, w=56800, cnw=56806, cne=56805, csw=56804, cse=56807, dnw=56810, dne=56808, dse=56811, dsw=56809)
reg(266, n=51823, e=51824, s=51825, w=51826, cnw=51827, cne=51828, csw=51829, cse=51830, dnw=51831, dne=51832, dse=51834, dsw=51833)
reg(30, n=5828, e=5829, s=5830, w=5831, cnw=5834, cne=5835, csw=5832, cse=5833, dnw=5836, dne=5837, dse=5839, dsw=5838)

def outer_border(points, bid, inside=None):
    """Standard outer border of a patch (set of (x,y)). Returns {(x,y): [ids]}"""
    pts = set(points)
    ins = inside or (lambda x, y: (x, y) in pts)
    cand = set()
    for (x, y) in pts:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                q = (x + dx, y + dy)
                if q not in pts:
                    cand.add(q)
    out = {}
    s = SETS[bid]
    for (x, y) in cand:
        if ins(x, y):
            continue
        names = pieces_for_mask(mask_at(x, y, ins))
        ids = [s[nm] for nm in names if nm in s and s[nm]]
        if ids:
            out[(x, y)] = ids
    return out
