-- Nascimento dos animais selvagens da fazenda (BlackTalon): desenha no mapa os pontos de cada
-- criatura do FarmTameable, deixa por/mover/apagar pontos, definir o maximo vivo e o tempo de
-- respawn de cada uma, conferir e salvar.
--
-- A fonte e o server: blacktalon-workspace\server\data\scripts\housemanager\farm_spawns.json,
-- lido por farm_wild_spawns.lua. O spawns.json desta pasta e um HARD LINK para ele (o RME so
-- le/grava dentro da propria pasta de scripts), criado por .blacktalon/scripts/link_farm_spawns.cjs,
-- que tambem traz a lista de criaturas do server (serverSpecies). Se um git checkout/pull trocar
-- o arquivo do server, o link quebra: rode o .cjs de novo.
--
-- As regras do server: no startup cada criatura nasce ate o maximo; capturou uma, a vaga volta
-- depois de respawnMin..respawnMax minutos num ponto livre SORTEADO da lista, e so fora da tela
-- de qualquer jogador.
--
-- Texto da tela so em ASCII (a camada Lua do editor quebra acento UTF-8).

if not app or not app.hasMap() or not app.map then
    print("[farm_spawns] no map open")
    if app then app.alert("Nenhum mapa aberto.") end
    return
end

local OVERLAY_ID = "bt_farm_spawns"
local store = app.storage("spawns.json")

-- Rodar o script de novo executa o arquivo no mesmo estado Lua: o estado fica aqui.
BTFarmSpawns = BTFarmSpawns or {}
local S = BTFarmSpawns

-- ---------------------------------------------------------------------------
-- Dados
-- ---------------------------------------------------------------------------

local function loadData()
    local data = store:load()
    if type(data) ~= "table" or type(data.species) ~= "table" then
        return false
    end
    for _, cfg in pairs(data.species) do
        if type(cfg.points) ~= "table" then cfg.points = {} end
    end
    S.data = data
    S.dirty = false
    return true
end

if not loadData() then
    app.alert("Nao achei o spawns.json valido em\n" .. tostring(store.path) ..
        "\n\nRode: node .blacktalon/scripts/link_farm_spawns.cjs (no blacktalon-workspace).")
    return
end

local function speciesIds()
    local ids = {}
    for id in pairs(S.data.species) do ids[#ids + 1] = id end
    table.sort(ids)
    return ids
end

local function current()
    return S.data.species[S.selSpecies]
end

local function onServer(id)
    for _, name in ipairs(S.data.serverSpecies or {}) do
        if name == id then return true end
    end
    return false
end

local function speciesTitle(id)
    local cfg = S.data.species[id]
    return string.format("%s  (%d pontos, ate %d vivos)", id, #(cfg.points or {}), math.floor(tonumber(cfg.max) or 0))
end

-- ---------------------------------------------------------------------------
-- Overlay
-- ---------------------------------------------------------------------------

local function rgba(cfg, a)
    local c = (cfg and cfg.color) or { 255, 255, 255 }
    return { c[1] or 255, c[2] or 255, c[3] or 255, a }
end

local function inView(v, x, y)
    return x >= v.x1 - 2 and x <= v.x2 + 2 and y >= v.y1 - 2 and y <= v.y2 + 2
end

local function hitAt(x, y, z)
    for _, id in ipairs(speciesIds()) do
        for i, p in ipairs(S.data.species[id].points) do
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
        for _, id in ipairs(speciesIds()) do
            local cfg = S.data.species[id]
            local mine = (id == S.selSpecies)
            for i, p in ipairs(cfg.points) do
                if p.z == v.z and inView(v, p.x, p.y) then
                    local sel = mine and i == S.selPoint
                    ctx.rect({ x = p.x, y = p.y, z = v.z, filled = true, color = rgba(cfg, mine and 150 or 70) })
                    ctx.rect({ x = p.x, y = p.y, z = v.z, filled = false, width = sel and 3 or (mine and 2 or 1),
                        color = sel and { 255, 255, 255, 255 } or rgba(cfg, 235) })
                    -- a propria criatura em pe no ponto (ctx.creature: build do RME de 2026-10-28 em
                    -- diante; no antigo fica so o quadrado). Meio transparente para nao parecer um
                    -- spawn de verdade do mapa; a escolhida mais forte.
                    if ctx.creature and cfg.lookType then
                        ctx.creature({ x = p.x, y = p.y, z = v.z, lookType = cfg.lookType, direction = 2,
                            opacity = mine and 0.9 or 0.55 })
                    end
                    if mine or (v.zoom or 1) <= 1 then
                        ctx.text({ x = p.x, y = p.y - 1, z = v.z, text = id .. " " .. i, color = rgba(cfg, 255) })
                    end
                end
            end
        end
    end,
    onhover = function(info)
        if not info.pos then return nil end
        S.hover = { x = info.pos.x, y = info.pos.y, z = info.pos.z }
        local id, i = hitAt(info.pos.x, info.pos.y, info.pos.z)
        if not id then return nil end
        return {
            tooltip = { text = string.format("%s, ponto %d (%d,%d,%d)", id, i, info.pos.x, info.pos.y, info.pos.z),
                color = { 255, 255, 255 } },
            highlight = { color = rgba(S.data.species[id], 120), filled = false, width = 2 },
        }
    end,
})
pcall(function()
    app.mapView:registerShow("Nascimento dos animais (BlackTalon)", OVERLAY_ID, { enabled = true })
end)

