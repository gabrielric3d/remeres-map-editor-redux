"""Teste offline do nucleo WFC de main.lua (roda via lupa, sem o editor)."""
from lupa import LuaRuntime

src = open("scripts/map_synthesizer/main.lua", encoding="utf-8").read()
start = src.index("-- WFC - extracao de padroes")
end = src.index("local function applyResult")
core = src[start:end]

lua = LuaRuntime()
test = core + r"""
app = nil  -- nao usado: as funcoes extraidas so chamam app.yield
app = { yield = function() end }

-- amostra sintetica 14x14: moldura vazia (1), blob de ground (2) com franja (3)
EMPTY = 1 -- global: o codigo extraido de main.lua resolve EMPTY como global aqui
local rows = {}
for y = 1, 14 do
    rows[y] = {}
    for x = 1, 14 do
        rows[y][x] = 1
    end
end
for y = 4, 11 do
    for x = 4, 11 do
        rows[y][x] = 3 -- franja
    end
end
for y = 5, 10 do
    for x = 5, 10 do
        rows[y][x] = 2 -- ground principal
    end
end

local profile = { samples = { { w = 14, h = 14, rows = rows } } }
local N = 3
local patterns = compilePatterns(profile, N, true)
print("padroes:", #patterns)
local compat = buildCompat(patterns, N)

local allEmptySet = {}
for pid, pat in ipairs(patterns) do
    local allEmpty = true
    for _, c in ipairs(pat.cells) do
        if c ~= EMPTY then allEmpty = false break end
    end
    if allEmpty then allEmptySet[pid] = true end
end

local W, H = 24, 24
local PW, PH = W - N + 1, H - N + 1
local wave, count
for attempt = 1, 8 do
    math.randomseed(1000 + attempt)
    wave, count = solve(patterns, compat, PW, PH, { allEmptySet = allEmptySet })
    if wave then
        print("convergiu na tentativa " .. attempt)
        break
    end
end
assert(wave, "WFC nao convergiu em 8 tentativas")

local out = decode(wave, count, patterns, PW, PH, W, H, N)

-- validacoes
local counts = { 0, 0, 0 }
local chars = { ".", "#", "+" }
local lines = {}
for y = 1, H do
    local line = {}
    for x = 1, W do
        local v = out[y][x]
        assert(v ~= nil, "celula sem label em " .. x .. "," .. y)
        counts[v] = counts[v] + 1
        line[x] = chars[v]
        if x == 1 or y == 1 or x == W or y == H then
            assert(v == EMPTY, "moldura nao vazia em " .. x .. "," .. y)
        end
    end
    lines[y] = table.concat(line)
end
print(table.concat(lines, "\n"))
print(string.format("vazio=%d ground=%d franja=%d", counts[1], counts[2], counts[3]))
assert(counts[2] > 0, "nenhum ground gerado")
assert(counts[3] > 0, "nenhuma franja gerada")

-- adjacencia: ground principal (2) nunca pode encostar no vazio (1)
for y = 1, H do
    for x = 1, W do
        if out[y][x] == 2 then
            for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
                local nx, ny = x + d[1], y + d[2]
                if nx >= 1 and nx <= W and ny >= 1 and ny <= H then
                    assert(out[ny][nx] ~= EMPTY,
                        "ground encostou no vazio em " .. x .. "," .. y)
                end
            end
        end
    end
end
-- filtro de shapes: mantem so o maior componente conexo
local kept = filterShapes(out, 1)
assert(kept == 1, "esperava manter 1 shape, manteve " .. tostring(kept))
local remaining = filterShapes(out, 9999) -- recontagem: deve achar exatamente 1
assert(remaining == 1, "apos filtro deveria restar 1 componente, restou " .. remaining)
local nonEmpty = 0
for y = 1, H do
    for x = 1, W do
        if out[y][x] ~= EMPTY then nonEmpty = nonEmpty + 1 end
    end
end
print("shape final com " .. nonEmpty .. " tiles")
assert(nonEmpty > 0, "filtro apagou tudo")
-- ===== modo "Formato novo" (silhueta) =====
local profile2 = { samples = { { w = 14, h = 14, rows = rows } } }
local out2, kept2 = synthesizeShape(profile2, { seed = 7, sizePct = 1.0 }, 40, 40, function() end)
assert(out2, "synthesizeShape falhou: " .. tostring(kept2))

local W2, H2 = 40, 40
local lines2 = {}
local n2 = { 0, 0, 0 }
for y = 1, H2 do
    local line = {}
    for x = 1, W2 do
        local v = out2[y][x]
        n2[v] = n2[v] + 1
        line[x] = chars[v]
    end
    lines2[y] = table.concat(line)
end
print(table.concat(lines2, "\n"))
print(string.format("silhueta: vazio=%d ground=%d franja=%d", n2[1], n2[2], n2[3]))
assert(n2[2] > 0 and n2[3] > 0, "silhueta sem ground ou sem franja")

-- um unico componente
local remaining2 = filterShapes(out2, 9999)
assert(remaining2 == 1, "silhueta deveria ser 1 componente, achou " .. remaining2)

-- aneis: toda celula que encosta no vazio deve ser franja (3)
for y = 1, H2 do
    for x = 1, W2 do
        if out2[y][x] ~= EMPTY then
            local touches = false
            for dy = -1, 1 do
                for dx = -1, 1 do
                    local ny, nx = y + dy, x + dx
                    if ny < 1 or ny > H2 or nx < 1 or nx > W2 or out2[ny][nx] == EMPTY then
                        touches = true
                    end
                end
            end
            if touches then
                assert(out2[y][x] == 3, "borda do shape nao e franja em " .. x .. "," .. y)
            end
        end
    end
end
-- regra: nenhuma parede reta com 3+ celulas em sequencia no contorno
local function maxRunLen(isWall, outer, inner)
    -- generic scan: outer = primeira dimensao, inner = segunda
    local worst = 0
    for a = 1, outer do
        local run = 0
        for b = 1, inner + 1 do
            if b <= inner and isWall(a, b) then
                run = run + 1
                if run > worst then worst = run end
            else
                run = 0
            end
        end
    end
    return worst
end
local function filled(x, y)
    return x >= 1 and x <= W2 and y >= 1 and y <= H2 and out2[y][x] ~= EMPTY
end
local worst = 0
for _, dx in ipairs({ 1, -1 }) do
    local w = maxRunLen(function(x, y) return filled(x, y) and not filled(x + dx, y) end, W2, H2)
    if w > worst then worst = w end
end
for _, dy in ipairs({ 1, -1 }) do
    local w = maxRunLen(function(y, x) return filled(x, y) and not filled(x, y + dy) end, H2, W2)
    if w > worst then worst = w end
end
print("maior sequencia reta no contorno: " .. worst)
assert(worst <= 2, "contorno com " .. worst .. " bordas retas em sequencia")
-- ===== multiplos shapes com espacamento =====
local out3, placed3 = synthesizeShape(profile2,
    { seed = 11, sizePct = 1.0, maxShapes = 2, spacing = 4, shapeKind = "blob" },
    64, 40, function() end)
assert(out3, "multi-shape falhou: " .. tostring(placed3))
print("shapes colocados: " .. placed3)
assert(placed3 == 2, "esperava 2 shapes, colocou " .. placed3)
-- espacamento: nenhuma celula de um shape a menos de 4 tiles (chebyshev) de outro
do
    local comp = {}
    for y = 1, 40 do comp[y] = {} end
    local cid = 0
    local cellsByComp = {}
    for sy = 1, 40 do
        for sx = 1, 64 do
            if out3[sy][sx] ~= EMPTY and not comp[sy][sx] then
                cid = cid + 1
                cellsByComp[cid] = {}
                local stack = { { sx, sy } }
                comp[sy][sx] = cid
                while #stack > 0 do
                    local c = stack[#stack]
                    stack[#stack] = nil
                    table.insert(cellsByComp[cid], c)
                    for dy = -1, 1 do
                        for dx = -1, 1 do
                            local nx, ny = c[1] + dx, c[2] + dy
                            if nx >= 1 and nx <= 64 and ny >= 1 and ny <= 40
                                and out3[ny][nx] ~= EMPTY and not comp[ny][nx] then
                                comp[ny][nx] = cid
                                stack[#stack + 1] = { nx, ny }
                            end
                        end
                    end
                end
            end
        end
    end
    assert(cid == 2, "esperava 2 componentes, achou " .. cid)
    local minGap = 9999
    for _, a in ipairs(cellsByComp[1]) do
        for _, b in ipairs(cellsByComp[2]) do
            local g = math.max(math.abs(a[1] - b[1]), math.abs(a[2] - b[2])) - 1
            if g < minGap then minGap = g end
        end
    end
    print("menor folga entre shapes: " .. minGap .. " tiles")
    assert(minGap >= 4, "espacamento violado: " .. minGap)
end

-- ===== preencher a area com shapes (maxShapes = 0) =====
local out5, placed5 = synthesizeShape(profile2,
    { seed = 21, sizePct = 1.0, maxShapes = 0, spacing = 3, shapeKind = "blob" },
    90, 60, function() end)
assert(out5, "preenchimento falhou: " .. tostring(placed5))
print("preenchimento: " .. placed5 .. " shapes na area 90x60")
assert(placed5 >= 3, "esperava varios shapes preenchendo, colocou " .. placed5)

-- ===== caminho (rio) com largura de centro =====
local out4, placed4 = synthesizeShape(profile2,
    { seed = 3, sizePct = 1.0, shapeKind = "path", coreWidth = 8 },
    50, 30, function() end)
assert(out4, "path falhou: " .. tostring(placed4))
local lines4 = {}
for y = 1, 30 do
    local line = {}
    for x = 1, 50 do
        line[x] = chars[out4[y][x]]
    end
    lines4[y] = table.concat(line)
end
print(table.concat(lines4, "\n"))
do
    -- atravessa a area?
    local minx, maxx, miny, maxy = 999, 0, 999, 0
    local dist4max = 0
    local dist4 = distanceToEmpty(out4, 50, 30)
    for y = 1, 30 do
        for x = 1, 50 do
            if out4[y][x] ~= EMPTY then
                if x < minx then minx = x end
                if x > maxx then maxx = x end
                if y < miny then miny = y end
                if y > maxy then maxy = y end
                if dist4[y][x] and dist4[y][x] > dist4max then dist4max = dist4[y][x] end
            end
        end
    end
    local spanX, spanY = maxx - minx + 1, maxy - miny + 1
    print(string.format("rio: span=%dx%d, profundidade max=%d", spanX, spanY, dist4max))
    assert(spanX >= 30 or spanY >= 18, "caminho nao atravessa a area")
    -- coreWidth=8, fringeDepth=1 -> profundidade max >= 5
    assert(dist4max >= 5, "centro do rio fino demais: " .. dist4max)

    -- sem bolsoes vazios fechados dentro do rio
    local reach = {}
    for y = 1, 30 do reach[y] = {} end
    local q, qh = {}, 1
    local function push(x, y)
        if x >= 1 and x <= 50 and y >= 1 and y <= 30 and not reach[y][x] and out4[y][x] == EMPTY then
            reach[y][x] = true
            q[#q + 1] = { x, y }
        end
    end
    for x = 1, 50 do push(x, 1) push(x, 30) end
    for y = 1, 30 do push(1, y) push(50, y) end
    while qh <= #q do
        local c = q[qh]
        qh = qh + 1
        push(c[1] + 1, c[2]) push(c[1] - 1, c[2]) push(c[1], c[2] + 1) push(c[1], c[2] - 1)
    end
    for y = 1, 30 do
        for x = 1, 50 do
            assert(out4[y][x] ~= EMPTY or reach[y][x], "bolsao vazio fechado em " .. x .. "," .. y)
        end
    end
end
print("TODAS AS VALIDACOES PASSARAM")
"""
lua.execute(test)
