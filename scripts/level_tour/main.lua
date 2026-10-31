-- Rastros de level do BlackTalon: desenha no mapa a rota de cada rastro (os pontos
-- e as retas entre eles) e deixa adicionar, mover, reordenar e apagar pontos pelo
-- mouse ou pela selecao, e salvar. Depois do video do lugar novo, o jogador recebe
-- essa rota como rastro no minimapa, que some quando ele chega.
--
-- A fonte e o server: blacktalon-workspace\server\data\scripts\levelvideo\level_tours.json.
-- O tours.json desta pasta e um HARD LINK para ele (o RME so le/grava dentro da
-- propria pasta de scripts), criado por .blacktalon/scripts/link_level_tours.cjs. Se
-- um git checkout/pull trocar o arquivo do server, o link quebra: rode o .cjs de novo.
--
-- As retas sao so a ordem dos pontos: no jogo o caminho entre dois pontos vem do A*
-- (contorna parede). Ponto em outro andar = o caminho pega a escada mais perto dele
-- (ate 3 sqm). O rastro so existe no jogo se o id tambem estiver no LevelVideoConfig
-- do levelvideo.lua (nivel, storage, video).
--
-- Texto da tela so em ASCII (a camada Lua do editor quebra acento UTF-8).

if not app or not app.hasMap() or not app.map then
    print("[level_tour] no map open")
    if app then app.alert("Nenhum mapa aberto.") end
    return
end

local OVERLAY_ID = "bt_level_tour"
local store = app.storage("tours.json")

local MAX_GAP = 30          -- acima disso o A* do server pode nao achar o caminho
local COLOR_SEL = { 200, 168, 76 }
local COLOR_OTHER = { 120, 170, 220 }

-- Rodar o script de novo executa o arquivo no mesmo estado Lua: o estado fica aqui.
BTLevelTour = BTLevelTour or {}
local S = BTLevelTour

-- ---------------------------------------------------------------------------
-- Dados
-- ---------------------------------------------------------------------------

local function loadData()
    local data = store:load()
    if type(data) ~= "table" or type(data.tours) ~= "table" then
        return false
    end
    for _, tour in pairs(data.tours) do
        if type(tour.points) ~= "table" then tour.points = {} end
    end
    S.data = data
    S.dirty = false
    return true
end

if not loadData() then
    app.alert("Nao achei o tours.json valido em\n" .. tostring(store.path) ..
        "\n\nRode: node .blacktalon/scripts/link_level_tours.cjs (no blacktalon-workspace).")
    return
end

