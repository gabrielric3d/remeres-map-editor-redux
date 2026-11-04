-- Embaralhar offset dos itens (BlackTalon, plano 181)
-- Sorteia o draw offset (px) de cada item escolhido dentro da area, numa faixa
-- X/Y configuravel. Serve para quebrar a repeticao de mato, pedra, flor etc.
--
-- Area: a selecao atual ou um retangulo from pos -> to pos (com andares).
-- Itens: "Ler itens da area" lista os ids que existem nela; clicar numa linha
-- marca/desmarca. Tambem da para digitar ids (ex.: 2700, 2705-2710).
-- Chao: so aparece na lista com "Listar o chao"; escolhido o id, ele entra.
-- Modo "Substituir": offset novo = sorteio. Modo "Somar": offset atual + sorteio.
-- Tudo vira UM passo de desfazer (Ctrl+Z).
--
-- Textos da janela sem acento: as caixas wx do RME sao ANSI.

if not app then
    print("Error: RME Lua API not found.")
    return
end

if not app.hasMap() or not app.map then
    app.alert("Nenhum mapa aberto.")
    return
end

local map = app.map

local OFFSET_LIMIT = 64          -- o mesmo -64..64 do server, client e RME
local MAX_RECT_TILES = 1000000   -- trava contra um retangulo gigante por engano
local MODE_REPLACE = "Substituir (offset = sorteio)"
local MODE_ADD = "Somar ao offset atual"

-- ---------------------------------------------------------------------------
-- Utilitarios
-- ---------------------------------------------------------------------------

local function clampInt(value, lo, hi)
    value = math.floor(tonumber(value) or 0)
    if value < lo then value = lo end
    if value > hi then value = hi end
    return value
end

local function orderedPair(a, b)
    if a > b then return b, a end
    return a, b
end

-- "2700, 2705-2710 3000" -> { [2700]=true, [2705]=true, ..., [3000]=true }
local function parseIdText(text)
    local ids = {}
    local count = 0
    text = tostring(text or "")
    for token in text:gmatch("[^,;%s]+") do
        local a, b = token:match("^(%d+)%-(%d+)$")
        if a then
            local first, last = orderedPair(tonumber(a), tonumber(b))
            if last - first <= 5000 then
                for id = first, last do
                    if id >= 1 and id <= 65535 and not ids[id] then
                        ids[id] = true
                        count = count + 1
                    end
                end
            end
        else
            local id = tonumber(token)
            if id and id >= 1 and id <= 65535 and not ids[id] then
                ids[id] = true
                count = count + 1
            end
        end
    end
    return ids, count
end

-- O item do build atual tem o draw offset? (RME anterior ao plano 181 nao tem)
local function itemSupportsDrawOffset(item)
    local ok, value = pcall(function() return item.effectiveDrawOffset end)
    return ok and type(value) == "table"
end

-- ---------------------------------------------------------------------------
-- Area
-- ---------------------------------------------------------------------------

local function selectionHasTiles()
    local sel = app.selection
    return sel ~= nil and not sel.isEmpty
end

