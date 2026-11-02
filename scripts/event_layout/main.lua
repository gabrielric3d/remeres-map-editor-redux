-- Layout dos eventos do BlackTalon: desenha no mapa as arenas/tabuleiros, a moldura,
-- a grade de spawn e os pontos (teleporte, sala de espera) de cada evento, e deixa
-- mover tudo pela selecao ou pelo mouse e salvar.
--
-- A fonte e o server: blacktalon-workspace\server\data\scripts\eventos\event_layouts.json.
-- O layouts.json desta pasta e um HARD LINK para ele (o RME so le/grava dentro da
-- propria pasta de scripts), criado por .blacktalon/scripts/link_event_layouts.cjs. Se
-- um git checkout/pull trocar o arquivo do server, o link quebra: rode o .cjs de novo.
--
-- Texto da tela so em ASCII (a camada Lua do editor quebra acento UTF-8).

if not app or not app.hasMap() or not app.map then
    print("[event_layout] no map open")
    if app then app.alert("Nenhum mapa aberto.") end
    return
end

local OVERLAY_ID = "bt_event_layout"
local store = app.storage("layouts.json")

-- Rodar o script de novo executa o arquivo no mesmo estado Lua: o estado fica aqui.
BTEventLayout = BTEventLayout or {}
local S = BTEventLayout

-- ---------------------------------------------------------------------------
-- Dados
-- ---------------------------------------------------------------------------

local function loadData()
    local data = store:load()
    if type(data) ~= "table" or type(data.events) ~= "table" then
        return false
    end
    S.data = data
    S.dirty = false
    return true
end

if not loadData() then
    app.alert("Nao achei o layouts.json valido em\n" .. tostring(store.path) ..
        "\n\nRode: node .blacktalon/scripts/link_event_layouts.cjs (no blacktalon-workspace).")
    return
end

