"""Testes offline do motor Area Decoration (roda via lupa, sem o editor)."""
import os
from lupa import LuaRuntime

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

lua = LuaRuntime()
src = open(os.path.join(HERE, "main.lua"), encoding="utf-8").read()
AD = lua.execute(src)
assert AD is not None, "modulo nao exportado (app deveria ser nil offline)"

lua.globals().AD = AD

run = lua.execute

# ============================================================
# 1. validatePreset: mensagens fail-fast
# ============================================================
run(r"""
local D = AD.Defaults
local ok, err = AD.validatePreset(D.preset())
assert(not ok and err == "No floor rules defined", tostring(err))

local p = D.preset()
local r = D.floorRule()
r.name = "R1"
p.floorRules = { r }
ok, err = AD.validatePreset(p)
assert(not ok and err == "Floor rule has no floor ID specified", tostring(err))

r.floorId = 100
ok, err = AD.validatePreset(p)
assert(not ok and err == "Floor rule 'R1' has no items", tostring(err))

r.items = { D.itemEntry(2000, 0) }
ok, err = AD.validatePreset(p)
assert(not ok and err == "Item weight must be positive", tostring(err))

r.items = { D.itemEntry(2000, 100) }
ok, err = AD.validatePreset(p)
assert(ok, tostring(err))

r.friendChance = 150
ok, err = AD.validatePreset(p)
assert(not ok and err == "Friend chance must be between 0 and 100", tostring(err))
print("OK validate")
""")

# ============================================================
# 2. Determinismo por seed (virtual, PureRandom)
# ============================================================
run(r"""
local D = AD.Defaults

local function makeSimplePreset()
    local p = D.preset()
    local r = D.floorRule()
    r.name = "grass"
    r.floorId = 100
    r.density = 0.5
    r.items = { D.itemEntry(2000, 100), D.itemEntry(2001, 100) }
    p.floorRules = { r }
    return p
end

local function snapshot(eng)
    local s = {}
    for _, it in ipairs(eng.preview.items) do
        s[#s + 1] = it.x .. "," .. it.y .. "," .. it.z .. "," .. it.itemId
    end
    return table.concat(s, ";")
end

local e1 = AD.Engine.new(nil)
e1:setPreset(makeSimplePreset())
assert(e1:generatePreviewVirtual(20, 20, 100, 42), e1.lastError)
local a = snapshot(e1)
assert(e1.preview.totalItemsPlaced > 0, "nada colocado")

local e2 = AD.Engine.new(nil)
e2:setPreset(makeSimplePreset())
assert(e2:generatePreviewVirtual(20, 20, 100, 42), e2.lastError)
assert(snapshot(e2) == a, "mesma seed deveria reproduzir o mesmo preview")

local e3 = AD.Engine.new(nil)
e3:setPreset(makeSimplePreset())
assert(e3:generatePreviewVirtual(20, 20, 100, 43), e3.lastError)
assert(snapshot(e3) ~= a, "seeds diferentes deveriam divergir")
print("OK determinismo: " .. e1.preview.totalItemsPlaced .. " itens")
""")

# ============================================================
# 3. SpacingConfig respeitado (minDistance / minSameItemDistance)
# ============================================================
run(r"""
local D = AD.Defaults
local p = D.preset()
local r = D.floorRule()
r.name = "x"
r.floorId = 100
r.density = 1.0
r.items = { D.itemEntry(2000, 100), D.itemEntry(2001, 100) }
p.floorRules = { r }
p.spacing = { minDistance = 1, minSameItemDistance = 2, checkDiagonals = true }

local e = AD.Engine.new(nil)
e:setPreset(p)
assert(e:generatePreviewVirtual(15, 15, 100, 7), e.lastError)
local items = e.preview.items
assert(#items > 10, "esperava varios itens")
for i = 1, #items do
    for j = i + 1, #items do
        local a, b = items[i], items[j]
        if a.z == b.z then
            local d = math.max(math.abs(a.x - b.x), math.abs(a.y - b.y))
            assert(d >= 1, "minDistance violado")
            if a.itemId == b.itemId then
                assert(d >= 2, "minSameItemDistance violado: d=" .. d)
            end
        end
    end
end
print("OK spacing: " .. #items .. " itens")
""")

