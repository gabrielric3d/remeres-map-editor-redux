-- Portas de spawn (BlackTalon SpawnsConfig): o tile que o jogador pisa para ir a uma hunt, o
-- destino e o que ele precisa para passar (level, proficiencia, storage). Desenha portas e destinos
-- no mapa e deixa marcar a porta e o destino pelo tile selecionado, editar os requisitos, criar,
-- apagar, conferir e salvar.
--
-- A fonte e o server: blacktalon-workspace\server\data\scripts\movement\spawn_portals.json, lido
-- por new_spawns.lua. O spawn_portals.json desta pasta e um HARD LINK para ele (o RME so le/grava
-- dentro da propria pasta de scripts), criado por .blacktalon/scripts/link_spawn_portals.cjs. Se
-- um git checkout/pull trocar o arquivo do server, o link quebra: rode o .cjs de novo.
--
-- O RME abre um mapa por vez e as portas da cidade (Mainland Evopolis.otbm) levam a hunts de
-- outro arquivo (Mainland.otbm): tile que nao esta no mapa aberto e AVISO, nao erro.
--
-- Encoding: o JSON e UTF-8 (e o overlay, NanoVG, tambem). As caixas da janela (wx) convertem pelo
-- codepage ANSI do Windows (1252): todo texto passa por toAnsi ao ir para a janela e por fromAnsi
-- ao voltar. O server converte para 1252 sozinho na hora de mandar.
--
-- Texto da propria tela so em ASCII (a camada Lua do editor quebra acento UTF-8).

if not app or not app.hasMap() or not app.map then
    print("[spawn_portals] no map open")
    if app then app.alert("Nenhum mapa aberto.") end
    return
end

local OVERLAY_ID = "bt_spawn_portals"
local ALL_GROUPS = "(todos)"
local store = app.storage("spawn_portals.json")

-- Rodar o script de novo executa o arquivo no mesmo estado Lua: o estado fica aqui.
BTSpawnPortals = BTSpawnPortals or {}
local S = BTSpawnPortals

-- ---------------------------------------------------------------------------
-- Texto: UTF-8 <-> Windows-1252
-- ---------------------------------------------------------------------------

local CP1252_HIGH = {
    [0x80] = 0x20AC, [0x82] = 0x201A, [0x83] = 0x0192, [0x84] = 0x201E, [0x85] = 0x2026,
    [0x86] = 0x2020, [0x87] = 0x2021, [0x88] = 0x02C6, [0x89] = 0x2030, [0x8A] = 0x0160,
    [0x8B] = 0x2039, [0x8C] = 0x0152, [0x8E] = 0x017D, [0x91] = 0x2018, [0x92] = 0x2019,
    [0x93] = 0x201C, [0x94] = 0x201D, [0x95] = 0x2022, [0x96] = 0x2013, [0x97] = 0x2014,
    [0x98] = 0x02DC, [0x99] = 0x2122, [0x9A] = 0x0161, [0x9B] = 0x203A, [0x9C] = 0x0153,
    [0x9E] = 0x017E, [0x9F] = 0x0178,
}
local UNICODE_TO_CP1252 = {}
for byte, cp in pairs(CP1252_HIGH) do UNICODE_TO_CP1252[cp] = byte end