local function eventIds()
    local ids = {}
    for id in pairs(S.data.events) do ids[#ids + 1] = id end
    table.sort(ids)
    return ids
end

local function eventTitle(id)
    local ev = S.data.events[id]
    return (ev and ev.title) or id
end

local function sizeOf(m)
    return m.to.x - m.from.x + 1, m.to.y - m.from.y + 1
end

-- Entradas editaveis de um evento: os mapas (na ordem do JSON) e os pontos (por nome)
local function entriesFor(evId)
    local ev = S.data.events[evId]
    local list = {}
    if not ev then return list end
    for i, m in ipairs(ev.maps or {}) do
        list[#list + 1] = { kind = "map", key = "map:" .. i, index = i, ref = m }
    end
    local names = {}
    for name in pairs(ev.points or {}) do names[#names + 1] = name end
    table.sort(names)
    for _, name in ipairs(names) do
        list[#list + 1] = { kind = "point", key = "point:" .. name, name = name, ref = ev.points[name] }
    end
    return list
end

local function entryText(e)
    if e.kind == "map" then
        local m = e.ref
        local w, h = sizeOf(m)
        return string.format("Mapa %-7s %dx%d  ate %d jogadores   (%d,%d,%d) a (%d,%d)",
            m.name or "?", w, h, m.maxPlayers or 0, m.from.x, m.from.y, m.from.z, m.to.x, m.to.y)
    end
    local p = e.ref
    return string.format("Ponto %-16s (%d,%d,%d)", e.name, p.x, p.y, p.z)
end

local function currentEntry()
    for _, e in ipairs(entriesFor(S.selEvent)) do
        if e.key == S.selKey then return e end
    end
    return nil
end

-- ---------------------------------------------------------------------------
-- Overlay
-- ---------------------------------------------------------------------------

local function rgba(ev, a)
    local c = (ev and ev.color) or { 255, 255, 255 }
    return { c[1] or 255, c[2] or 255, c[3] or 255, a }
end

local function inView(v, x1, y1, x2, y2)
    return x2 >= v.x1 - 2 and x1 <= v.x2 + 2 and y2 >= v.y1 - 2 and y1 <= v.y2 + 2
end

local function drawMap(ctx, v, evId, ev, i, m)
    local x1, y1, x2, y2 = m.from.x, m.from.y, m.to.x, m.to.y
    local w, h = sizeOf(m)
    local sel = (S.selEvent == evId and S.selKey == "map:" .. i)
    ctx.rect({ x = x1, y = y1, z = v.z, w = w, h = h, filled = true, color = rgba(ev, sel and 70 or 30) })
    ctx.rect({ x = x1, y = y1, z = v.z, w = w, h = h, filled = false, width = sel and 3 or 2, color = rgba(ev, 235) })
    local f = tonumber(ev.frame) or 0
    if f > 0 then
        ctx.rect({ x = x1 - f, y = y1 - f, z = v.z, w = w + 2 * f, h = h + 2 * f, filled = false, width = 1,
            style = "dashed", color = rgba(ev, 170) })
    end
    local grid = ev.spawnGrid
    if type(grid) == "table" and (v.zoom or 1) <= 2 then
        local step = math.max(1, math.floor(tonumber(grid.step) or 2))
        local margin = math.max(0, math.floor(tonumber(grid.margin) or 1))
        for r = margin, h - 1 - margin, step do
            for c = margin, w - 1 - margin, step do
                ctx.rect({ x = x1 + c, y = y1 + r, z = v.z, filled = true, color = { 255, 255, 255, 60 } })
            end
        end
    end
    ctx.text({ x = x1 - f, y = y1 - f - 1, z = v.z, color = rgba(ev, 255),
        text = string.format("%s / %s  %dx%d  ate %d", eventTitle(evId), m.name or "?", w, h, m.maxPlayers or 0) })
end

local function drawPoint(ctx, v, evId, ev, name, p)
    local sel = (S.selEvent == evId and S.selKey == "point:" .. name)
    ctx.rect({ x = p.x, y = p.y, z = v.z, filled = true, color = rgba(ev, sel and 160 or 110) })
    ctx.rect({ x = p.x, y = p.y, z = v.z, filled = false, width = sel and 3 or 2, color = { 255, 255, 255, 235 } })
    ctx.text({ x = p.x, y = p.y - 1, z = v.z, text = eventTitle(evId) .. ": " .. name, color = rgba(ev, 255) })
end

local function hitAt(x, y, z)
    for _, evId in ipairs(eventIds()) do
        local ev = S.data.events[evId]
        for name, p in pairs(ev.points or {}) do
            if p.x == x and p.y == y and p.z == z then
                return evId, "point:" .. name, string.format("%s: ponto %s (%d,%d,%d)", eventTitle(evId), name, x, y, z)
            end
        end
        for i, m in ipairs(ev.maps or {}) do
            if m.from.z == z and x >= m.from.x and x <= m.to.x and y >= m.from.y and y <= m.to.y then
                local w, h = sizeOf(m)
                return evId, "map:" .. i, string.format("%s: mapa %s %dx%d, ate %d jogadores\ncasa (%d,%d) = coluna %d, linha %d",
                    eventTitle(evId), m.name or "?", w, h, m.maxPlayers or 0, x, y, x - m.from.x, y - m.from.y)
            end
        end
    end
    return nil
end

pcall(function() app.mapView:removeOverlay(OVERLAY_ID) end)
app.mapView:addOverlay(OVERLAY_ID, {
    ondraw = function(ctx)
        local v = ctx.view
        for _, evId in ipairs(eventIds()) do
            local ev = S.data.events[evId]
            for i, m in ipairs(ev.maps or {}) do
                if m.from and m.to and m.from.z == v.z and inView(v, m.from.x - 1, m.from.y - 2, m.to.x + 1, m.to.y + 1) then
                    drawMap(ctx, v, evId, ev, i, m)
                end
            end
            for name, p in pairs(ev.points or {}) do
                if p.z == v.z and inView(v, p.x, p.y - 1, p.x, p.y) then
                    drawPoint(ctx, v, evId, ev, name, p)
                end
            end
        end
    end,
    onhover = function(info)
        if not info.pos then return nil end
        S.hover = { x = info.pos.x, y = info.pos.y, z = info.pos.z }
        local evId, _, text = hitAt(info.pos.x, info.pos.y, info.pos.z)
        if not evId then return nil end
        return {
            tooltip = { text = text, color = { 255, 255, 255 } },
            highlight = { color = rgba(S.data.events[evId], 120), filled = false, width = 2 },
        }
    end,
})
pcall(function()
    app.mapView:registerShow("Layout dos eventos (BlackTalon)", OVERLAY_ID, { enabled = true })
end)

-- o desenho e cacheado pela vista: setEnabled soma a revisao e refaz
local function redraw()
    app.mapView:setEnabled(OVERLAY_ID, true)
    app.refresh()
end

-- ---------------------------------------------------------------------------
-- Conferencia
-- ---------------------------------------------------------------------------

local function painted(m)
    local w, h = sizeOf(m)
    local probes = { { 0, 0 }, { w - 1, 0 }, { 0, h - 1 }, { w - 1, h - 1 }, { math.floor(w / 2), math.floor(h / 2) } }
    for _, p in ipairs(probes) do
        local t = app.map:getTile(m.from.x + p[1], m.from.y + p[2], m.from.z)
        if not t or not t.ground then return false end
    end
    return true
end

-- distancia entre dois retangulos (com a moldura de cada evento), em sqm
local function gap(a, fa, b, fb)
    local ax1, ay1, ax2, ay2 = a.from.x - fa, a.from.y - fa, a.to.x + fa, a.to.y + fa
    local bx1, by1, bx2, by2 = b.from.x - fb, b.from.y - fb, b.to.x + fb, b.to.y + fb
    local dx = math.max(0, bx1 - ax2 - 1, ax1 - bx2 - 1)
    local dy = math.max(0, by1 - ay2 - 1, ay1 - by2 - 1)
    return math.max(dx, dy)
end

-- O client ve 8 sqm para cada lado (17 de largura): abaixo de 9 de folga, quem esta
-- na borda de uma arena ve a outra.
local MIN_GAP = 9

local function runChecks()
    local lines, problems = {}, 0
    local all = {}
    for _, evId in ipairs(eventIds()) do
        local ev = S.data.events[evId]
        for _, m in ipairs(ev.maps or {}) do
            all[#all + 1] = { evId = evId, ev = ev, m = m }
            if not painted(m) then
                problems = problems + 1
                lines[#lines + 1] = string.format("SEM CHAO: %s / %s (cantos ou centro sem piso)", eventTitle(evId), m.name or "?")
            end
        end
        for name, p in pairs(ev.points or {}) do
            local t = app.map:getTile(p.x, p.y, p.z)
            if not t or not t.ground then
                problems = problems + 1
                lines[#lines + 1] = string.format("PONTO SEM CHAO: %s / %s (%d,%d,%d)", eventTitle(evId), name, p.x, p.y, p.z)
            elseif t.isBlocking then
                problems = problems + 1
                lines[#lines + 1] = string.format("PONTO BLOQUEADO: %s / %s (%d,%d,%d)", eventTitle(evId), name, p.x, p.y, p.z)
            end
        end
    end
    for i = 1, #all do
        for j = i + 1, #all do
            local a, b = all[i], all[j]
            if a.m.from.z == b.m.from.z then
                local g = gap(a.m, tonumber(a.ev.frame) or 0, b.m, tonumber(b.ev.frame) or 0)
                if g < MIN_GAP then
                    problems = problems + 1
                    lines[#lines + 1] = string.format("PERTO: %s/%s e %s/%s com %d sqm de folga (minimo %d)",
                        eventTitle(a.evId), a.m.name or "?", eventTitle(b.evId), b.m.name or "?", g, MIN_GAP)
                end
            end
        end
    end
    if problems == 0 then
        lines[#lines + 1] = "Tudo certo: mapas pintados, pontos livres e arenas afastadas."
    end
    return table.concat(lines, "\n"), problems
end

-- ---------------------------------------------------------------------------
-- A janela
-- ---------------------------------------------------------------------------

S.selEvent = (S.selEvent and S.data.events[S.selEvent]) and S.selEvent or eventIds()[1]
S.selKey = S.selKey or "map:1"

local function statusText()
    return (S.dirty and "ALTERACOES NAO SALVAS. " or "Salvo. ") .. "Arquivo: " .. tostring(store.path)
end

local function listItems()
    local items, sel = {}, 1
    for i, e in ipairs(entriesFor(S.selEvent)) do
        items[#items + 1] = { text = entryText(e) }
        if e.key == S.selKey then sel = i end
    end
    return items, sel
end

local function refresh(d)
    local items, sel = listItems()
    local e = currentEntry()
    d:modify({
        list = { items = items, selection = sel },
        lbl_status = { text = statusText() },
        max_players = { value = (e and e.kind == "map") and (e.ref.maxPlayers or 0) or 0 },
    })
end

local function goTo(e)
    if not e then return end
    if e.kind == "map" then
        local m = e.ref
        app.setCameraPosition(math.floor((m.from.x + m.to.x) / 2), math.floor((m.from.y + m.to.y) / 2), m.from.z)
    else
        app.setCameraPosition(e.ref.x, e.ref.y, e.ref.z)
    end
end

local function changed(d)
    S.dirty = true
    refresh(d)
    redraw()
end

local ids = eventIds()
local titles = {}
for _, id in ipairs(ids) do titles[#titles + 1] = eventTitle(id) end

-- rodar de novo fecha a janela anterior (senao acumulam)
if S.dlg then pcall(function() S.dlg:close() end) end
S.runChecks = runChecks

local dlg = Dialog({ title = "Layout dos eventos (BlackTalon)", width = 760, height = 560, resizable = true })
S.dlg = dlg

dlg:label({ text = "Cada evento: os mapas por lotacao (o server escolhe o menor que cabe todo mundo) e os pontos fixos." })
dlg:newrow()
dlg:combobox({
    id = "event",
    label = "Evento",
    options = titles,
    option = eventTitle(S.selEvent),
    onchange = function(d)
        local title = d.data.event
        for _, id in ipairs(ids) do
            if eventTitle(id) == title then S.selEvent = id end
        end
        S.selKey = "map:1"
        refresh(d)
        redraw()
        goTo(currentEntry())
    end,
})
dlg:newrow()
dlg:list({
    id = "list",
    height = 200,
    expand = true,
    items = (listItems()),
    onchange = function(d)
        local idx = d.data.list
        local e = entriesFor(S.selEvent)[idx]
        if e then
            S.selKey = e.key
            refresh(d)
            redraw()
            goTo(e)
        end
    end,
})
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Mover o item escolhido", padding = 4, margin = 4, expand = false })
dlg:button({
    text = "Usar a selecao",
    onclick = function(d)
        local e = currentEntry()
        if not e then return end
        if (app.selection.size or 0) == 0 then
            app.alert("Selecione no mapa: a area do mapa (do canto de cima-esquerda ao de baixo-direita) ou o tile do ponto.")
            return
        end
        local b = app.selection.bounds
        if e.kind == "map" then
            e.ref.from = { x = b.min.x, y = b.min.y, z = b.min.z }
            e.ref.to = { x = b.max.x, y = b.max.y, z = b.min.z }
        else
            e.ref.x, e.ref.y, e.ref.z = b.min.x, b.min.y, b.min.z
        end
        changed(d)
    end,
})
dlg:button({
    text = "Usar o ultimo tile do mouse",
    onclick = function(d)
        local e = currentEntry()
        if not e or not S.hover then
            app.alert("Passe o mouse sobre o tile no mapa antes de clicar.")
            return
        end
        local h = S.hover
        if e.kind == "map" then
            -- move o mapa inteiro com o canto de cima-esquerda no mouse (mesmo tamanho)
            local w, hh = sizeOf(e.ref)
            e.ref.from = { x = h.x, y = h.y, z = h.z }
            e.ref.to = { x = h.x + w - 1, y = h.y + hh - 1, z = h.z }
        else
            e.ref.x, e.ref.y, e.ref.z = h.x, h.y, h.z
        end
        changed(d)
    end,
})
dlg:button({ text = "Ir ate ele", onclick = function() goTo(currentEntry()) end })
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Lotacao do mapa escolhido", padding = 4, margin = 4, expand = false })
dlg:number({ id = "max_players", label = "Ate quantos jogadores", value = 0, min = 0, max = 500 })
dlg:button({
    text = "Aplicar",
    onclick = function(d)
        local e = currentEntry()
        if e and e.kind == "map" then
            e.ref.maxPlayers = math.floor(tonumber(d.data.max_players) or 0)
            changed(d)
        end
    end,
})
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", padding = 4, margin = 4, expand = false })
dlg:button({
    text = "Conferir",
    onclick = function()
        local text, problems = runChecks()
        print("[event_layout] conferencia:\n" .. text)
        app.alert((problems > 0 and (problems .. " problema(s):\n\n") or "") .. text)
    end,
})
dlg:button({
    text = "Recarregar do arquivo",
    onclick = function(d)
        if loadData() then
            refresh(d)
            redraw()
        else
            app.alert("layouts.json invalido ou ausente: " .. tostring(store.path))
        end
    end,
})
dlg:button({
    text = "Salvar",
    onclick = function(d)
        if store:save(S.data) then
            S.dirty = false
            refresh(d)
            app.alert("Salvo.\nNo server: /reload scripts (ou reiniciar) para valer.")
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