# ============================================================
# 4. maxItemsTotal cap + previewWasCapped
# ============================================================
run(r"""
local D = AD.Defaults
local p = D.preset()
local r = D.floorRule()
r.name = "x"
r.floorId = 100
r.density = 1.0
r.items = { D.itemEntry(2000, 100) }
p.floorRules = { r }
p.maxItemsTotal = 5

local e = AD.Engine.new(nil)
e:setPreset(p)
assert(e:generatePreviewVirtual(30, 30, 100, 9), e.lastError)
assert(e.preview.totalItemsPlaced <= 5, "cap estourado: " .. e.preview.totalItemsPlaced)
assert(e.previewWasCapped, "flag de cap nao ligado")

-- maxItemsTotal == 0: cap imediato, geracao ainda retorna true
local e0 = AD.Engine.new(nil)
p.maxItemsTotal = 0
e0:setPreset(p)
assert(e0:generatePreviewVirtual(10, 10, 100, 9), e0.lastError)
assert(e0.preview.totalItemsPlaced == 0 and e0.previewWasCapped, "cap 0")
print("OK cap")
""")

# ============================================================
# 5. Selecao ponderada fortemente enviesada
# ============================================================
run(r"""
local D = AD.Defaults
local p = D.preset()
local r = D.floorRule()
r.name = "x"
r.floorId = 100
r.density = 1.0
r.items = { D.itemEntry(111, 1), D.itemEntry(222, 999999) }
p.floorRules = { r }
p.spacing.minSameItemDistance = 0
p.spacing.minDistance = 0

local e = AD.Engine.new(nil)
e:setPreset(p)
assert(e:generatePreviewVirtual(25, 25, 100, 11), e.lastError)
local rare = e.preview.itemCountById[111] or 0
local common = e.preview.itemCountById[222] or 0
assert(common > 100, "esperava muitos do item pesado: " .. common)
assert(rare <= 3, "item raro apareceu demais: " .. rare)
print("OK pesos: raro=" .. rare .. " comum=" .. common)
""")

# ============================================================
# 6. GridBased: jitter 0 = posicoes exatamente na grade
# ============================================================
run(r"""
local D = AD.Defaults
local p = D.preset()
local r = D.floorRule()
r.name = "x"
r.floorId = 100
r.density = 1.0
r.items = { D.itemEntry(2000, 100) }
p.floorRules = { r }
p.distribution.mode = 2
p.distribution.gridSpacingX = 3
p.distribution.gridSpacingY = 4
p.distribution.gridJitter = 0
p.spacing.minDistance = 0
p.spacing.minSameItemDistance = 0

local e = AD.Engine.new(nil)
e:setPreset(p)
assert(e:generatePreviewVirtual(20, 20, 100, 5), e.lastError)
assert(e.preview.totalItemsPlaced > 0, "grid vazio")
for _, it in ipairs(e.preview.items) do
    assert(it.x % 3 == 0, "fora da grade X: " .. it.x)
    assert(it.y % 4 == 0, "fora da grade Y: " .. it.y)
end
print("OK grid: " .. e.preview.totalItemsPlaced .. " itens na grade")
""")

# ============================================================
# 7. Cluster rule (centered, virtual): instancias + distancia minima
# ============================================================
run(r"""
local D = AD.Defaults
local p = D.preset()
local r = D.floorRule()
r.name = "cl"
r.ruleMode = "cluster"
r.hasCenterPoint = true
r.centerOffset = { x = 0, y = 0, z = 0 }
r.instanceCount = 3
r.instanceMinDistance = 6
r.clusterTiles = { { offset = { x = 0, y = 0, z = 0 }, itemIds = { 5000 } } }
r.items = { D.itemEntry(5000, 100) }
p.floorRules = { r }

local e = AD.Engine.new(nil)
e:setPreset(p)
assert(e:generatePreviewVirtual(30, 30, 100, 21), e.lastError)
local items = e.preview.items
assert(#items == 3, "esperava 3 instancias, veio " .. #items)
for i = 1, #items do
    for j = i + 1, #items do
        local d = math.max(math.abs(items[i].x - items[j].x), math.abs(items[i].y - items[j].y))
        assert(d >= 6, "instanceMinDistance violado: " .. d)
    end
end
print("OK cluster centered")
""")