-- UTF-8 (dado) -> 1252 (janela)
local function toAnsi(s)
    s = s or ""
    if not s:find("[\128-\255]") then return s end
    local ok, out = pcall(function()
        local parts = {}
        for _, cp in utf8.codes(s) do
            if cp < 0x80 or (cp >= 0xA0 and cp <= 0xFF) then
                parts[#parts + 1] = string.char(cp)
            else
                parts[#parts + 1] = UNICODE_TO_CP1252[cp] and string.char(UNICODE_TO_CP1252[cp]) or "?"
            end
        end
        return table.concat(parts)
    end)
    return ok and out or s
end

-- 1252 (janela) -> UTF-8 (dado)
local function fromAnsi(s)
    s = s or ""
    return (s:gsub("[\128-\255]", function(c)
        local b = c:byte()
        return utf8.char(CP1252_HIGH[b] or b)
    end))
end

local function trim(s) return ((s or ""):gsub("^%s+", ""):gsub("%s+$", "")) end

local function short(s, n)
    s = s or ""
    if #s > n then return s:sub(1, n) .. "..." end
    return s
end

-- ---------------------------------------------------------------------------
-- Dados
-- ---------------------------------------------------------------------------

local function int(v)
    v = tonumber(v)
    if not v then return nil end
    return math.floor(v)
end

local function toPos(p)
    if type(p) ~= "table" then return nil end
    local x, y, z = int(p.x), int(p.y), int(p.z)
    if not x or not y or not z then return nil end
    return { x = x, y = y, z = z }
end

local function posKey(p) return p.x .. "," .. p.y .. "," .. p.z end
local function posText(p) return p and posKey(p) or "-" end

-- requisito ausente/0 = sem exigencia: some do JSON
local function requirement(v)
    v = int(v)
    if v and v > 0 then return v end
    return nil
end

-- Os campos que a janela nao edita (areas, old, autoEnter) passam intactos para o Salvar.
local function normalize(p)
    p.name = p.name or ""
    p.group = p.group or ""
    if p.enabled == nil then p.enabled = true end
    p.pos = toPos(p.pos)
    p.destination = toPos(p.destination)
    p.level = requirement(p.level)
    p.proficiency = requirement(p.proficiency)
    if type(p.storage) == "table" and p.storage.key ~= nil and p.storage.key ~= "" then
        local s = p.storage
        s.value = int(s.value) or 1
        if type(s.message) == "table" then
            s.message.en, s.message.pt = s.message.en or "", s.message.pt or ""
        else
            s.message = nil
        end
    else
        p.storage = nil
    end
    if type(p.areas) == "table" and #p.areas == 0 then p.areas = nil end
end

local function loadData()
    local data = store:load()
    if type(data) ~= "table" or type(data.portals) ~= "table" then
        return false
    end
    for _, p in ipairs(data.portals) do normalize(p) end
    S.data = data
    S.dirty = false
    return true
end

if not loadData() then
    app.alert("Nao achei o spawn_portals.json valido em\n" .. tostring(store.path) ..
        "\n\nRode: node .blacktalon/scripts/link_spawn_portals.cjs (no blacktalon-workspace).")
    return
end

local function portals() return S.data.portals end
local function current() return S.sel and portals()[S.sel] or nil end

local function groups()
    local seen, out = {}, { ALL_GROUPS }
    for _, p in ipairs(portals()) do
        if not seen[p.group] then
            seen[p.group] = true
            out[#out + 1] = p.group
        end
    end
    return out
end

local function groupOptions()
    local out = {}
    for k, g in ipairs(groups()) do out[k] = toAnsi(g) end
    return out
end

-- os destinos da porta: o unico, ou os das areas do seletor
local function destinations(p)
    if p.destination then return { p.destination } end
    local out = {}
    for _, a in ipairs(p.areas or {}) do
        local d = toPos(a.destination)
        if d then out[#out + 1] = d end
    end
    return out
end

local function requirementsText(p)
    local parts = {}
    if p.level then parts[#parts + 1] = "lvl " .. p.level end
    if p.proficiency then parts[#parts + 1] = "prof " .. p.proficiency end
    if p.storage then parts[#parts + 1] = tostring(p.storage.key) .. ">=" .. p.storage.value end
    return #parts > 0 and table.concat(parts, ", ") or "sem requisito"
end

local function destinationText(p)
    if p.destination then return posText(p.destination) end
    if p.areas then return "seletor com " .. #p.areas .. " areas" end
    return "SEM DESTINO"
end

local function title(p)
    return string.format("%s%s  [%s]  %s -> %s  (%s)", p.enabled and "" or "[desligada] ", p.name,
        p.group, posText(p.pos), destinationText(p), requirementsText(p))
end

-- ---------------------------------------------------------------------------
-- Overlay
-- ---------------------------------------------------------------------------

local COLOR_ON = { 90, 200, 255 }
local COLOR_OFF = { 150, 150, 150 }
local COLOR_SEL = { 255, 214, 90 }
local COLOR_DEST = { 110, 230, 120 }

local function inView(v, p)
    return p.x >= v.x1 - 2 and p.x <= v.x2 + 2 and p.y >= v.y1 - 2 and p.y <= v.y2 + 2
end

-- indices das portas cuja porta esta no tile / cujo destino e o tile
local function portalsAt(x, y, z)
    local hits = {}
    for i, p in ipairs(portals()) do
        if p.pos and p.pos.x == x and p.pos.y == y and p.pos.z == z then hits[#hits + 1] = i end
    end
    return hits
end

local function arrivingAt(x, y, z)
    local hits = {}
    for i, p in ipairs(portals()) do
        for _, d in ipairs(destinations(p)) do
            if d.x == x and d.y == y and d.z == z then
                hits[#hits + 1] = i
                break
            end
        end
    end
    return hits
end

local function draw(ctx)
    local v = ctx.view
    local closeUp = (v.zoom or 1) <= 1.5
    for i, p in ipairs(portals()) do
        local sel = (i == S.sel)
        local c = sel and COLOR_SEL or (p.enabled and COLOR_ON or COLOR_OFF)
        -- a porta
        if p.pos and p.pos.z == v.z and inView(v, p.pos) then
            ctx.rect({ x = p.pos.x, y = p.pos.y, z = v.z, filled = true, color = { c[1], c[2], c[3], sel and 150 or 80 } })
            ctx.rect({ x = p.pos.x, y = p.pos.y, z = v.z, filled = false, width = sel and 3 or 1, color = { c[1], c[2], c[3], 255 } })
            if sel or closeUp then
                ctx.text({ x = p.pos.x, y = p.pos.y, z = v.z, text = short(p.name, 28), color = { c[1], c[2], c[3], 255 } })
            end
        end
        -- os destinos: so contorno; o da escolhida em destaque e ligado a porta quando no mesmo andar
        for _, d in ipairs(destinations(p)) do
            if d.z == v.z and inView(v, d) then
                local dc = sel and COLOR_SEL or COLOR_DEST
                ctx.rect({ x = d.x, y = d.y, z = v.z, filled = sel, style = "dashed", width = sel and 3 or 1,
                    color = { dc[1], dc[2], dc[3], sel and 110 or 200 } })
                if sel or closeUp then
                    ctx.text({ x = d.x, y = d.y, z = v.z, text = "<- " .. short(p.name, 24), color = { dc[1], dc[2], dc[3], 255 } })
                end
            end
            if sel and p.pos and p.pos.z == v.z and d.z == v.z then
                ctx.line({ x1 = p.pos.x, y1 = p.pos.y, z1 = v.z, x2 = d.x, y2 = d.y, z2 = v.z, style = "dashed", width = 2,
                    color = { COLOR_SEL[1], COLOR_SEL[2], COLOR_SEL[3], 200 } })
            end
        end
    end
end

-- erro de Lua em callback do overlay derruba o RME: tudo em pcall
local function hover(info)
    if not info or not info.pos then return nil end
    local x, y, z = info.pos.x, info.pos.y, info.pos.z
    local lines = {}
    for _, i in ipairs(portalsAt(x, y, z)) do
        local p = portals()[i]
        lines[#lines + 1] = string.format("PORTA #%d %s -> %s (%s)", i, p.name, destinationText(p), requirementsText(p))
    end
    for _, i in ipairs(arrivingAt(x, y, z)) do
        lines[#lines + 1] = string.format("DESTINO de #%d %s (porta %s)", i, portals()[i].name, posText(portals()[i].pos))
    end
    if #lines == 0 then return nil end
    return {
        tooltip = { text = table.concat(lines, "\n"), color = { 255, 255, 255 } },
        highlight = { color = { COLOR_SEL[1], COLOR_SEL[2], COLOR_SEL[3], 160 }, filled = false, width = 2 },
    }
end

pcall(function() app.mapView:removeOverlay(OVERLAY_ID) end)
app.mapView:addOverlay(OVERLAY_ID, {
    ondraw = function(ctx)
        local ok, err = pcall(draw, ctx)
        if not ok then print("[spawn_portals] draw: " .. tostring(err)) end
    end,
    onhover = function(info)
        local ok, res = pcall(hover, info)
        if ok then return res end
        print("[spawn_portals] hover: " .. tostring(res))
        return nil
    end,
})
pcall(function()
    app.mapView:registerShow("Portas de spawn (BlackTalon)", OVERLAY_ID, { enabled = true })
end)

-- o desenho e cacheado pela vista: setEnabled soma a revisao e refaz
local function redraw()
    app.mapView:setEnabled(OVERLAY_ID, true)
    app.refresh()
end

-- ---------------------------------------------------------------------------
-- Conferencia
-- ---------------------------------------------------------------------------

local MAX_ELSEWHERE_LINES = 15

local function runChecks()
    local errors, warnings, elsewhere = {}, {}, {}
    local function bad(fmt, ...) errors[#errors + 1] = string.format(fmt, ...) end
    local function warn(fmt, ...) warnings[#warnings + 1] = string.format(fmt, ...) end

    local byPos = {}
    for i, p in ipairs(portals()) do
        if p.enabled and p.pos then byPos[posKey(p.pos)] = byPos[posKey(p.pos)] or i end
    end

    -- tile da porta/destino: fora do mapa aberto = aviso; bloqueado = erro
    local function checkTile(label, what, pos)
        local t = app.map:getTile(pos.x, pos.y, pos.z)
        if not t or not t.ground then
            elsewhere[#elsewhere + 1] = string.format("%s: %s (%s)", label, what, posKey(pos))
            return
        end
        if t.isBlocking then bad("%s BLOQUEADO: %s em (%s) -- o jogador nao fica nesse tile", what:upper(), label, posKey(pos)) end
    end

    local seen = {}
    for i, p in ipairs(portals()) do
        local label = string.format("#%d %s", i, p.name)
        if p.enabled then
            if not p.pos then
                bad("SEM PORTA: %s", label)
            else
                local key = posKey(p.pos)
                if seen[key] then
                    bad("MESMA PORTA: %s e %s em (%s) -- o server ignora a segunda", seen[key], label, key)
                else
                    seen[key] = label
                end
                checkTile(label, "porta", p.pos)
            end
            if not p.destination and not p.areas then
                bad("SEM DESTINO: %s -- o server desliga a porta", label)
            end
            for k, d in ipairs(destinations(p)) do
                local what = p.areas and ("destino da area " .. k) or "destino"
                checkTile(label, what, d)
                local other = byPos[posKey(d)]
                if other then
                    bad("DESTINO EM CIMA DE PORTA: %s cai em (%s), que e a porta de #%d %s", label, posKey(d), other, portals()[other].name)
                end
            end
            if p.areas then
                for k, a in ipairs(p.areas) do
                    if not toPos(a.destination) then bad("AREA SEM DESTINO: %s, area %d", label, k) end
                end
            end
            if p.storage then
                local m = p.storage.message
                if not m or m.en == "" then
                    warn("SEM MENSAGEM: %s -- quem nao tem a storage ve o texto generico", label)
                elseif m.pt == "" then
                    warn("SEM PT: %s -- o jogador PT ve a mensagem em ingles", label)
                end
            end
        end
    end

    local lines = {}
    for _, l in ipairs(errors) do lines[#lines + 1] = l end
    if #warnings > 0 then
        lines[#lines + 1] = ""
        lines[#lines + 1] = "Avisos:"
        for _, l in ipairs(warnings) do lines[#lines + 1] = "  " .. l end
    end
    if #elsewhere > 0 then
        lines[#lines + 1] = ""
        lines[#lines + 1] = string.format("%d tile(s) fora do mapa aberto (%s) ou sem chao -- abra o outro mapa e confira de novo:",
            #elsewhere, tostring(app.map.name))
        for k = 1, math.min(#elsewhere, MAX_ELSEWHERE_LINES) do lines[#lines + 1] = "  " .. elsewhere[k] end
        if #elsewhere > MAX_ELSEWHERE_LINES then
            lines[#lines + 1] = string.format("  ... e mais %d (lista inteira no console de scripts)", #elsewhere - MAX_ELSEWHERE_LINES)
        end
    end
    if #errors == 0 then
        table.insert(lines, 1, "Nenhum erro: toda porta ligada tem tile proprio e destino, e nada cai em tile bloqueado ou em outra porta.")
    end
    print("[spawn_portals] fora do mapa aberto:\n  " .. table.concat(elsewhere, "\n  "))
    return table.concat(lines, "\n"), #errors, #warnings, #elsewhere
end

-- ---------------------------------------------------------------------------
-- A janela
-- ---------------------------------------------------------------------------

S.group = S.group or ALL_GROUPS
do
    local ok = false
    for _, g in ipairs(groups()) do ok = ok or g == S.group end
    if not ok then S.group = ALL_GROUPS end
end
if not current() then S.sel = portals()[1] and 1 or nil end

local function visible()
    local out = {}
    for i, p in ipairs(portals()) do
        if S.group == ALL_GROUPS or p.group == S.group then out[#out + 1] = i end
    end
    return out
end

local function statusText()
    return (S.dirty and "ALTERACOES NAO SALVAS. " or "Salvo. ") .. #portals() .. " portas. Arquivo: " .. tostring(store.path)
end

local function listItems()
    local items, sel = {}, 0
    for row, i in ipairs(visible()) do
        items[#items + 1] = { text = toAnsi(string.format("#%-3d %s", i, title(portals()[i]))) }
        if i == S.sel then sel = row end
    end
    return items, sel
end

local function fieldsOf(p)
    p = p or { name = "", group = "", enabled = true }
    local s = p.storage or {}
    local m = s.message or {}
    return {
        name = { text = toAnsi(p.name) },
        group = { text = toAnsi(p.group) },
        enabled = { selected = p.enabled ~= false },
        level = { value = p.level or 0 },
        proficiency = { value = p.proficiency or 0 },
        storage_key = { text = toAnsi(s.key and tostring(s.key) or "") },
        storage_value = { value = s.value or 1 },
        msg_en = { text = toAnsi(m.en or "") },
        msg_pt = { text = toAnsi(m.pt or "") },
    }
end

-- ao escrever nos campos o wx dispara eventos de texto; com isso ligado o commit nao le campo
-- pela metade
local filling = false

local function refresh(d)
    filling = true
    local exists = false
    for _, g in ipairs(groups()) do exists = exists or g == S.group end
    if not exists then S.group = ALL_GROUPS end
    local items, sel = listItems()
    local mods = fieldsOf(current())
    mods.group_filter = { options = groupOptions(), option = toAnsi(S.group) }
    mods.list = { items = items, selection = sel }
    local p = current()
    mods.lbl_where = { text = p and string.format("Porta: %s    Destino: %s", posText(p.pos), destinationText(p)) or "Nenhuma porta escolhida." }
    mods.lbl_status = { text = statusText() }
    d:modify(mods)
    filling = false
end

local function changed(d)
    S.dirty = true
    refresh(d)
    redraw()
end

local function sameMessage(a, b)
    if not a or not b then return a == b end
    return a.en == b.en and a.pt == b.pt
end

-- grava os campos da janela na porta escolhida; true se algo mudou. Todo botao chama isto antes
-- de agir, entao trocar de porta ou salvar nunca perde o que foi digitado.
local function commit(d)
    local p = current()
    if not p or filling then return false end
    local f = d.data
    local key = trim(fromAnsi(f.storage_key))
    local storage = nil
    if key ~= "" then
        local en, pt = trim(fromAnsi(f.msg_en)), trim(fromAnsi(f.msg_pt))
        storage = {
            key = tonumber(key) or key,
            value = int(f.storage_value) or 1,
            message = (en ~= "" or pt ~= "") and { en = en, pt = pt } or nil,
        }
    end
    local new = {
        name = trim(fromAnsi(f.name)),
        group = trim(fromAnsi(f.group)),
        enabled = f.enabled and true or false,
        level = requirement(f.level),
        proficiency = requirement(f.proficiency),
    }
    local old = p.storage
    local storageChanged = (old == nil) ~= (storage == nil)
        or (old and storage and (old.key ~= storage.key or old.value ~= storage.value or not sameMessage(old.message, storage.message)))
    local dirty = new.name ~= p.name or new.group ~= p.group or new.enabled ~= p.enabled
        or new.level ~= p.level or new.proficiency ~= p.proficiency or storageChanged
    if not dirty then return false end
    p.name, p.group, p.enabled = new.name, new.group, new.enabled
    p.level, p.proficiency, p.storage = new.level, new.proficiency, storage
    S.dirty = true
    return true
end

local function goTo(pos)
    if pos then app.setCameraPosition(pos.x, pos.y, pos.z) end
end

local function selectedTile()
    if (app.selection.size or 0) == 0 then return nil end
    local b = app.selection.bounds
    if b.min.x ~= b.max.x or b.min.y ~= b.max.y then return nil end
    return { x = b.min.x, y = b.min.y, z = b.min.z }
end

-- Os botoes agem no tile SELECIONADO, nao no ultimo do mouse: ate o mouse chegar na janela ele
-- passa por outros tiles.
local function needTile()
    local pos = selectedTile()
    if not pos then
        app.alert("Selecione UM tile no mapa: ferramenta de selecao, clique no tile.")
    end
    return pos
end

local function focus(d, i)
    S.sel = i
    if S.group ~= ALL_GROUPS and current().group ~= S.group then S.group = ALL_GROUPS end
    refresh(d)
    redraw()
end

-- escolhe a porta da lista de hits; de novo no mesmo tile passa para a proxima
local function pickFrom(d, hits)
    if #hits == 0 then return false end
    commit(d)
    local nextIdx = hits[1]
    for k, i in ipairs(hits) do
        if i == S.sel then nextIdx = hits[k % #hits + 1] end
    end
    focus(d, nextIdx)
    return true
end

-- rodar de novo fecha a janela anterior (senao acumulam)
if S.dlg then pcall(function() S.dlg:close() end) end
if S.selectionListener then pcall(function() app.events:off(S.selectionListener) end) end
S.runChecks = runChecks

local dlg = Dialog({ title = "Portas de spawn - SpawnsConfig (BlackTalon)", width = 980, height = 820, resizable = true,
    onclose = function()
        if S.selectionListener then pcall(function() app.events:off(S.selectionListener) end) end
        S.selectionListener = nil
    end })
S.dlg = dlg

dlg:label({ text = "Cheio = a porta (o tile que o jogador pisa); tracejado verde = destino. Amarelo = escolhida, azul = ligada, cinza = desligada." })
dlg:newrow()
dlg:label({ text = "Clicar (selecionar) o tile de uma porta no mapa ja escolhe ela aqui." })
dlg:newrow()
dlg:combobox({
    id = "group_filter",
    label = "Grupo",
    options = groupOptions(),
    option = toAnsi(S.group),
    onchange = function(d)
        commit(d)
        S.group = fromAnsi(d.data.group_filter)
        local vis = visible()
        local keep = false
        for _, i in ipairs(vis) do keep = keep or i == S.sel end
        if not keep then S.sel = vis[1] end
        refresh(d)
        redraw()
    end,
})
dlg:newrow()
dlg:list({
    id = "list",
    height = 260,
    expand = true,
    items = (listItems()),
    onchange = function(d)
        commit(d)
        local i = visible()[d.data.list]
        if i then S.sel = i end
        refresh(d)
        redraw()
        local p = current()
        goTo(p and p.pos)
    end,
})
dlg:newrow()

dlg:box({ orient = "vertical", label = "Porta escolhida (os campos valem ao clicar em qualquer botao)", padding = 4, margin = 4, expand = false })
dlg:wrap({ orient = "horizontal" })
dlg:input({ id = "name", label = "Nome", text = "", width = 260 })
dlg:input({ id = "group", label = "Grupo", text = "", width = 160 })
dlg:check({ id = "enabled", text = "Ligada", selected = true })
dlg:endwrap()
dlg:wrap({ orient = "horizontal" })
dlg:number({ id = "level", label = "Level minimo (0 = nenhum)", value = 0, min = 0, max = 100000 })
dlg:number({ id = "proficiency", label = "Proficiencia minima (0 = nenhuma)", value = 0, min = 0, max = 100000 })
dlg:endwrap()
dlg:wrap({ orient = "horizontal" })
dlg:input({ id = "storage_key", label = "Storage (nome em PlayerStorageKeys, vazio = nenhuma)", text = "", width = 220 })
dlg:number({ id = "storage_value", label = "Valor minimo", value = 1, min = -1000000000, max = 1000000000 })
dlg:endwrap()
for _, f in ipairs({ { "msg_en", "Mensagem sem a storage EN" }, { "msg_pt", "Mensagem sem a storage PT" } }) do
    dlg:wrap({ orient = "horizontal" })
    dlg:input({ id = f[1], label = f[2], text = "", width = 680 })
    dlg:endwrap()
end
dlg:label({ text = "Storage com nome que nao existe no storages.lua: o server DESLIGA a porta e avisa no /spawnportals reload." })
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Porta e destino", padding = 4, margin = 4, expand = false })
dlg:label({ id = "lbl_where", text = "" })
dlg:button({
    text = "Porta = tile selecionado",
    onclick = function(d)
        commit(d)
        local p, pos = current(), needTile()
        if not p or not pos then return end
        p.pos = pos
        changed(d)
    end,
})
dlg:button({
    text = "Destino = tile selecionado",
    onclick = function(d)
        commit(d)
        local p, pos = current(), needTile()
        if not p or not pos then return end
        if p.areas then
            app.alert("Esta porta e um seletor com " .. #p.areas .. " areas: os destinos ficam nas areas (edite no JSON).\n" ..
                "Um destino unico passaria por cima do seletor.")
            return
        end
        p.destination = pos
        changed(d)
    end,
})
dlg:button({ text = "Ir ate a porta", onclick = function(d) commit(d); local p = current(); goTo(p and p.pos) end })
dlg:button({
    text = "Ir ate o destino",
    onclick = function(d)
        commit(d)
        local p = current()
        if not p then return end
        -- seletor: cada clique vai para a proxima area
        local list = destinations(p)
        if #list == 0 then return end
        S.destCycle = ((S.destCycle or 0) % #list) + 1
        goTo(list[S.destCycle])
    end,
})
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Portas", padding = 4, margin = 4, expand = false })
dlg:button({
    text = "Nova porta no tile selecionado",
    onclick = function(d)
        commit(d)
        local pos = needTile()
        if not pos then return end
        if #portalsAt(pos.x, pos.y, pos.z) > 0 then
            app.alert("Ja tem porta nesse tile.")
            return
        end
        -- herda grupo e requisitos da escolhida: as portas de uma regiao costumam ser iguais
        local base = current()
        local p = {
            name = "nova",
            group = (S.group ~= ALL_GROUPS and S.group) or (base and base.group) or "",
            enabled = true,
            pos = pos,
            level = base and base.level or nil,
            proficiency = base and base.proficiency or nil,
        }
        if base and base.storage then
            local bs = base.storage
            p.storage = { key = bs.key, value = bs.value,
                message = bs.message and { en = bs.message.en, pt = bs.message.pt } or nil }
        end
        local at = (S.sel or #portals()) + 1
        table.insert(portals(), at, p)
        S.sel = at
        if S.group ~= ALL_GROUPS and p.group ~= S.group then S.group = ALL_GROUPS end
        d:modify({ group_filter = { options = groupOptions(), option = toAnsi(S.group) } })
        changed(d)
        app.alert("Porta criada (com os requisitos da anterior). Agora selecione o tile do destino e clique \"Destino = tile selecionado\".")
    end,
})
dlg:button({
    text = "Escolher a do tile selecionado",
    onclick = function(d)
        local pos = needTile()
        if pos and not pickFrom(d, portalsAt(pos.x, pos.y, pos.z)) then app.alert("Nenhuma porta no tile selecionado.") end
    end,
})
dlg:button({
    text = "Quem leva para o tile selecionado",
    onclick = function(d)
        local pos = needTile()
        if pos and not pickFrom(d, arrivingAt(pos.x, pos.y, pos.z)) then app.alert("Nenhuma porta tem esse tile como destino.") end
    end,
})
dlg:button({
    text = "Apagar a escolhida",
    onclick = function(d)
        local p = current()
        if not p then return end
        -- o alert so mostra OK/Cancelar (o rotulo dos botoes e ignorado); OK = 1
        local r = app.alert({ title = "Apagar porta", text = "Apagar a porta \"" .. toAnsi(p.name) .. "\"?", buttons = { "OK", "Cancel" } })
        if r ~= 1 then return end
        table.remove(portals(), S.sel)
        if #visible() == 0 then S.group = ALL_GROUPS end
        S.sel = math.min(S.sel, #portals())
        if S.sel < 1 then S.sel = nil end
        d:modify({ group_filter = { options = groupOptions(), option = toAnsi(S.group) } })
        changed(d)
    end,
})
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", padding = 4, margin = 4, expand = false })
dlg:button({
    text = "Conferir",
    onclick = function(d)
        if commit(d) then refresh(d) end
        local text, errors, warnings, elsewhere = runChecks()
        print("[spawn_portals] conferencia:\n" .. text)
        app.alert(toAnsi(string.format("%d erro(s), %d aviso(s), %d fora deste mapa.\n\n", errors, warnings, elsewhere) .. text))
    end,
})
dlg:button({
    text = "Recarregar do arquivo",
    onclick = function(d)
        if loadData() then
            if not current() then S.sel = portals()[1] and 1 or nil end
            S.group = ALL_GROUPS
            d:modify({ group_filter = { options = groupOptions(), option = toAnsi(S.group) } })
            refresh(d)
            redraw()
        else
            app.alert("spawn_portals.json invalido ou ausente: " .. tostring(store.path))
        end
    end,
})
dlg:button({
    text = "Salvar",
    onclick = function(d)
        commit(d)
        if store:save(S.data) then
            S.dirty = false
            refresh(d)
            app.alert("Salvo.\nNo server: /spawnportals reload (GM) para valer sem reiniciar.")
        else
            app.alert("Falha ao gravar " .. tostring(store.path))
        end
    end,
})
dlg:button({ text = "Fechar", onclick = function(d) d:close() end })
dlg:endbox()
dlg:newrow()
dlg:label({ id = "lbl_status", text = statusText() })

dlg:show({ wait = false })
refresh(dlg)
redraw()

-- clicar (selecionar) o tile de uma porta ja a escolhe na janela; erro aqui derrubaria o RME
S.selectionListener = app.events:on("selectionChange", function()
    pcall(function()
        local pos = selectedTile()
        if pos and S.dlg == dlg then pickFrom(dlg, portalsAt(pos.x, pos.y, pos.z)) end
    end)
end)
