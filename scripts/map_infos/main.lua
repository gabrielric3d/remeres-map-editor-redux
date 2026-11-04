-- Paineis de informacao no mapa (BlackTalon MapInfos): o texto que o client desenha em cima de um
-- tile (Training AFK, Dungeons, as instrucoes dos eventos...). Desenha cada painel no mapa e deixa
-- criar, mover, duplicar, editar e apagar, conferir e salvar.
--
-- A fonte e o server: blacktalon-workspace\server\data\scripts\1loaded\map_infos.json, lido por
-- 2mapinfos.lua. O map_infos.json desta pasta e um HARD LINK para ele (o RME so le/grava dentro da
-- propria pasta de scripts), criado por .blacktalon/scripts/link_map_infos.cjs. Se um git
-- checkout/pull trocar o arquivo do server, o link quebra: rode o .cjs de novo.
--
-- Encoding: o JSON e UTF-8 (e o overlay, NanoVG, tambem). As caixas da janela (wx) convertem pelo
-- codepage ANSI do Windows (1252): por isso todo texto passa por toAnsi ao ir para a janela e por
-- fromAnsi ao voltar. O server converte para 1252 sozinho na carga (a fonte do client e 1252).
--
-- Texto da propria tela so em ASCII (a camada Lua do editor quebra acento UTF-8).

if not app or not app.hasMap() or not app.map then
    print("[map_infos] no map open")
    if app then app.alert("Nenhum mapa aberto.") end
    return
end

local OVERLAY_ID = "bt_map_infos"
local ALL_GROUPS = "(todos)"
local store = app.storage("map_infos.json")

-- Rodar o script de novo executa o arquivo no mesmo estado Lua: o estado fica aqui.
BTMapInfos = BTMapInfos or {}
local S = BTMapInfos

-- ---------------------------------------------------------------------------
-- Texto: UTF-8 <-> Windows-1252, "\n" na caixa de uma linha, texto sem as tags de cor
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

-- a caixa e de uma linha so: a quebra aparece como \n escrito
local function escapeNl(s) return ((s or ""):gsub("\n", "\\n")) end
local function unescapeNl(s) return ((s or ""):gsub("\r", ""):gsub("\\n", "\n")) end

-- RME com input({multiline=true}) (lua_dialog.cpp de 2026-11-02 em diante): EN/PT viram caixas
-- altas com quebra de linha de verdade. No executavel antigo ficam de uma linha, com \n escrito.
local MULTILINE = type(dialog) == "table" and dialog.multilineInput == true
local function boxText(s) return MULTILINE and (s or "") or escapeNl(s) end

local function plain(s)
    s = (s or ""):gsub("<color=[^>]*>", ""):gsub("</color>", "")
    return (s:gsub("\n+$", ""):gsub("\n", " / "))
end

local function short(s, n)
    if utf8.len(s) and utf8.len(s) > n then
        return s:sub(1, utf8.offset(s, n + 1) - 1) .. "..."
    end
    return s
end

-- ---------------------------------------------------------------------------
-- Dados
-- ---------------------------------------------------------------------------

local function normalize(w)
    if type(w.pos) ~= "table" then w.pos = {} end
    if type(w.text) ~= "table" then w.text = { en = tostring(w.text or ""), pt = "" } end
    w.text.en, w.text.pt = w.text.en or "", w.text.pt or ""
    w.name, w.group, w.title = w.name or "", w.group or "", w.title or ""
    if w.enabled == nil then w.enabled = true end
    w.marginx, w.marginy = tonumber(w.marginx) or 0, tonumber(w.marginy) or 0
    w.width = tonumber(w.width) or 120
    w.blockhide = w.blockhide == true
end

local function loadData()
    local data = store:load()
    if type(data) ~= "table" or type(data.widgets) ~= "table" then
        return false
    end
    for _, w in ipairs(data.widgets) do normalize(w) end
    S.data = data
    S.dirty = false
    return true
end

if not loadData() then
    app.alert("Nao achei o map_infos.json valido em\n" .. tostring(store.path) ..
        "\n\nRode: node .blacktalon/scripts/link_map_infos.cjs (no blacktalon-workspace).")
    return