# ============================================================
# 8. Friend bias: formula e atenuacao por distancia
# ============================================================
run(r"""
local D = AD.Defaults
local e = AD.Engine.new(nil)
e.virtualPreview = true

local rule = D.floorRule()
rule.friendFloorId = 200
rule.friendChance = 100
rule.friendStrength = 0
rule.enabled = true
e.preset = D.preset()
e.preset.floorRules = { rule }

-- faixa de friend ground (200) na coluna x=0; resto 100
local tiles = {}
for y = 0, 4 do
    for x = 0, 9 do
        tiles[#tiles + 1] = { pos = { x = x, y = y, z = 0 }, groundId = (x == 0) and 200 or 100 }
    end
end
e:buildFriendDistanceCache(tiles)

local function bias(x)
    return e:applyFriendBias(rule, { x = x, y = 2, z = 0 }, 1.0)
end
assert(math.abs(bias(0) - 1.0) < 1e-9, "distancia 0 deveria manter a base: " .. bias(0))
assert(math.abs(bias(1) - 0.5) < 1e-9, "distancia 1 -> 0.5: " .. bias(1))
assert(math.abs(bias(2) - (1 / 3)) < 1e-9, "distancia 2 -> 1/3: " .. bias(2))

-- com friendChance 50: base*(0.5 + 0.5*proximity)
rule.friendChance = 50
assert(math.abs(bias(1) - 0.75) < 1e-9, "blend 50%: " .. bias(1))

-- com strength 20: expoente 2 -> proximity^2
rule.friendChance = 100
rule.friendStrength = 20
assert(math.abs(bias(1) - 0.25) < 1e-9, "strength 20 em d=1 -> 0.25: " .. bias(1))
print("OK friend bias")
""")

# ============================================================
# 9. Importador XML: presets reais + semantica pugixml
# ============================================================
circus = open(os.path.join(ROOT, "data", "presets", "decoration", "circus.xml"), encoding="utf-8").read()
lua.globals().CIRCUS_XML = circus
ice = open(os.path.join(ROOT, "data", "presets", "decoration", "ICE.xml"), encoding="utf-8").read()
lua.globals().ICE_XML = ice

run(r"""
local p, err = AD.Xml.presetFromString(CIRCUS_XML)
assert(p, tostring(err))
assert(p.name == "circus", p.name)
assert(p.spacing.minDistance == 3 and p.spacing.minSameItemDistance == 1, "spacing circus")
assert(p.hasArea and p.area.type == 0, "area circus")
assert(#p.floorRules == 1, "regras circus: " .. #p.floorRules)
local r = p.floorRules[1]
assert(r.ruleMode == "range" and r.fromFloorId == 18764 and r.toFloorId == 18766, "range circus")
assert(math.abs(r.density - 1.0) < 1e-9, "density circus")
assert(#r.items > 0, "itens circus")
local entry = r.items[1]
assert(entry.isComposite and entry.isCluster and entry.weight == 100, "entry circus")
assert(entry.clusterCount == 1 and entry.clusterRadius == 3 and entry.clusterMinDistance == 2, "params circus")
assert(#entry.compositeTiles > 0, "tiles circus")
-- tile com 2 ids empilhados preservados em ordem
local found2 = false
for _, t in ipairs(entry.compositeTiles) do
    if #t.itemIds >= 2 then found2 = true end
end
assert(found2, "esperava tile com itens empilhados")

local pi = AD.Xml.presetFromString(ICE_XML)
assert(pi and pi.name == "ICE" and pi.hasArea and pi.area.type == 2, "ICE basica")
assert(#pi.floorRules == 1 and pi.floorRules[1].fromFloorId == 44935, "ICE range")

-- o preset importado VALIDA e GERA (virtual) com ground dentro do range
local e = AD.Engine.new(nil)
e:setPreset(p)
assert(e:generatePreviewVirtual(20, 20, 18765, 99), e.lastError)
assert(e.preview.totalItemsPlaced > 0, "circus nao gerou nada em virtual")
print("OK import XML: circus gerou " .. e.preview.totalItemsPlaced .. " itens em virtual")
""")