-- o desenho e cacheado pela vista: setEnabled soma a revisao e refaz
local function redraw()
    app.mapView:setEnabled(OVERLAY_ID, true)
    app.refresh()
end

-- ---------------------------------------------------------------------------
-- Conferencia
-- ---------------------------------------------------------------------------

-- Dois pontos colados viram um so na pratica: o animal ronda 3 sqm de onde nasceu.
local MIN_SPACING = 2

local function runChecks()
    local lines, problems = {}, 0
    local function bad(fmt, ...)
        problems = problems + 1
        lines[#lines + 1] = string.format(fmt, ...)
    end
    local seen = {}
    for _, id in ipairs(speciesIds()) do
        local cfg = S.data.species[id]
        local max = math.floor(tonumber(cfg.max) or 0)
        local lo, hi = tonumber(cfg.respawnMin) or 0, tonumber(cfg.respawnMax) or 0
        if not onServer(id) then
            bad("NAO EXISTE NO SERVER: %s (fora do FarmTameable; rode o link_farm_spawns.cjs)", id)
        end
        if max > 0 and #cfg.points == 0 then
            bad("SEM PONTOS: %s quer %d vivos e nao tem onde nascer", id, max)
        elseif max > #cfg.points then
            bad("POUCOS PONTOS: %s quer %d vivos e tem %d pontos (cada ponto segura um animal por vez)", id, max, #cfg.points)
        end
        if lo > hi then
            bad("RESPAWN INVERTIDO: %s com minimo %d maior que o maximo %d", id, lo, hi)
        end
        for i, p in ipairs(cfg.points) do
            local key = p.x .. "," .. p.y .. "," .. p.z
            if seen[key] then
                bad("REPETIDO: %s %d e %s em (%s)", id, i, seen[key], key)
            end
            seen[key] = id .. " " .. i
            local t = app.map:getTile(p.x, p.y, p.z)
            if not t or not t.ground then
                bad("SEM CHAO: %s %d (%s)", id, i, key)
            elseif t.isBlocking then
                bad("BLOQUEADO: %s %d (%s)", id, i, key)
            elseif t.isPZ then
                bad("EM PZ: %s %d (%s) -- animal nao nasce em zona protegida", id, i, key)
            end
            for j = i + 1, #cfg.points do
                local q = cfg.points[j]
                if q.z == p.z and math.max(math.abs(q.x - p.x), math.abs(q.y - p.y)) < MIN_SPACING then
                    bad("COLADOS: %s %d e %d a menos de %d sqm", id, i, j, MIN_SPACING)
                end
            end
        end
    end
    if problems == 0 then
        lines[#lines + 1] = "Tudo certo: toda criatura existe no server, tem pontos para o maximo, e os pontos tem chao livre fora de PZ."
    end
    return table.concat(lines, "\n"), problems
end

-- ---------------------------------------------------------------------------
-- A janela
-- ---------------------------------------------------------------------------

S.selSpecies = (S.selSpecies and S.data.species[S.selSpecies]) and S.selSpecies or speciesIds()[1]
S.selPoint = S.selPoint or 1

local function statusText()
    return (S.dirty and "ALTERACOES NAO SALVAS. " or "Salvo. ") .. "Arquivo: " .. tostring(store.path)
end

local function listItems()
    local items = {}
    local cfg = current()
    for i, p in ipairs(cfg and cfg.points or {}) do
        items[#items + 1] = { text = string.format("Ponto %-3d (%d,%d,%d)", i, p.x, p.y, p.z) }
    end
    local sel = math.max(1, math.min(S.selPoint or 1, #items))
    return items, sel
end

local function refresh(d)
    local items, sel = listItems()
    local cfg = current()
    d:modify({
        list = { items = items, selection = sel },
        lbl_species = { text = speciesTitle(S.selSpecies) },
        lbl_status = { text = statusText() },
        max_alive = { value = math.floor(tonumber(cfg.max) or 0) },
        respawn_min = { value = math.floor(tonumber(cfg.respawnMin) or 0) },
        respawn_max = { value = math.floor(tonumber(cfg.respawnMax) or 0) },
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

local function selectedTile()
    if (app.selection.size or 0) == 0 then return nil end
    local b = app.selection.bounds
    if b.min.x ~= b.max.x or b.min.y ~= b.max.y then return nil end
    return { x = b.min.x, y = b.min.y, z = b.min.z }
end

local function addPoint(d, pos)
    local cfg = current()
    if not cfg or not pos then return end
    local id, i = hitAt(pos.x, pos.y, pos.z)
    if id then
        app.alert(string.format("Ja existe um ponto ai: %s %d.", id, i))
        return
    end
    cfg.points[#cfg.points + 1] = { x = pos.x, y = pos.y, z = pos.z }
    S.selPoint = #cfg.points
    changed(d)
end

local ids = speciesIds()

-- rodar de novo fecha a janela anterior (senao acumulam)
if S.dlg then pcall(function() S.dlg:close() end) end
S.runChecks = runChecks

local dlg = Dialog({ title = "Nascimento dos animais da fazenda (BlackTalon)", width = 720, height = 600, resizable = true })
S.dlg = dlg

dlg:label({ text = "Cada criatura: os pontos onde pode nascer. No startup nasce ate o maximo; capturou uma, ela volta" })
dlg:newrow()
dlg:label({ text = "depois do respawn num ponto livre sorteado, sempre fora da tela dos jogadores." })
dlg:newrow()
dlg:combobox({
    id = "species",
    label = "Criatura",
    options = ids,
    option = S.selSpecies,
    onchange = function(d)
        S.selSpecies = d.data.species
        S.selPoint = 1
        refresh(d)
        redraw()
        local cfg = current()
        goTo(cfg and cfg.points[1])
    end,
})
dlg:label({ id = "lbl_species", text = speciesTitle(S.selSpecies) })
dlg:newrow()
dlg:list({
    id = "list",
    height = 200,
    expand = true,
    items = (listItems()),
    onchange = function(d)
        S.selPoint = d.data.list
        refresh(d)
        redraw()
        local cfg = current()
        goTo(cfg and cfg.points[S.selPoint])
    end,
})
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Pontos da criatura escolhida", padding = 4, margin = 4, expand = false })
dlg:button({
    text = "Por no ultimo tile do mouse",
    onclick = function(d)
        if not S.hover then
            app.alert("Passe o mouse sobre o tile no mapa antes de clicar.")
            return
        end
        addPoint(d, S.hover)
    end,
})
dlg:button({
    text = "Por no tile selecionado",
    onclick = function(d)
        local pos = selectedTile()
        if not pos then
            app.alert("Selecione UM tile no mapa.")
            return
        end
        addPoint(d, pos)
    end,
})
dlg:button({
    text = "Mover o escolhido para o mouse",
    onclick = function(d)
        local cfg = current()
        local p = cfg and cfg.points[S.selPoint]
        if not p or not S.hover then
            app.alert("Escolha um ponto na lista e passe o mouse sobre o tile de destino.")
            return
        end
        p.x, p.y, p.z = S.hover.x, S.hover.y, S.hover.z
        changed(d)
    end,
})
dlg:button({
    text = "Apagar o escolhido",
    onclick = function(d)
        local cfg = current()
        if cfg and cfg.points[S.selPoint] then
            table.remove(cfg.points, S.selPoint)
            S.selPoint = math.max(1, S.selPoint - 1)
            changed(d)
        end
    end,
})
dlg:button({
    text = "Ir ate ele",
    onclick = function()
        local cfg = current()
        goTo(cfg and cfg.points[S.selPoint])
    end,
})
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Regras da criatura escolhida", padding = 4, margin = 4, expand = false })
dlg:number({ id = "max_alive", label = "Maximo vivo", value = 0, min = 0, max = 200 })
dlg:number({ id = "respawn_min", label = "Respawn de (min)", value = 0, min = 0, max = 1440 })
dlg:number({ id = "respawn_max", label = "ate (min)", value = 0, min = 0, max = 1440 })
dlg:button({
    text = "Aplicar",
    onclick = function(d)
        local cfg = current()
        if not cfg then return end
        cfg.max = math.floor(tonumber(d.data.max_alive) or 0)
        cfg.respawnMin = math.floor(tonumber(d.data.respawn_min) or 0)
        cfg.respawnMax = math.floor(tonumber(d.data.respawn_max) or 0)
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
        print("[farm_spawns] conferencia:\n" .. text)
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
            app.alert("spawns.json invalido ou ausente: " .. tostring(store.path))
        end
    end,
})
dlg:button({
    text = "Salvar",
    onclick = function(d)
        if store:save(S.data) then
            S.dirty = false
            refresh(d)
            app.alert("Salvo.\nNo server: /reload scripts (ou /farmspawns reload) para valer.")
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