local function tourIds()
    local ids = {}
    for id in pairs(S.data.tours) do ids[#ids + 1] = id end
    table.sort(ids)
    return ids
end

local function tourTitle(id)
    local tour = S.data.tours[id]
    return (tour and tour.title) and (tour.title .. " (" .. id .. ")") or id
end

local function points()
    local tour = S.data.tours[S.selTour]
    return tour and tour.points or {}
end

local function gap(a, b)
    return math.max(math.abs(a.x - b.x), math.abs(a.y - b.y))
end

local function pointText(i, p, prev)
    local s = string.format("%2d  (%d, %d, %d)", i, p.x, p.y, p.z)
    if prev then
        if prev.z ~= p.z then
            s = s .. string.format("   andar %d -> %d", prev.z, p.z)
        else
            local g = gap(prev, p)
            s = s .. string.format("   %d sqm", g)
            if g > MAX_GAP then s = s .. "  LONGE" end
        end
    else
        s = s .. "   inicio"
    end
    return s
end

-- ---------------------------------------------------------------------------
-- Overlay
-- ---------------------------------------------------------------------------

local function rgba(c, a)
    return { c[1], c[2], c[3], a }
end

local function drawTour(ctx, v, id, tour)
    local sel = (id == S.selTour)
    local color = sel and COLOR_SEL or COLOR_OTHER
    local pts = tour.points
    for i = 2, #pts do
        local a, b = pts[i - 1], pts[i]
        if a.z == v.z and b.z == v.z then
            ctx.line({ x1 = a.x, y1 = a.y, z1 = v.z, x2 = b.x, y2 = b.y, z2 = v.z, color = rgba(color, sel and 230 or 130) })
        end
    end
    for i, p in ipairs(pts) do
        if p.z == v.z then
            local current = sel and i == S.selIndex
            ctx.rect({ x = p.x, y = p.y, z = v.z, filled = true, color = rgba(color, current and 170 or 90) })
            ctx.rect({ x = p.x, y = p.y, z = v.z, filled = false, width = current and 3 or 2, color = { 255, 255, 255, sel and 235 or 140 } })
            local label = tostring(i)
            if i == 1 then label = tourTitle(id) .. ": 1 inicio" end
            if i == #pts and #pts > 1 then label = label .. " fim" end
            local nxt = pts[i + 1]
            if nxt and nxt.z ~= p.z then
                label = label .. (nxt.z > p.z and " (desce)" or " (sobe)")
            end
            ctx.text({ x = p.x, y = p.y - 1, z = v.z, text = label, color = rgba(color, 255) })
        end
    end
end

local function hitAt(x, y, z)
    for _, id in ipairs(tourIds()) do
        for i, p in ipairs(S.data.tours[id].points) do
            if p.x == x and p.y == y and p.z == z then
                return id, i
            end
        end
    end
    return nil
end

pcall(function() app.mapView:removeOverlay(OVERLAY_ID) end)
app.mapView:addOverlay(OVERLAY_ID, {
    ondraw = function(ctx)
        local v = ctx.view
        for _, id in ipairs(tourIds()) do
            if id ~= S.selTour then drawTour(ctx, v, id, S.data.tours[id]) end
        end
        local tour = S.data.tours[S.selTour]
        if tour then drawTour(ctx, v, S.selTour, tour) end
    end,
    onhover = function(info)
        if not info.pos then return nil end
        S.hover = { x = info.pos.x, y = info.pos.y, z = info.pos.z }
        local id, i = hitAt(info.pos.x, info.pos.y, info.pos.z)
        if not id then return nil end
        local p = S.data.tours[id].points[i]
        return {
            tooltip = { text = string.format("%s: ponto %d (%d,%d,%d)", tourTitle(id), i, p.x, p.y, p.z), color = { 255, 255, 255 } },
            highlight = { color = rgba(id == S.selTour and COLOR_SEL or COLOR_OTHER, 140), filled = false, width = 2 },
        }
    end,
})
pcall(function()
    app.mapView:registerShow("Rastros de level (BlackTalon)", OVERLAY_ID, { enabled = true })
end)

-- o desenho e cacheado pela vista: setEnabled soma a revisao e refaz
local function redraw()
    app.mapView:setEnabled(OVERLAY_ID, true)
    app.refresh()
end

-- ---------------------------------------------------------------------------
-- Conferencia
-- ---------------------------------------------------------------------------

local function runChecks()
    local lines, problems = {}, 0
    for _, id in ipairs(tourIds()) do
        local pts = S.data.tours[id].points
        if #pts == 1 then
            problems = problems + 1
            lines[#lines + 1] = string.format("%s: so 1 ponto (precisa de 2 para virar tour)", tourTitle(id))
        end
        for i, p in ipairs(pts) do
            local t = app.map:getTile(p.x, p.y, p.z)
            if not t or not t.ground then
                problems = problems + 1
                lines[#lines + 1] = string.format("%s: ponto %d SEM CHAO (%d,%d,%d)", tourTitle(id), i, p.x, p.y, p.z)
            elseif t.isBlocking then
                problems = problems + 1
                lines[#lines + 1] = string.format("%s: ponto %d BLOQUEADO (%d,%d,%d)", tourTitle(id), i, p.x, p.y, p.z)
            end
            local prev = pts[i - 1]
            if prev and prev.z == p.z and gap(prev, p) > MAX_GAP then
                -- longe so atrapalha se a reta nao for livre (o A* do server para cedo)
                local blocked = 0
                local n = gap(prev, p)
                for k = 0, n do
                    local x = prev.x + math.floor((p.x - prev.x) * k / n + 0.5)
                    local y = prev.y + math.floor((p.y - prev.y) * k / n + 0.5)
                    local tt = app.map:getTile(x, y, p.z)
                    if not tt or not tt.ground or tt.isBlocking then blocked = blocked + 1 end
                end
                if blocked > 0 then
                    problems = problems + 1
                    lines[#lines + 1] = string.format("%s: ponto %d a %d sqm do anterior e a reta passa por %d tile(s) bloqueado(s): ponha um ponto no meio",
                        tourTitle(id), i, n, blocked)
                end
            end
        end
    end
    if problems == 0 then
        lines[#lines + 1] = "Tudo certo: pontos com chao, livres, e trechos longos so em reta livre."
    end
    return table.concat(lines, "\n"), problems
end

-- ---------------------------------------------------------------------------
-- A janela
-- ---------------------------------------------------------------------------

local ids = tourIds()
S.selTour = (S.selTour and S.data.tours[S.selTour]) and S.selTour or ids[1]
S.selIndex = S.selIndex or 1

local function statusText()
    return (S.dirty and "ALTERACOES NAO SALVAS. " or "Salvo. ") .. "Arquivo: " .. tostring(store.path)
end

local function listItems()
    local items = {}
    local pts = points()
    for i, p in ipairs(pts) do
        items[#items + 1] = { text = pointText(i, p, pts[i - 1]) }
    end
    if #items == 0 then
        items[1] = { text = "(sem pontos: passe o mouse no tile e clique em Adicionar no fim)" }
    end
    if S.selIndex > #pts then S.selIndex = math.max(1, #pts) end
    return items, S.selIndex
end

local function refresh(d)
    local items, sel = listItems()
    local tour = S.data.tours[S.selTour] or {}
    local p = points()[S.selIndex]
    d:modify({
        list = { items = items, selection = sel },
        lbl_status = { text = statusText() },
        arrive_radius = { value = tonumber(tour.arriveRadius) or 4 },
    })
end

local function goTo(p)
    if p then app.setCameraPosition(p.x, p.y, p.z) end
end

local function changed(d)
    S.dirty = true
    refresh(d)
    redraw()
end

-- posicao vinda do mouse ou da selecao, ou nil com o aviso
local function mousePos()
    if not S.hover then
        app.alert("Passe o mouse sobre o tile no mapa antes de clicar.")
        return nil
    end
    return { x = S.hover.x, y = S.hover.y, z = S.hover.z }
end

local function selectionPos()
    if (app.selection.size or 0) == 0 then
        app.alert("Selecione o tile do ponto no mapa.")
        return nil
    end
    local b = app.selection.bounds
    return { x = b.min.x, y = b.min.y, z = b.min.z }
end

local function insertAt(index, pos, d)
    if not pos then return end
    local pts = points()
    table.insert(pts, index, pos)
    S.selIndex = index
    changed(d)
end

local titles = {}
for _, id in ipairs(ids) do titles[#titles + 1] = tourTitle(id) end

-- rodar de novo fecha a janela anterior (senao acumulam)
if S.dlg then pcall(function() S.dlg:close() end) end
S.runChecks = runChecks

local dlg = Dialog({ title = "Rastros de level (BlackTalon)", width = 760, height = 600, resizable = true })
S.dlg = dlg

dlg:label({ text = "O rastro vai do ponto 1 ao ultimo; entre dois pontos o caminho vem do A* (contorna parede). Ponto em outro andar = escada." })
dlg:newrow()
dlg:combobox({
    id = "tour",
    label = "Tour",
    options = titles,
    option = tourTitle(S.selTour),
    onchange = function(d)
        local title = d.data.tour
        for _, id in ipairs(ids) do
            if tourTitle(id) == title then S.selTour = id end
        end
        S.selIndex = 1
        refresh(d)
        redraw()
        goTo(points()[1])
    end,
})
dlg:newrow()
dlg:list({
    id = "list",
    height = 220,
    expand = true,
    items = (listItems()),
    onchange = function(d)
        local idx = d.data.list
        local p = points()[idx]
        if p then
            S.selIndex = idx
            refresh(d)
            redraw()
            goTo(p)
        end
    end,
})
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Adicionar ponto", padding = 4, margin = 4, expand = false })
dlg:button({ text = "No fim (mouse)", onclick = function(d) insertAt(#points() + 1, mousePos(), d) end })
dlg:button({ text = "No fim (selecao)", onclick = function(d) insertAt(#points() + 1, selectionPos(), d) end })
dlg:button({
    text = "Depois do escolhido (mouse)",
    onclick = function(d) insertAt(math.min(S.selIndex + 1, #points() + 1), mousePos(), d) end,
})
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Ponto escolhido", padding = 4, margin = 4, expand = false })
dlg:button({
    text = "Mover para o mouse",
    onclick = function(d)
        local p, pos = points()[S.selIndex], mousePos()
        if p and pos then p.x, p.y, p.z = pos.x, pos.y, pos.z; changed(d) end
    end,
})
dlg:button({
    text = "Mover para a selecao",
    onclick = function(d)
        local p, pos = points()[S.selIndex], selectionPos()
        if p and pos then p.x, p.y, p.z = pos.x, pos.y, pos.z; changed(d) end
    end,
})
dlg:button({
    text = "Subir",
    onclick = function(d)
        local pts, i = points(), S.selIndex
        if i > 1 and pts[i] then pts[i - 1], pts[i] = pts[i], pts[i - 1]; S.selIndex = i - 1; changed(d) end
    end,
})
dlg:button({
    text = "Descer",
    onclick = function(d)
        local pts, i = points(), S.selIndex
        if pts[i + 1] then pts[i + 1], pts[i] = pts[i], pts[i + 1]; S.selIndex = i + 1; changed(d) end
    end,
})
dlg:button({
    text = "Remover",
    onclick = function(d)
        local pts = points()
        if pts[S.selIndex] then table.remove(pts, S.selIndex); changed(d) end
    end,
})
dlg:button({ text = "Ir ate ele", onclick = function() goTo(points()[S.selIndex]) end })
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Rastro", padding = 4, margin = 4, expand = false })
dlg:number({ id = "arrive_radius", label = "Chegou a ate (sqm)", value = 4, min = 0, max = 30 })
dlg:button({
    text = "Aplicar",
    onclick = function(d)
        local tour = S.data.tours[S.selTour]
        if not tour then return end
        tour.arriveRadius = math.max(0, math.floor(tonumber(d.data.arrive_radius) or 4))
        changed(d)
    end,
})
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", padding = 4, margin = 4, expand = false })
dlg:button({
    text = "Conferir",
    onclick = function()
        local text, problems = runChecks()
        print("[level_tour] conferencia:\n" .. text)
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
            app.alert("tours.json invalido ou ausente: " .. tostring(store.path))
        end
    end,
})
dlg:button({
    text = "Salvar",
    onclick = function(d)
        if store:save(S.data) then
            S.dirty = false
            refresh(d)
            app.alert("Salvo.\nNo server: /tour reload (GM) ou /reload scripts para valer.")
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