# ============================================================
# 10. Semantica pugixml: bool/int/u16/density default/chance fallback
# ============================================================
run(r"""
local xml = [[<?xml version="1.0"?>
<decoration_preset name="t&amp;t">
  <settings max_items_total="abc" skip_blocked="TRUE" />
  <floor_rules>
    <rule name="r" floor_id="70000">
      <items>
        <item id="0" weight="50" />
        <item id="-1" />
        <composite chance="77"><tile x="1" y="-2"><item id="123" /></tile></composite>
        <cluster weight="0" chance="44"><tile><item id="9" /></tile></cluster>
      </items>
    </rule>
  </floor_rules>
</decoration_preset>]]
local p, err = AD.Xml.presetFromString(xml)
assert(p, tostring(err))
assert(p.name == "t&t", "unescape: " .. p.name)
assert(p.maxItemsTotal == 0, "malformado presente -> 0, veio " .. p.maxItemsTotal)
assert(p.skipBlockedTiles == true, "as_bool TRUE -> true")
local r = p.floorRules[1]
assert(r.floorId == 70000 % 65536, "u16 wrap: " .. r.floorId)
assert(math.abs(r.density - 0.3) < 1e-9, "density default XML = 0.3: " .. r.density)
-- item id 0 descartado; id -1 vira 65535 (strtoul wrap) e e mantido
assert(#r.items == 3, "entradas: " .. #r.items)
assert(r.items[1].itemId == 65535, "id -1 -> 65535: " .. r.items[1].itemId)
assert(r.items[2].isComposite and r.items[2].weight == 77, "chance fallback composite")
assert(r.items[3].isCluster and r.items[3].weight == 44, "chance fallback cluster (weight 0)")
assert(r.items[2].compositeTiles[1].offset.x == 1 and r.items[2].compositeTiles[1].offset.y == -2, "offsets")
print("OK semantica pugixml")
""")

# ============================================================
# 11. Regressoes da verificacao adversarial
# ============================================================
run(r"""
local D = AD.Defaults

-- (a) Clustered com clusterCount=0 e strength=0: FLT_MAX*0 -> exp(0)=1
-- (math.huge daria NaN e nada seria colocado)
local p = D.preset()
local r = D.floorRule()
r.name = "x"
r.floorId = 100
r.density = 1.0
r.items = { D.itemEntry(2000, 100) }
p.floorRules = { r }
p.distribution.mode = 1
p.distribution.clusterCount = 0
p.distribution.clusterStrength = 0
p.spacing.minDistance = 0
p.spacing.minSameItemDistance = 0
local e = AD.Engine.new(nil)
e:setPreset(p)
assert(e:generatePreviewVirtual(8, 8, 100, 3), e.lastError)
assert(e.preview.totalItemsPlaced == 64, "FLT_MAX sentinel: " .. e.preview.totalItemsPlaced)

-- (b) setPreset NAO pode mutar o preset do chamador (deep copy + sort)
local p2 = D.preset()
local lo = D.floorRule(); lo.name = "lo"; lo.floorId = 1; lo.priority = 1
lo.items = { D.itemEntry(10, 100) }
local hi = D.floorRule(); hi.name = "hi"; hi.floorId = 1; hi.priority = 9
hi.items = { D.itemEntry(20, 100) }
p2.floorRules = { lo, hi }
local e2 = AD.Engine.new(nil)
e2:setPreset(p2)
assert(p2.floorRules[1].name == "lo", "preset do chamador foi reordenado!")
assert(e2.preset.floorRules[1].name == "hi", "copia interna deveria estar ordenada")

-- (c) findRule: primeira regra habilitada que casa (ordem pos-sort)
local found = AD.findRule(e2.preset, 1)
assert(found and found.name == "hi", "findRule deveria achar a de prioridade alta")

-- (d) reroll ignora defaultSeed (gera diferente do generatePreview(0))
local p3 = D.preset()
local r3 = D.floorRule(); r3.name = "x"; r3.floorId = 100; r3.density = 0.5
r3.items = { D.itemEntry(2000, 100) }
p3.floorRules = { r3 }
p3.defaultSeed = 1234
local e3 = AD.Engine.new(nil)
e3:setPreset(p3)
assert(e3:generatePreviewVirtual(15, 15, 100, 0), e3.lastError)
assert(e3.preview.seed == 1234, "seed 0 deveria cair no defaultSeed")

-- (e) PreviewManager: virtual NAO registra; getItemsAt funciona
assert(not AD.PreviewManager.hasActivePreview(), "virtual nao deveria registrar preview")
local anyItem = e3.preview.items[1]
assert(anyItem, "esperava itens")
local at = e3:getItemsAt(anyItem.x, anyItem.y, anyItem.z)
assert(#at >= 1 and at[1].itemId == anyItem.itemId, "getItemsAt")

-- (f) AreaOps.contains/getBounds com os quirks [as-is]
local area = D.area()
area.rectMin = { x = 5, y = 5, z = 7 }
area.rectMax = { x = 9, y = 9, z = 7 }
assert(AD.AreaOps.contains(area, { x = 7, y = 7, z = 7 }), "contains dentro")
assert(not AD.AreaOps.contains(area, { x = 4, y = 7, z = 7 }), "contains fora")
area.type = 2
assert(not AD.AreaOps.contains(area, { x = 7, y = 7, z = 7 }), "Selection contains = false [as-is]")
area.type = 1
area.floodOrigin = { x = 50, y = 50, z = 7 }
area.floodMaxRadius = 10
local mn, mx = AD.AreaOps.getBounds(area)
assert(mn.x == 40 and mx.x == 60 and mn.z == 7, "flood bounds quadrado")
print("OK regressoes do motor")
""")