-- Devolve a lista de tiles da area escolhida, ou nil + mensagem de erro
local function gatherTiles(data)
    local tiles = {}

    if data.mode_selection then
        if not selectionHasTiles() then
            return nil, "A selecao esta vazia. Selecione uma area ou use o retangulo."
        end
        for _, tile in ipairs(app.selection.tiles) do
            if tile then
                tiles[#tiles + 1] = tile
            end
        end
        return tiles
    end

    local x1, x2 = orderedPair(clampInt(data.from_x, 0, 65535), clampInt(data.to_x, 0, 65535))
    local y1, y2 = orderedPair(clampInt(data.from_y, 0, 65535), clampInt(data.to_y, 0, 65535))
    local z1, z2 = orderedPair(clampInt(data.from_z, 0, 15), clampInt(data.to_z, 0, 15))

    local area = (x2 - x1 + 1) * (y2 - y1 + 1) * (z2 - z1 + 1)
    if area > MAX_RECT_TILES then
        return nil, string.format("Retangulo grande demais (%d tiles; limite %d).", area, MAX_RECT_TILES)
    end

    for z = z1, z2 do
        for y = y1, y2 do
            for x = x1, x2 do
                local tile = map:getTile(x, y, z)
                if tile then
                    tiles[#tiles + 1] = tile
                end
            end
        end
    end
    return tiles
end

-- Chama fn(item, isGround) para cada item do tile que entra na conta
local function forEachTileItem(tile, includeGround, fn)
    if includeGround then
        local ground = tile.ground
        if ground then
            fn(ground, true)
        end
    end
    local items = tile.items
    if items then
        for _, item in ipairs(items) do
            fn(item, false)
        end
    end
end

-- ---------------------------------------------------------------------------
-- Estado da lista de itens
-- ---------------------------------------------------------------------------

local state = {
    entries = {},   -- { {id=, name=, count=, withOffset=, isGround=}, ... } na ordem da lista
    marked = {},    -- [id] = true
}

local function countMarked()
    local n = 0
    for _ in pairs(state.marked) do n = n + 1 end
    return n
end

local function buildListItems()
    local rows = {}
    for _, entry in ipairs(state.entries) do
        local mark = state.marked[entry.id] and "[x]" or "[  ]"
        local text = string.format("%s  %d  %s  (x%d)", mark, entry.id, entry.name, entry.count)
        if entry.withOffset > 0 then
            text = text .. string.format("  - %d com offset", entry.withOffset)
        end
        if entry.isGround then
            text = text .. "  - chao"
        end

        local row = { text = text, tooltip = "Clique para marcar/desmarcar" }
        local ok, img = pcall(Image.fromItemSprite, entry.id)
        if ok and img then
            row.image = img
        end
        rows[#rows + 1] = row
    end
    if #rows == 0 then
        rows[1] = { text = "(clique em \"Ler itens da area\")" }
    end
    return rows
end

-- Sempre sem linha selecionada: neste RME o onleftclick da lista nunca dispara e o
-- onchange so vem quando a selecao MUDA. Sem selecao, todo clique (ate na mesma
-- linha) vira mudanca e chega no toggleRow.
local function refreshList(d)
    d:modify({ item_list = { items = buildListItems(), selection = 0 } })
end

local function setStatus(d, text)
    d:modify({ lbl_status = { text = text } })
    print("[offset_shuffle] " .. text)
end

-- Le os ids que existem na area e monta a lista (mantem as marcas que ainda existem)
local function scanArea(d)
    local data = d.data
    local tiles, err = gatherTiles(data)
    if not tiles then
        setStatus(d, err)
        return
    end

    local includeGround = data.include_ground and true or false
    local byId = {}
    local supported = nil

    for _, tile in ipairs(tiles) do
        forEachTileItem(tile, includeGround, function(item, isGround)
            if supported == nil then
                supported = itemSupportsDrawOffset(item)
            end
            local entry = byId[item.id]
            if not entry then
                entry = { id = item.id, name = item.name or "?", count = 0, withOffset = 0, isGround = isGround }
                byId[item.id] = entry
            end
            entry.count = entry.count + 1
            if supported and item.drawOffset then
                entry.withOffset = entry.withOffset + 1
            end
        end)
    end

    if supported == false then
        setStatus(d, "Este RME nao tem o draw offset por item (plano 181). Use o rme.exe novo.")
        return
    end

    local entries = {}
    for _, entry in pairs(byId) do
        entries[#entries + 1] = entry
    end
    -- Mais frequente primeiro: quem quer embaralhar costuma mirar o que repete
    table.sort(entries, function(a, b)
        if a.count ~= b.count then return a.count > b.count end
        return a.id < b.id
    end)

    local marked = {}
    for _, entry in ipairs(entries) do
        if state.marked[entry.id] then
            marked[entry.id] = true
        end
    end

    state.entries = entries
    state.marked = marked
    refreshList(d)
    setStatus(d, string.format("%d tile(s) lidos, %d id(s) diferentes. Clique nos itens para marcar.", #tiles, #entries))
end

-- ---------------------------------------------------------------------------
-- Aplicar
-- ---------------------------------------------------------------------------

local function chosenIds(data)
    local ids = {}
    local count = 0
    for id in pairs(state.marked) do
        ids[id] = true
        count = count + 1
    end
    local typed = parseIdText(data.typed_ids)
    for id in pairs(typed) do
        if not ids[id] then
            ids[id] = true
            count = count + 1
        end
    end
    return ids, count
end

local function applyOffsets(d, clearOnly)
    local data = d.data

    local ids, idCount = chosenIds(data)
    if idCount == 0 then
        setStatus(d, "Nenhum item escolhido: marque na lista ou digite os ids.")
        return
    end

    local tiles, err = gatherTiles(data)
    if not tiles then
        setStatus(d, err)
        return
    end

    -- O chao entra sempre que o id dele foi escolhido; a caixa "Listar o chao"
    -- so decide se ele aparece na lista (antes, desmarcar a caixa depois de ler
    -- a area fazia o piso marcado ser ignorado)
    local includeGround = true
    local skipExisting = data.skip_existing and true or false
    local addMode = (data.mode == MODE_ADD)

    local xMin, xMax = orderedPair(clampInt(data.x_min, -OFFSET_LIMIT, OFFSET_LIMIT), clampInt(data.x_max, -OFFSET_LIMIT, OFFSET_LIMIT))
    local yMin, yMax = orderedPair(clampInt(data.y_min, -OFFSET_LIMIT, OFFSET_LIMIT), clampInt(data.y_max, -OFFSET_LIMIT, OFFSET_LIMIT))

    -- Semente 0 = uma nova a cada clique; a usada sai no status para repetir o resultado.
    -- A nova vem do proprio gerador (o os.time so muda de segundo em segundo)
    local seed = clampInt(data.seed, 0, 2147483647)
    if seed == 0 then
        seed = math.random(1, 2147483646)
    end
    math.randomseed(seed)

    -- So os tiles com pelo menos um alvo entram na transacao
    local targets = {}
    local supported = nil
    for _, tile in ipairs(tiles) do
        local hasTarget = false
        forEachTileItem(tile, includeGround, function(item)
            if ids[item.id] then
                hasTarget = true
                if supported == nil then
                    supported = itemSupportsDrawOffset(item)
                end
            end
        end)
        if hasTarget then
            targets[#targets + 1] = tile
        end
    end

    if #targets == 0 then
        setStatus(d, "Nenhum dos itens escolhidos esta na area.")
        return
    end
    if supported == false then
        setStatus(d, "Este RME nao tem o draw offset por item (plano 181). Use o rme.exe novo.")
        return
    end

    local changed, skipped = 0, 0
    local label = clearOnly and "Zerar offset dos itens" or "Embaralhar offset dos itens"

    local ok, failure = pcall(app.transaction, label, function()
        for _, tile in ipairs(targets) do
            -- Mexer em atributo de item nao registra o tile no desfazer;
            -- reescrever os map flags (iguais) registra, e tem de vir ANTES
            tile.mapFlags = tile.mapFlags

            forEachTileItem(tile, includeGround, function(item)
                if not ids[item.id] then
                    return
                end

                if clearOnly then
                    if item.drawOffset then
                        item.drawOffset = nil
                        changed = changed + 1
                    end
                    return
                end

                if skipExisting and item.drawOffset then
                    skipped = skipped + 1
                    return
                end

                local baseX, baseY = 0, 0
                if addMode then
                    local current = item.effectiveDrawOffset
                    baseX, baseY = current.x or 0, current.y or 0
                end

                local x = clampInt(baseX + math.random(xMin, xMax), -OFFSET_LIMIT, OFFSET_LIMIT)
                local y = clampInt(baseY + math.random(yMin, yMax), -OFFSET_LIMIT, OFFSET_LIMIT)
                item.drawOffset = { x = x, y = y }
                changed = changed + 1
            end)
        end
    end)

    if not ok then
        setStatus(d, "Falhou (nada foi alterado): " .. tostring(failure))
        return
    end

    if app.refresh then app.refresh() end

    local text
    if clearOnly then
        text = string.format("Offset zerado em %d item(ns) de %d tile(s). Ctrl+Z desfaz.", changed, #targets)
    else
        text = string.format("Offset sorteado em %d item(ns) de %d tile(s), X %d..%d, Y %d..%d. Semente %d. Ctrl+Z desfaz.",
            changed, #targets, xMin, xMax, yMin, yMax, seed)
        if skipped > 0 then
            text = text .. string.format(" %d pulado(s) por ja ter offset.", skipped)
        end
    end
    setStatus(d, text)
    -- Atualiza a coluna "com offset" da lista
    if #state.entries > 0 then
        local status = text
        scanArea(d)
        setStatus(d, status)
    end
end

-- ---------------------------------------------------------------------------
-- Valores iniciais (selecao atual ou em volta da camera)
-- ---------------------------------------------------------------------------

local defaults = { from_x = 0, from_y = 0, from_z = 7, to_x = 0, to_y = 0, to_z = 7 }

do
    local sel = app.selection
    if sel and not sel.isEmpty and sel.bounds and sel.bounds.min and sel.bounds.max then
        local b = sel.bounds
        defaults.from_x, defaults.from_y, defaults.from_z = b.min.x, b.min.y, b.min.z
        defaults.to_x, defaults.to_y, defaults.to_z = b.max.x, b.max.y, b.max.z
    else
        local cam = app.getCameraPosition and app.getCameraPosition() or nil
        if cam then
            defaults.from_x, defaults.from_y, defaults.from_z = (cam.x or 1000) - 10, (cam.y or 1000) - 10, cam.z or 7
            defaults.to_x, defaults.to_y, defaults.to_z = (cam.x or 1000) + 10, (cam.y or 1000) + 10, cam.z or 7
        end
    end
end

local startWithSelection = selectionHasTiles()

-- ---------------------------------------------------------------------------
-- Janela
-- ---------------------------------------------------------------------------

local dlg = Dialog({
    title = "Embaralhar offset dos itens (BlackTalon)",
    width = 560,
    height = 720,
    resizable = true,
})

dlg:label({ text = "Sorteia o deslocamento de desenho (px) de cada item escolhido dentro da area." })
dlg:newrow()

dlg:box({ orient = "vertical", label = "Area", padding = 6, margin = 4, expand = false })
    dlg:radio({ id = "mode_selection", text = "Usar a selecao atual (vale a selecao na hora do clique)", selected = startWithSelection })
    dlg:newrow()
    dlg:radio({ id = "mode_rect", text = "Usar o retangulo (from pos -> to pos)", selected = not startWithSelection })
    dlg:newrow()
    dlg:box({ orient = "horizontal", padding = 0, expand = false })
        dlg:number({ id = "from_x", label = "From X:", value = defaults.from_x, min = 0, max = 65535 })
        dlg:number({ id = "from_y", label = "Y:", value = defaults.from_y, min = 0, max = 65535 })
        dlg:number({ id = "from_z", label = "Z:", value = defaults.from_z, min = 0, max = 15 })
    dlg:endbox()
    dlg:newrow()
    dlg:box({ orient = "horizontal", padding = 0, expand = false })
        dlg:number({ id = "to_x", label = "To X:", value = defaults.to_x, min = 0, max = 65535 })
        dlg:number({ id = "to_y", label = "Y:", value = defaults.to_y, min = 0, max = 65535 })
        dlg:number({ id = "to_z", label = "Z:", value = defaults.to_z, min = 0, max = 15 })
    dlg:endbox()
    dlg:newrow()
    dlg:check({ id = "include_ground", text = "Listar o chao tambem (piso deslocado abre fresta entre os tiles)", selected = false, onclick = function(d)
        if #state.entries > 0 then
            scanArea(d)
        end
    end })
dlg:endbox()

dlg:box({ orient = "vertical", label = "Itens (clique na linha para marcar/desmarcar)", padding = 6, margin = 4, expand = true })
    dlg:box({ orient = "horizontal", padding = 0, expand = false })
        dlg:button({ text = "Ler itens da area", onclick = function(d) scanArea(d) end })
        dlg:button({ text = "Marcar todos", onclick = function(d)
            for _, entry in ipairs(state.entries) do
                state.marked[entry.id] = true
            end
            refreshList(d)
        end })
        dlg:button({ text = "Desmarcar todos", onclick = function(d)
            state.marked = {}
            refreshList(d)
        end })
    dlg:endbox()
    dlg:newrow()
    dlg:list({
        id = "item_list",
        items = buildListItems(),
        icon_size = 32,
        item_height = 36,
        height = 240,
        expand = true,
        onchange = function(d)
            local entry = state.entries[tonumber(d.data.item_list) or 0]
            if not entry then
                return
            end
            if state.marked[entry.id] then
                state.marked[entry.id] = nil
            else
                state.marked[entry.id] = true
            end
            refreshList(d)
            d:modify({ lbl_status = { text = string.format("%d id(s) marcados.", countMarked()) } })
        end,
    })
    dlg:newrow()
    dlg:input({ id = "typed_ids", label = "Ou digite ids:", text = "", width = 300 })
    dlg:newrow()
    dlg:label({ text = "Ex.: 2700, 2705-2710. Soma com os marcados na lista." })
dlg:endbox()

dlg:box({ orient = "vertical", label = "Faixa do offset (px, -64..64; 32 = um tile)", padding = 6, margin = 4, expand = false })
    dlg:box({ orient = "horizontal", padding = 0, expand = false })
        dlg:number({ id = "x_min", label = "X de:", value = -4, min = -OFFSET_LIMIT, max = OFFSET_LIMIT })
        dlg:number({ id = "x_max", label = "ate:", value = 4, min = -OFFSET_LIMIT, max = OFFSET_LIMIT })
    dlg:endbox()
    dlg:newrow()
    dlg:box({ orient = "horizontal", padding = 0, expand = false })
        dlg:number({ id = "y_min", label = "Y de:", value = -4, min = -OFFSET_LIMIT, max = OFFSET_LIMIT })
        dlg:number({ id = "y_max", label = "ate:", value = 4, min = -OFFSET_LIMIT, max = OFFSET_LIMIT })
    dlg:endbox()
    dlg:newrow()
    dlg:combobox({ id = "mode", label = "Modo:", options = { MODE_REPLACE, MODE_ADD }, option = MODE_REPLACE })
    dlg:newrow()
    dlg:check({ id = "skip_existing", text = "Pular itens que ja tem offset proprio (os ajustados a mao)", selected = false })
    dlg:newrow()
    dlg:number({ id = "seed", label = "Semente (0 = nova a cada clique):", value = 0, min = 0, max = 2147483647 })
dlg:endbox()

dlg:separator()
dlg:label({ id = "lbl_status", text = "Selecione a area e clique em \"Ler itens da area\"." })
dlg:newrow()

dlg:box({ orient = "horizontal", padding = 4, margin = 4, expand = false })
    dlg:button({ text = "Embaralhar", onclick = function(d) applyOffsets(d, false) end })
    dlg:button({ text = "Zerar offset", onclick = function(d) applyOffsets(d, true) end })
    dlg:button({ text = "Fechar", onclick = function(d) d:close() end })
dlg:endbox()

dlg:show({ wait = false })

-- Ja abre com a lista preenchida quando ha selecao
if startWithSelection then
    scanArea(dlg)
end