end

S.found = S.found or {}   -- [actionId] = { {x,y,z}, ... } achados pelo "Procurar actionIds"

local function widgets() return S.data.widgets end
local function current() return widgets()[S.sel] end

local function groups()
    local seen, out = {}, { ALL_GROUPS }
    for _, w in ipairs(widgets()) do
        if not seen[w.group] then
            seen[w.group] = true
            out[#out + 1] = w.group
        end
    end
    return out
end

-- o combobox e wx: os nomes vao em 1252
local function groupOptions()
    local out = {}
    for k, g in ipairs(groups()) do out[k] = toAnsi(g) end
    return out
end

-- os tiles onde o painel aparece: pos, ou (painel por actionId) o tile do item achado no mapa
-- deslocado pelo anchorOffset, como o MapInfos.ParseActionIdPos faz no server
local function anchors(w)
    if w.actionId and (tonumber(w.actionId) or 0) > 0 and #w.pos == 0 then
        local out = {}
        local off = w.anchorOffset or {}
        for _, p in ipairs(S.found[math.floor(w.actionId)] or {}) do
            out[#out + 1] = { x = p.x + (off.x or 0), y = p.y + (off.y or 0), z = p.z, byActionId = true }
        end
        return out
    end
    return w.pos
end

local function posText(w)
    local aid = math.floor(tonumber(w.actionId) or 0)
    if aid > 0 and #w.pos == 0 then
        local n = S.found[aid] and #S.found[aid]
        return string.format("actionId %d%s", aid, n and (" - " .. n .. " no mapa") or "")
    end
    local p = w.pos[1]
    if not p then return "SEM POSICAO" end
    return string.format("%d,%d,%d%s", p.x, p.y, p.z, #w.pos > 1 and (" +" .. (#w.pos - 1)) or "")
end

local function title(w)
    return string.format("%s%s - %s  (%s)", w.enabled and "" or "[desligado] ", w.name,
        short(plain(w.text.en), 60), posText(w))
end

-- ---------------------------------------------------------------------------
-- Overlay
-- ---------------------------------------------------------------------------

local TILE = 32
local LINE_PX = 14

-- o retangulo do painel no jogo, aproximado em tiles: o client poe o painel no canto do tile e
-- soma marginx/marginy em pixels; a altura cresce com as linhas de texto
local function panelRect(w, p)
    local lines = select(2, (w.text.en or ""):gsub("\n", "\n")) + 1
    local heightPx = lines * LINE_PX + 15 + (w.title ~= "" and 16 or 0)
    local x = p.x + math.floor(w.marginx / TILE + 0.5)
    local y = p.y + math.floor(w.marginy / TILE + 0.5)
    return x, y, math.max(1, math.ceil(w.width / TILE)), math.max(1, math.ceil(heightPx / TILE))
end

local function inView(v, x, y, w, h)
    return x + (w or 1) >= v.x1 - 2 and x <= v.x2 + 2 and y + (h or 1) >= v.y1 - 2 and y <= v.y2 + 2
end

local COLOR_ON = { 90, 200, 255 }
local COLOR_OFF = { 150, 150, 150 }
local COLOR_SEL = { 255, 214, 90 }

local function hitAt(x, y, z)
    local hits = {}
    for i, w in ipairs(widgets()) do
        for _, p in ipairs(anchors(w)) do
            if p.x == x and p.y == y and p.z == z then
                hits[#hits + 1] = i
                break
            end
        end
    end
    return hits
end

pcall(function() app.mapView:removeOverlay(OVERLAY_ID) end)
app.mapView:addOverlay(OVERLAY_ID, {
    ondraw = function(ctx)
        local v = ctx.view
        local closeUp = (v.zoom or 1) <= 1.5
        for i, w in ipairs(widgets()) do
            local sel = (i == S.sel)
            local c = sel and COLOR_SEL or (w.enabled and COLOR_ON or COLOR_OFF)
            for _, p in ipairs(anchors(w)) do
                if p.z == v.z then
                    local px, py, pw, ph = panelRect(w, p)
                    if inView(v, px, py, pw, ph) or inView(v, p.x, p.y) then
                        -- o tile do painel (o que vai no JSON)
                        ctx.rect({ x = p.x, y = p.y, z = v.z, filled = true, color = { c[1], c[2], c[3], sel and 140 or 70 } })
                        ctx.rect({ x = p.x, y = p.y, z = v.z, filled = false, width = sel and 3 or 1,
                            color = { c[1], c[2], c[3], 255 } })
                        -- onde o painel fica na tela do jogo (aproximado)
                        ctx.rect({ x = px, y = py, z = v.z, w = pw, h = ph, filled = sel, style = "dashed",
                            width = sel and 2 or 1, color = { c[1], c[2], c[3], sel and 40 or 150 } })
                        if sel or closeUp then
                            ctx.text({ x = px, y = py, z = v.z, text = short(plain(w.text.en), 40),
                                color = { c[1], c[2], c[3], 255 } })
                        end
                    end
                end
            end
        end
    end,
    onhover = function(info)
        if not info.pos then return nil end
        S.hover = { x = info.pos.x, y = info.pos.y, z = info.pos.z }
        local hits = hitAt(info.pos.x, info.pos.y, info.pos.z)
        if #hits == 0 then return nil end
        local lines = {}
        for _, i in ipairs(hits) do
            local w = widgets()[i]
            lines[#lines + 1] = string.format("#%d %s: %s", i, w.name, short(plain(w.text.en), 70))
        end
        if #hits > 1 then lines[#lines + 1] = "MESMO TILE: no jogo so um aparece" end
        return {
            tooltip = { text = table.concat(lines, "\n"), color = { 255, 255, 255 } },
            highlight = { color = { COLOR_SEL[1], COLOR_SEL[2], COLOR_SEL[3], 160 }, filled = false, width = 2 },
        }
    end,
})
pcall(function()
    app.mapView:registerShow("Paineis do MapInfos (BlackTalon)", OVERLAY_ID, { enabled = true })
end)

-- o desenho e cacheado pela vista: setEnabled soma a revisao e refaz
local function redraw()
    app.mapView:setEnabled(OVERLAY_ID, true)
    app.refresh()
end

-- ---------------------------------------------------------------------------
-- Procurar no mapa os itens dos paineis por actionId
-- ---------------------------------------------------------------------------

local function scanActionIds()
    local wanted, any = {}, false
    for _, w in ipairs(widgets()) do
        local aid = math.floor(tonumber(w.actionId) or 0)
        if aid > 0 then
            wanted[aid] = true
            any = true
        end
    end
    if not any then return 0 end
    local found, n, total = {}, 0, 0
    for aid in pairs(wanted) do found[aid] = {} end
    local function check(tile, item)
        local aid = item and item.actionId
        if aid and wanted[aid] then
            local list = found[aid]
            list[#list + 1] = { x = tile.x, y = tile.y, z = tile.z }
            total = total + 1
        end
    end
    for tile in app.map.tiles do
        check(tile, tile.ground)
        local items = tile.items
        if items then
            for k = 1, #items do check(tile, items[k]) end
        end
        n = n + 1
        if n % 20000 == 0 then app.yield() end
    end
    S.found = found
    return total
end

-- ---------------------------------------------------------------------------
-- Conferencia
-- ---------------------------------------------------------------------------

local function runChecks()
    local lines, problems = {}, 0
    local function bad(fmt, ...)
        problems = problems + 1
        lines[#lines + 1] = string.format(fmt, ...)
    end
    local seen = {}
    for i, w in ipairs(widgets()) do
        local label = string.format("#%d %s", i, w.name)
        if w.enabled then
            local aid = math.floor(tonumber(w.actionId) or 0)
            if #w.pos == 0 and aid == 0 then
                bad("SEM POSICAO: %s (nem pos nem actionId)", label)
            end
            if aid > 0 and #w.pos == 0 and S.found[aid] and #S.found[aid] == 0 then
                bad("ACTIONID SEM ITEM: %s -- nenhum item com actionId %d no mapa", label, aid)
            end
            if w.text.en == "" then bad("SEM TEXTO: %s (en vazio)", label) end
            if w.text.pt == "" then bad("SEM PT: %s (o jogador PT ve o EN)", label) end
            if w.width <= 0 then bad("LARGURA: %s com width %d", label, w.width) end
            for _, field in ipairs({ "en", "pt" }) do
                local s = w.text[field]
                local open = select(2, s:gsub("<color=", ""))
                local close = select(2, s:gsub("</color>", ""))
                if open ~= close then
                    bad("TAG DE COR: %s (%s) com %d <color> e %d </color>", label, field, open, close)
                end
            end
            for _, p in ipairs(anchors(w)) do
                -- o client indexa painel por "x,y,z": dois no mesmo tile, um apaga o outro
                local key = p.x .. "," .. p.y .. "," .. p.z
                if seen[key] then
                    bad("MESMO TILE: %s e %s em (%s) -- no jogo so um aparece", label, seen[key], key)
                else
                    seen[key] = label
                end
                -- sem tile no client nao ha onde pendurar o painel (mapinfo.lua: if not tile)
                if not p.byActionId then
                    local t = app.map:getTile(p.x, p.y, p.z)
                    if not t or not t.ground then bad("SEM CHAO: %s em (%s) -- o painel nao aparece", label, key) end
                end
            end
        end
    end
    if problems == 0 then
        lines[#lines + 1] = "Tudo certo: todo painel ligado tem posicao com chao, texto EN e PT, tags de cor fechadas e nenhum divide tile."
    end
    if not next(S.found) then
        lines[#lines + 1] = "\n(Paineis por actionId so sao conferidos depois do \"Procurar actionIds no mapa\".)"
    end
    return table.concat(lines, "\n"), problems
end

-- ---------------------------------------------------------------------------
-- A janela
-- ---------------------------------------------------------------------------

S.group = S.group or ALL_GROUPS
if S.group ~= ALL_GROUPS then
    local ok = false
    for _, g in ipairs(groups()) do ok = ok or g == S.group end
    if not ok then S.group = ALL_GROUPS end
end
if not current() then S.sel = widgets()[1] and 1 or nil end

-- os indices de widgets() que o filtro de grupo mostra na lista
local function visible()
    local out = {}
    for i, w in ipairs(widgets()) do
        if S.group == ALL_GROUPS or w.group == S.group then out[#out + 1] = i end
    end
    return out
end

local function statusText()
    return (S.dirty and "ALTERACOES NAO SALVAS. " or "Salvo. ") .. #widgets() .. " paineis. Arquivo: " .. tostring(store.path)
end

local function listItems()
    local items, sel = {}, 0
    for row, i in ipairs(visible()) do
        local w = widgets()[i]
        items[#items + 1] = { text = toAnsi(string.format("#%-3d %s", i, title(w))) }
        if i == S.sel then sel = row end
    end
    return items, sel
end

local function fieldsOf(w)
    w = w or { name = "", group = "", title = "", text = { en = "", pt = "" }, pos = {} }
    return {
        name = { text = toAnsi(w.name) },
        group = { text = toAnsi(w.group) },
        enabled = { selected = w.enabled ~= false },
        blockhide = { selected = w.blockhide == true },
        marginx = { value = w.marginx or 0 },
        marginy = { value = w.marginy or 0 },
        width = { value = w.width or 120 },
        action_id = { value = math.floor(tonumber(w.actionId) or 0) },
        wtitle = { text = toAnsi(escapeNl(w.title)) },
        text_en = { text = toAnsi(boxText(w.text.en)) },
        text_pt = { text = toAnsi(boxText(w.text.pt)) },
    }
end

-- ao escrever nos campos o wx dispara eventos de texto; com isso ligado o commit nao le campo
-- pela metade
local filling = false

local function refresh(d)
    filling = true
    -- renomear o grupo do ultimo painel dele faz o grupo do filtro sumir
    local exists = false
    for _, g in ipairs(groups()) do exists = exists or g == S.group end
    if not exists then S.group = ALL_GROUPS end
    local items, sel = listItems()
    local mods = fieldsOf(current())
    mods.group_filter = { options = groupOptions(), option = toAnsi(S.group) }
    mods.list = { items = items, selection = sel }
    mods.lbl_pos = { text = current() and ("Posicao: " .. posText(current())) or "Nenhum painel escolhido." }
    mods.lbl_status = { text = statusText() }
    d:modify(mods)
    filling = false
end

local function changed(d)
    S.dirty = true
    refresh(d)
    redraw()
end

-- grava os campos da janela no painel escolhido; true se algo mudou. Todo botao chama isto
-- antes de agir, entao trocar de painel ou salvar nunca perde o que foi digitado.
local function commit(d)
    local w = current()
    if not w or filling then return false end
    local f = d.data
    local aid = math.floor(tonumber(f.action_id) or 0)
    local new = {
        name = fromAnsi(f.name),
        group = fromAnsi(f.group),
        enabled = f.enabled and true or false,
        blockhide = f.blockhide and true or false,
        marginx = math.floor(tonumber(f.marginx) or 0),
        marginy = math.floor(tonumber(f.marginy) or 0),
        width = math.floor(tonumber(f.width) or 120),
        title = unescapeNl(fromAnsi(f.wtitle)),
        en = unescapeNl(fromAnsi(f.text_en)),
        pt = unescapeNl(fromAnsi(f.text_pt)),
    }
    local dirty = new.name ~= w.name or new.group ~= w.group or new.enabled ~= w.enabled
        or new.blockhide ~= w.blockhide or new.marginx ~= w.marginx or new.marginy ~= w.marginy
        or new.width ~= w.width or new.title ~= w.title or new.en ~= w.text.en or new.pt ~= w.text.pt
        or aid ~= math.floor(tonumber(w.actionId) or 0)
    if not dirty then return false end
    w.name, w.group, w.enabled, w.blockhide = new.name, new.group, new.enabled, new.blockhide
    w.marginx, w.marginy, w.width, w.title = new.marginx, new.marginy, new.width, new.title
    w.text.en, w.text.pt = new.en, new.pt
    w.actionId = aid > 0 and aid or nil
    if not w.actionId then w.anchorOffset = nil end
    S.dirty = true
    return true
end

local function goTo(w)
    local p = w and anchors(w)[1]
    if p then app.setCameraPosition(p.x, p.y, p.z) end
end

local function selectedTile()
    if (app.selection.size or 0) == 0 then return nil end
    local b = app.selection.bounds
    if b.min.x ~= b.max.x or b.min.y ~= b.max.y then return nil end
    return { x = b.min.x, y = b.min.y, z = b.min.z }
end

-- Os botoes agem no tile SELECIONADO, nao no ultimo do mouse: ate o mouse chegar na janela ele
-- passa por outros tiles (e o hover dos scripts nem existia no canvas antes do build de 2026-11-02).
local function needTile()
    local pos = selectedTile()
    if not pos then
        app.alert("Selecione UM tile no mapa: ferramenta de selecao, clique no tile.")
    end
    return pos
end

-- escolhe o painel do tile; de novo no mesmo tile passa para o proximo painel dele
local function pickAt(d, pos)
    local hits = hitAt(pos.x, pos.y, pos.z)
    if #hits == 0 then return false end
    commit(d)
    local nextIdx = hits[1]
    for k, i in ipairs(hits) do
        if i == S.sel then nextIdx = hits[k % #hits + 1] end
    end
    S.sel = nextIdx
    if S.group ~= ALL_GROUPS and current().group ~= S.group then S.group = ALL_GROUPS end
    refresh(d)
    redraw()
    return true
end

local function copyOf(w)
    local c = {}
    for k, v in pairs(w) do c[k] = v end
    c.text = { en = w.text.en, pt = w.text.pt }
    c.pos = {}
    for k, p in ipairs(w.pos) do c.pos[k] = { x = p.x, y = p.y, z = p.z } end
    if w.anchorOffset then c.anchorOffset = { x = w.anchorOffset.x, y = w.anchorOffset.y } end
    return c
end

-- entra logo depois do escolhido, para os grupos ficarem juntos no arquivo
local function insertWidget(d, w)
    local at = (S.sel or #widgets()) + 1
    table.insert(widgets(), at, w)
    S.sel = at
    if S.group ~= ALL_GROUPS and w.group ~= S.group then S.group = ALL_GROUPS end
    d:modify({ group_filter = { options = groupOptions(), option = toAnsi(S.group) } })
    changed(d)
end

local function setPosition(d, pos)
    commit(d)
    local w = current()
    if not w or not pos then return end
    if w.actionId and #w.pos == 0 then
        app.alert("Este painel cai no item do actionId " .. w.actionId .. ".\nZere o actionId para usar posicao fixa.")
        return
    end
    w.pos[1] = pos
    changed(d)
end

-- rodar de novo fecha a janela anterior (senao acumulam)
if S.dlg then pcall(function() S.dlg:close() end) end
if S.selectionListener then pcall(function() app.events:off(S.selectionListener) end) end
S.runChecks = runChecks

local dlg = Dialog({ title = "Paineis de informacao no mapa - MapInfos (BlackTalon)", width = 900, height = MULTILINE and 900 or 760, resizable = true,
    onclose = function()
        if S.selectionListener then pcall(function() app.events:off(S.selectionListener) end) end
        S.selectionListener = nil
    end })
S.dlg = dlg

dlg:label({ text = "O texto que aparece em cima de um tile no jogo. Amarelo = escolhido, azul = ligado, cinza = desligado." })
dlg:newrow()
dlg:label({ text = "Quadrado cheio = o tile do painel; tracejado = onde o painel fica na tela do jogo (aproximado)." })
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
    height = 230,
    expand = true,
    items = (listItems()),
    onchange = function(d)
        commit(d)
        local i = visible()[d.data.list]
        if i then S.sel = i end
        refresh(d)
        redraw()
        goTo(current())
    end,
})
dlg:newrow()

dlg:box({ orient = "vertical", label = "Painel escolhido (os campos valem ao clicar em qualquer botao)", padding = 4, margin = 4, expand = false })
dlg:wrap({ orient = "horizontal" })
dlg:input({ id = "name", label = "Nome", text = "" })
dlg:input({ id = "group", label = "Grupo", text = "" })
dlg:check({ id = "enabled", text = "Ligado", selected = true })
dlg:check({ id = "blockhide", text = "Sempre opaco (blockhide)", selected = true })
dlg:endwrap()
dlg:wrap({ orient = "horizontal" })
dlg:number({ id = "marginx", label = "marginx (px)", value = 0, min = -2000, max = 2000 })
dlg:number({ id = "marginy", label = "marginy (px)", value = 0, min = -2000, max = 2000 })
dlg:number({ id = "width", label = "Largura (px)", value = 120, min = 0, max = 2000 })
dlg:number({ id = "action_id", label = "actionId (0 = posicao fixa)", value = 0, min = 0, max = 65535 })
dlg:endwrap()
-- o texto das instrucoes de evento passa de 300 caracteres: caixa larga, e alta quando da
local TEXT_BOX_WIDTH = 760
local TEXT_BOX_HEIGHT = 110
dlg:wrap({ orient = "horizontal" })
dlg:input({ id = "wtitle", label = "Titulo     ", text = "", width = TEXT_BOX_WIDTH })
dlg:endwrap()
for _, f in ipairs({ { "text_en", "Texto EN" }, { "text_pt", "Texto PT" } }) do
    dlg:wrap({ orient = "horizontal" })
    dlg:input({ id = f[1], label = f[2], text = "", width = TEXT_BOX_WIDTH,
        multiline = MULTILINE, height = MULTILINE and TEXT_BOX_HEIGHT or nil })
    dlg:endwrap()
end
dlg:label({ text = (MULTILINE and "Quebra de linha = Enter (ou \\n escrito)." or "Quebra de linha = \\n escrito.")
    .. "  Cor: <color=\"white\">texto</color> (white, yellow, orange, green, red...)." })
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Posicao", padding = 4, margin = 4, expand = false })
dlg:label({ id = "lbl_pos", text = "" })
dlg:button({
    text = "Mover para o tile selecionado",
    onclick = function(d) setPosition(d, needTile()) end,
})
dlg:button({
    text = "+ Posicao extra no tile selecionado",
    onclick = function(d)
        commit(d)
        local w, pos = current(), needTile()
        if not w or not pos then return end
        w.pos[#w.pos + 1] = pos
        changed(d)
    end,
})
dlg:button({
    text = "Tirar as extras",
    onclick = function(d)
        commit(d)
        local w = current()
        if w and #w.pos > 1 then
            w.pos = { w.pos[1] }
            changed(d)
        end
    end,
})
dlg:button({ text = "Ir ate ele", onclick = function(d) commit(d); goTo(current()) end })
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Paineis", padding = 4, margin = 4, expand = false })
dlg:button({
    text = "Novo painel no tile selecionado",
    onclick = function(d)
        commit(d)
        local pos = needTile()
        if not pos then return end
        local base = current()
        insertWidget(d, {
            name = "novo",
            group = (S.group ~= ALL_GROUPS and S.group) or (base and base.group) or "",
            enabled = true,
            blockhide = true,
            marginx = -50,
            marginy = -65,
            width = 160,
            title = "",
            text = { en = "<color=\"white\">New</color>", pt = "<color=\"white\">Novo</color>" },
            pos = { pos },
        })
    end,
})
dlg:button({
    text = "Duplicar no tile selecionado",
    onclick = function(d)
        commit(d)
        local w, pos = current(), needTile()
        if not w or not pos then return end
        local c = copyOf(w)
        c.pos = { pos }
        c.actionId, c.anchorOffset = nil, nil
        insertWidget(d, c)
    end,
})
dlg:button({
    text = "Escolher o do tile selecionado",
    onclick = function(d)
        local pos = needTile()
        if pos and not pickAt(d, pos) then app.alert("Nenhum painel no tile selecionado.") end
    end,
})
dlg:button({
    text = "Apagar o escolhido",
    onclick = function(d)
        local w = current()
        if not w then return end
        -- o alert so mostra OK/Cancelar (o rotulo dos botoes e ignorado); OK = 1
        local r = app.alert({ title = "Apagar painel", text = "Apagar o painel \"" .. toAnsi(w.name) .. "\"?", buttons = { "OK", "Cancel" } })
        if r ~= 1 then return end
        table.remove(widgets(), S.sel)
        local vis = visible()
        if #vis == 0 then S.group = ALL_GROUPS end
        S.sel = math.min(S.sel, #widgets())
        if S.sel < 1 then S.sel = nil end
        d:modify({ group_filter = { options = groupOptions(), option = toAnsi(S.group) } })
        changed(d)
    end,
})
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", padding = 4, margin = 4, expand = false })
dlg:button({
    text = "Procurar actionIds no mapa",
    onclick = function(d)
        commit(d)
        local total = scanActionIds()
        refresh(d)
        redraw()
        app.alert(string.format("%d item(ns) com o actionId de algum painel.", total))
    end,
})
dlg:button({
    text = "Conferir",
    onclick = function(d)
        if commit(d) then refresh(d) end
        local text, problems = runChecks()
        print("[map_infos] conferencia:\n" .. text)
        app.alert(toAnsi((problems > 0 and (problems .. " problema(s):\n\n") or "") .. text))
    end,
})
dlg:button({
    text = "Recarregar do arquivo",
    onclick = function(d)
        if loadData() then
            if not current() then S.sel = widgets()[1] and 1 or nil end
            S.group = ALL_GROUPS
            d:modify({ group_filter = { options = groupOptions(), option = toAnsi(S.group) } })
            refresh(d)
            redraw()
        else
            app.alert("map_infos.json invalido ou ausente: " .. tostring(store.path))
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
            app.alert("Salvo.\nNo server: /mapinfos reload (GM) para valer sem reiniciar.")
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

-- clicar (selecionar) um tile que tem painel ja o escolhe na janela
S.selectionListener = app.events:on("selectionChange", function()
    local pos = selectedTile()
    if pos and S.dlg == dlg then pickAt(dlg, pos) end
end)