# ============================================================
# 12. Regressoes do XML
# ============================================================
run(r"""
-- strtol para no '.': "-3.7" -> -3; "1e3" -> 1; "0x10" -> 0
local xml = [[<decoration_preset name='single'>
  <settings max_items_total="-3.7" default_seed="18446744073709551615" />
  <distribution mode="1e3" cluster_count="0x10" />
  <floor_rules>
    <rule name="r" floor_id="4294967296"><items><item id="123" /></items></rule>
  </floor_rules>
</decoration_preset>]]
local p, err = AD.Xml.presetFromString(xml)
assert(p, tostring(err))
assert(p.name == "single", "aspas simples: " .. tostring(p.name))
assert(p.maxItemsTotal == -3, "strtol trunc: " .. p.maxItemsTotal)
assert(p.defaultSeed == 4294967295, "uint64 mod 2^32: " .. p.defaultSeed)
assert(p.distribution.mode == 1, "1e3 -> 1: " .. p.distribution.mode)
assert(p.distribution.clusterCount == 0, "0x10 -> 0: " .. p.distribution.clusterCount)
assert(p.floorRules[1].floorId == 65535, "strtoul satura: " .. p.floorRules[1].floorId)

-- float com sufixo: "0.5abc" -> 0.5
local xml2 = [[<decoration_preset name="f"><floor_rules>
  <rule name="r" floor_id="5" density="0.5abc"><items><item id="1" /></items></rule>
</floor_rules></decoration_preset>]]
local p2 = AD.Xml.presetFromString(xml2)
assert(math.abs(p2.floorRules[1].density - 0.5) < 1e-9, "strtod prefixo: " .. p2.floorRules[1].density)

-- XML malformado -> nil (espelha falha de parse do pugixml)
assert(AD.Xml.presetFromString("<decoration_preset name='x'><floor_rules>") == nil, "truncado")
assert(AD.Xml.presetFromString("<decoration_preset><a></b></decoration_preset>") == nil, "fechamento errado")
assert(AD.Xml.presetFromString("garbage") == nil, "lixo")

-- '>' dentro de valor de atributo
local p3 = AD.Xml.presetFromString('<decoration_preset name="A&gt;B e A>B" />')
assert(p3 and p3.name == "A>B e A>B", "quote-aware: " .. tostring(p3 and p3.name))
print("OK regressoes XML")
""")

# ============================================================
# 13. Rule-from-selection: montagem (spec D do dialogo C++)
# ============================================================
run(r"""
-- selecao sintetica: 2 tiles com ground+borda+arvore, em 2 andares
-- camadas: 0=ground 1=borda 2=fundo 3=regular
local all = {
    { itemId = 100, layer = 0, rel = { x = 1, y = 0, z = 0 } },  -- ground
    { itemId = 200, layer = 1, rel = { x = 1, y = 0, z = 0 } },  -- borda
    { itemId = 900, layer = 3, rel = { x = 1, y = 0, z = 0 } },  -- arvore
    { itemId = 100, layer = 0, rel = { x = 0, y = 1, z = 1 } },  -- ground (z=1!)
    { itemId = 900, layer = 3, rel = { x = 0, y = 1, z = 1 } },  -- arvore
    { itemId = 800, layer = 2, rel = { x = 0, y = 1, z = 1 } },  -- fundo
    { itemId = 100, layer = 0, rel = { x = 0, y = 0, z = 0 } },  -- ground no centro
}
local rule = AD.assembleRuleFromItems(all, {
    name = "Sel",
    instanceCount = 2,
    minDistance = 5,
    densityPct = 100,
    roles = { [0] = 0, [1] = 0, [2] = 1, [3] = 1 }, -- defaults do C++
})
assert(rule.ruleMode == "cluster" and rule.instanceCount == 2, "base")
assert(math.abs(rule.density - 1.0) < 1e-9, "density")

-- cluster tiles: (x,y) com z DESCARTADO, ordenados por (x asc, y asc):
-- (0,0), (0,1), (1,0)
assert(#rule.clusterTiles == 3, "tiles: " .. #rule.clusterTiles)
assert(rule.clusterTiles[1].offset.x == 0 and rule.clusterTiles[1].offset.y == 0, "ordem 1")
assert(rule.clusterTiles[2].offset.x == 0 and rule.clusterTiles[2].offset.y == 1, "ordem 2")
assert(rule.clusterTiles[3].offset.x == 1 and rule.clusterTiles[3].offset.y == 0, "ordem 3")
assert(rule.clusterTiles[2].offset.z == 0, "z descartado [as-is]")
-- tile (1,0): ground (camada 0) ANTES da borda (camada 1)
assert(rule.clusterTiles[3].itemIds[1] == 100 and rule.clusterTiles[3].itemIds[2] == 200, "empilhamento ground->borda")

-- itens: camadas 2 e 3; ordenados por id asc; peso = ocorrencias * 100
assert(#rule.items == 2, "entradas: " .. #rule.items)
assert(rule.items[1].itemId == 800 and rule.items[1].weight == 100, "item 800")
assert(rule.items[2].itemId == 900 and rule.items[2].weight == 200, "item 900 (2x -> peso 200)")
print("OK rule-from-selection")
""")

# ============================================================
# 14. Round-trip XML: presetToString -> presetFromString
# ============================================================
run(r"""
local p0, err = AD.Xml.presetFromString(CIRCUS_XML)
assert(p0, tostring(err))
local xml = AD.Xml.presetToString(p0)
local p1, err2 = AD.Xml.presetFromString(xml)
assert(p1, tostring(err2))

assert(p1.name == p0.name, "nome")
assert(p1.spacing.minDistance == p0.spacing.minDistance, "spacing")
assert(p1.distribution.mode == p0.distribution.mode, "modo")
assert(p1.maxItemsTotal == p0.maxItemsTotal, "maxItems")
assert(p1.hasArea == p0.hasArea and p1.area.type == p0.area.type, "area")
assert(p1.area.rectMin.x == p0.area.rectMin.x and p1.area.rectMax.y == p0.area.rectMax.y, "rect")
assert(#p1.floorRules == #p0.floorRules, "qtd regras")
for i, r0 in ipairs(p0.floorRules) do
    local r1 = p1.floorRules[i]
    assert(r1.name == r0.name and r1.ruleMode == r0.ruleMode, "regra " .. i)
    assert(r1.fromFloorId == r0.fromFloorId and r1.toFloorId == r0.toFloorId, "faixa " .. i)
    assert(math.abs(r1.density - r0.density) < 1e-6, "density " .. i)
    assert(#r1.items == #r0.items, "itens " .. i)
    for j, e0 in ipairs(r0.items) do
        local e1 = r1.items[j]
        assert(e1.weight == e0.weight and e1.isComposite == e0.isComposite and e1.isCluster == e0.isCluster, "entry " .. j)
        assert(#e1.compositeTiles == #e0.compositeTiles, "tiles " .. j)
        for k, t0 in ipairs(e0.compositeTiles) do
            local t1 = e1.compositeTiles[k]
            assert(t1.offset.x == t0.offset.x and t1.offset.y == t0.offset.y, "offset " .. k)
            assert(#t1.itemIds == #t0.itemIds, "ids " .. k)
            for m, id in ipairs(t0.itemIds) do
                assert(t1.itemIds[m] == id, "id " .. m)
            end
        end
    end
end

-- escaping de nome + regra cluster com friend range
local D = AD.Defaults
local p2 = D.preset()
p2.name = 'A&B "c" <d>'
p2.hasArea = false
local r = D.floorRule()
r.name = "cl"
r.ruleMode = "cluster"
r.clusterTiles = { { offset = { x = -1, y = 2, z = 0 }, itemIds = { 55, 66 } } }
r.items = { D.itemEntry(7, 100) }
r.friendFromFloorId = 10
r.friendToFloorId = 20
r.friendChance = 50
p2.floorRules = { r }
local xml2 = AD.Xml.presetToString(p2)
local back = AD.Xml.presetFromString(xml2)
assert(back and back.name == p2.name, "escape: " .. tostring(back and back.name))
local br = back.floorRules[1]
assert(br.ruleMode == "cluster" and #br.clusterTiles == 1, "cluster rt")
assert(br.clusterTiles[1].offset.x == -1 and br.clusterTiles[1].itemIds[2] == 66, "cluster tile rt")
assert(br.friendFromFloorId == 10 and br.friendToFloorId == 20 and br.friendFloorId == 0, "friend rt")
print("OK round-trip XML")
""")

print("TODOS OS TESTES DO MOTOR PASSARAM")
