-- Find Item by ID
-- Procura um item ID dentro de uma area retangular (from_pos -> to_pos).
-- Para cada tile onde o item aparece, registra a posicao e TODOS os outros
-- IDs presentes no tile (ground + itens). Os resultados sao exibidos numa
-- lista; selecionar/duplo clicar teleporta a camera para a posicao.

if not app then
    print("Erro: API Lua do RME nao encontrada.")
    return
end

if not app.hasMap() or not app.map then
    app.alert("Nenhum mapa aberto.")
    return
end

local map = app.map

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------

local function clampInt(value, lo, hi)
    value = math.floor(tonumber(value) or 0)
    if value < lo then value = lo end
    if value > hi then value = hi end
    return value
end

local function normalizeRange(a, b)
    if a > b then return b, a end
    return a, b
end

-- Extrai 3 numeros de uma string. Aceita varios formatos:
--   "1267, 46, 7"  /  "1267,46,7"  /  "1267 46 7"
--   "x:1267 y:46 z:7"  /  "{x=1267, y=46, z=7}"  /  "(1267, 46, 7)"
-- Retorna x, y, z ou nil se nao encontrar 3 numeros.
local function parsePosition(text)
    if not text or text == "" then return nil end
    local nums = {}
    for n in string.gmatch(text, "%-?%d+") do
        nums[#nums + 1] = tonumber(n)
        if #nums == 3 then break end
    end
    if #nums < 3 then return nil end
    return nums[1], nums[2], nums[3]
end

local function formatPosition(x, y, z)
    return string.format("%d, %d, %d", x, y, z)
end

-- Coleta todos os IDs presentes no tile (ground + itens nao-ground).
-- Cada entrada tem: id, ground (bool), blocking (bool).
local function collectTileIds(tile)
    local ids = {}
    if tile.hasGround and tile.ground then
        local g = tile.ground
        ids[#ids + 1] = {
            id = g.id,
            ground = true,
            blocking = g.isBlocking and true or false,
        }
    end
    local items = tile.items
    if items then
        for i = 1, #items do
            local it = items[i]
            if it then
                ids[#ids + 1] = {
                    id = it.id,
                    ground = false,
                    blocking = it.isBlocking and true or false,
                }
            end
        end
    end
    return ids
end

-- Verifica se algum item do tile (incluindo ground) e bloqueante.
local function tileHasUnpassableItem(tile)
    if tile.hasGround and tile.ground and tile.ground.isBlocking then
        return true
    end
    local items = tile.items
    if items then
        for i = 1, #items do
            local it = items[i]
            if it and it.isBlocking then
                return true
            end
        end
    end
    return false
end

-- Verifica se algum ID do tile bate com o target_id.
-- Retorna true se encontrou no ground, no item, ou ambos.
local function tileHasItemId(tile, target_id)
    if tile.hasGround and tile.ground and tile.ground.id == target_id then
        return true
    end
    local items = tile.items
    if items then
        for i = 1, #items do
            local it = items[i]
            if it and it.id == target_id then
                return true
            end
        end
    end
    return false
end

-- Pega o nome da criatura no tile (se existir). Retorna nil se nao tem.
local function getTileCreatureName(tile)
    if not tile.hasCreature then return nil end
    local c = tile.creature
    if not c then return nil end
    local name = c.name or ""
    if c.isNpc then name = name .. " (NPC)" end
    return name
end

-- Faz o scan da area procurando target_id. Retorna lista de hits:
--   { { x=, y=, z=, ids={ {id, ground, blocking}, ... }, has_block, creature_name }, ... }
-- Filtros:
--   only_unpassable:    so tiles com pelo menos um item bloqueante.
--   only_with_creature: so tiles que tem uma criatura colocada.
-- Tambem retorna o total de ocorrencias do ID e quantos tiles foram escaneados.
local function scanArea(x1, y1, z1, x2, y2, z2, target_id, only_unpassable, only_with_creature)
    x1, x2 = normalizeRange(x1, x2)
    y1, y2 = normalizeRange(y1, y2)
    z1, z2 = normalizeRange(z1, z2)

    local results = {}
    local total_occurrences = 0
    local tiles_scanned = 0

    for z = z1, z2 do
        for y = y1, y2 do
            for x = x1, x2 do
                local tile = map:getTile(x, y, z)
                if tile then
                    tiles_scanned = tiles_scanned + 1
                    if tileHasItemId(tile, target_id) then
                        local include = true
                        if only_unpassable and not tileHasUnpassableItem(tile) then
                            include = false
                        end
                        if include and only_with_creature and not tile.hasCreature then
                            include = false
                        end
                        if include then
                            local ids = collectTileIds(tile)
                            local has_block = false
                            for _, entry in ipairs(ids) do
                                if entry.id == target_id then
                                    total_occurrences = total_occurrences + 1
                                end
                                if entry.blocking then has_block = true end
                            end
                            results[#results + 1] = {
                                x = x, y = y, z = z,
                                ids = ids,
                                has_block = has_block,
                                creature_name = getTileCreatureName(tile),
                            }
                        end
                    end
                end
            end
        end
    end

    return results, total_occurrences, tiles_scanned
end

-- Monta o texto exibido em uma linha da list.
local function formatResultLine(hit, target_id)
    -- Outros IDs = todos do tile exceto a primeira ocorrencia do target.
    local others = {}
    local skipped = false
    for _, entry in ipairs(hit.ids) do
        if entry.id == target_id and not skipped then
            skipped = true  -- pula a primeira ocorrencia (e o item procurado)
        else
            local tag = ""
            if entry.ground then tag = tag .. " (ground)" end
            if entry.blocking then tag = tag .. " [block]" end
            others[#others + 1] = tostring(entry.id) .. tag
        end
    end

    local others_str
    if #others == 0 then
        others_str = "sem outros itens"
    else
        others_str = table.concat(others, ", ")
    end

    local block_prefix = hit.has_block and "[X] " or "[ ] "
    local creature_suffix = hit.creature_name
        and string.format("  | creature: %s", hit.creature_name) or ""
    return string.format(
        "%s(%d, %d, %d)  ->  %s%s",
        block_prefix, hit.x, hit.y, hit.z, others_str, creature_suffix
    )
end

local function formatTooltip(hit)
    local lines = {}
    lines[#lines + 1] = string.format("Pos: (%d, %d, %d)", hit.x, hit.y, hit.z)
    lines[#lines + 1] = string.format("Total de IDs no tile: %d", #hit.ids)
    lines[#lines + 1] = string.format("Contem item bloqueante: %s",
        hit.has_block and "SIM" or "nao")
    lines[#lines + 1] = string.format("Criatura: %s",
        hit.creature_name or "nenhuma")
    for _, entry in ipairs(hit.ids) do
        local tag = ""
        if entry.ground then tag = tag .. " [ground]" end
        if entry.blocking then tag = tag .. " [block]" end
        lines[#lines + 1] = "  - " .. tostring(entry.id) .. tag
    end
    return table.concat(lines, "\n")
end

-- ---------------------------------------------------------------------------
-- Defaults (vem da selecao atual ou da camera)
-- ---------------------------------------------------------------------------

local defaults = {
    item_id = 100,
    from_x = 0, from_y = 0, from_z = 7,
    to_x   = 0, to_y   = 0, to_z   = 7,
}

do
    local sel = app.selection
    if sel and not sel.isEmpty and sel.bounds then
        local b = sel.bounds
        if b.min and b.max then
            defaults.from_x = b.min.x or defaults.from_x
            defaults.from_y = b.min.y or defaults.from_y
            defaults.from_z = b.min.z or defaults.from_z
            defaults.to_x   = b.max.x or defaults.to_x
            defaults.to_y   = b.max.y or defaults.to_y
            defaults.to_z   = b.max.z or defaults.to_z
        end
    else
        local cam = app.getCameraPosition and app.getCameraPosition() or nil
        if cam then
            defaults.from_x = (cam.x or 1000) - 20
            defaults.from_y = (cam.y or 1000) - 20
            defaults.from_z = cam.z or 7
            defaults.to_x   = (cam.x or 1000) + 20
            defaults.to_y   = (cam.y or 1000) + 20
            defaults.to_z   = cam.z or 7
        end
    end
end

-- Estado do script (compartilhado entre callbacks)
local state = {
    results = {},     -- ultimo scan
    target_id = 0,
}

-- ---------------------------------------------------------------------------
-- Dialog
-- ---------------------------------------------------------------------------

local dlg = Dialog {
    title = "Find Item by ID",
    width = 560,
    height = 560,
    resizable = true,
}

dlg:label {
    text = "Procura um item ID numa area retangular e lista as posicoes encontradas.",
}
dlg:newrow()

-- ID alvo
dlg:box { orient = "horizontal", label = "Item Procurado", padding = 8, margin = 4, expand = false }
    dlg:number { id = "item_id", label = "Item ID:", value = defaults.item_id, min = 0, max = 65535 }
dlg:endbox()

-- Filtros
dlg:box { orient = "vertical", label = "Filtros", padding = 8, margin = 4, expand = false }
    dlg:check {
        id = "only_unpassable",
        text = "Somente tiles com item unpassable (bloqueante)",
        selected = false
    }
    dlg:newrow()
    dlg:check {
        id = "only_with_creature",
        text = "Somente tiles com criatura colocada",
        selected = false
    }
dlg:endbox()

-- Copiar / Colar posicao
-- Use Ctrl+C/Ctrl+V no campo de texto para copiar/colar a posicao.
-- Formato aceito: "1267, 46, 7" (tambem aceita espacos, parenteses, {x=,y=,z=}).
dlg:box { orient = "vertical", label = "Copiar / Colar Posicao", padding = 8, margin = 4, expand = false }
    dlg:label { text = "Cole aqui (Ctrl+V) e clique para aplicar, ou clique em From/To para copiar (Ctrl+C):" }
    dlg:newrow()
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:input { id = "pos_text", label = "Pos:", text = formatPosition(defaults.from_x, defaults.from_y, defaults.from_z) }
    dlg:endbox()
    dlg:newrow()
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:button {
            text = "Colar -> From",
            onclick = function(d)
                local px, py, pz = parsePosition(d.data.pos_text or "")
                if not px then
                    app.alert("Posicao invalida. Use o formato 'x, y, z'.")
                    return
                end
                d:modify {
                    from_x = { value = clampInt(px, 0, 65535) },
                    from_y = { value = clampInt(py, 0, 65535) },
                    from_z = { value = clampInt(pz, 0, 15) },
                }
            end
        }
        dlg:button {
            text = "Colar -> To",
            onclick = function(d)
                local px, py, pz = parsePosition(d.data.pos_text or "")
                if not px then
                    app.alert("Posicao invalida. Use o formato 'x, y, z'.")
                    return
                end
                d:modify {
                    to_x = { value = clampInt(px, 0, 65535) },
                    to_y = { value = clampInt(py, 0, 65535) },
                    to_z = { value = clampInt(pz, 0, 15) },
                }
            end
        }
        dlg:button {
            text = "From -> Copiar",
            onclick = function(d)
                local fx = clampInt(d.data.from_x, 0, 65535)
                local fy = clampInt(d.data.from_y, 0, 65535)
                local fz = clampInt(d.data.from_z, 0, 15)
                d:modify { pos_text = { text = formatPosition(fx, fy, fz) } }
            end
        }
        dlg:button {
            text = "To -> Copiar",
            onclick = function(d)
                local tx = clampInt(d.data.to_x, 0, 65535)
                local ty = clampInt(d.data.to_y, 0, 65535)
                local tz = clampInt(d.data.to_z, 0, 15)
                d:modify { pos_text = { text = formatPosition(tx, ty, tz) } }
            end
        }
        dlg:button {
            text = "Camera -> Pos",
            onclick = function(d)
                local cam = app.getCameraPosition and app.getCameraPosition() or nil
                if not cam then
                    app.alert("Nao foi possivel obter a posicao atual da camera.")
                    return
                end
                d:modify { pos_text = { text = formatPosition(cam.x or 0, cam.y or 0, cam.z or 7) } }
            end
        }
    dlg:endbox()
dlg:endbox()

-- Area
dlg:box { orient = "vertical", label = "Area (from_pos -> to_pos)", padding = 8, margin = 4, expand = false }
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:number { id = "from_x", label = "From X:", value = defaults.from_x, min = 0, max = 65535 }
        dlg:number { id = "from_y", label = "Y:",      value = defaults.from_y, min = 0, max = 65535 }
        dlg:number { id = "from_z", label = "Z:",      value = defaults.from_z, min = 0, max = 15 }
    dlg:endbox()
    dlg:newrow()
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:number { id = "to_x", label = "To X:", value = defaults.to_x, min = 0, max = 65535 }
        dlg:number { id = "to_y", label = "Y:",    value = defaults.to_y, min = 0, max = 65535 }
        dlg:number { id = "to_z", label = "Z:",    value = defaults.to_z, min = 0, max = 15 }
    dlg:endbox()
dlg:endbox()

dlg:separator()
dlg:label { id = "lbl_status", text = "Pressione 'Buscar' para escanear a area." }
dlg:newrow()

-- Lista de resultados (clicavel/duplo-clique teleporta)
dlg:label { text = "Resultados (selecione uma linha para teleportar):" }
dlg:newrow()
dlg:list {
    id = "results_list",
    height = 240,
    expand = true,
    items = { { text = "(nenhum resultado ainda)", tooltip = "" } },
    onchange = function(d)
        local idx = d.data.results_list
        if not idx or idx <= 0 then return end
        local hit = state.results[idx]
        if not hit then return end
        if app.setCameraPosition then
            app.setCameraPosition(hit.x, hit.y, hit.z)
            d:modify { lbl_status = { text = string.format(
                "Teleportado para (%d, %d, %d). %d resultado(s) no total.",
                hit.x, hit.y, hit.z, #state.results
            ) } }
        end
    end,
    ondoubleclick = function(d)
        local idx = d.data.results_list
        if not idx or idx <= 0 then return end
        local hit = state.results[idx]
        if not hit then return end
        if app.setCameraPosition then
            app.setCameraPosition(hit.x, hit.y, hit.z)
        end
    end,
}
dlg:newrow()

-- ---------------------------------------------------------------------------
-- Acoes
-- ---------------------------------------------------------------------------

local function runSearch(d)
    local data = d.data

    local target_id = clampInt(data.item_id, 0, 65535)
    if target_id <= 0 then
        app.alert("Informe um Item ID valido (> 0).")
        return
    end

    local x1 = clampInt(data.from_x, 0, 65535)
    local y1 = clampInt(data.from_y, 0, 65535)
    local z1 = clampInt(data.from_z, 0, 15)
    local x2 = clampInt(data.to_x,   0, 65535)
    local y2 = clampInt(data.to_y,   0, 65535)
    local z2 = clampInt(data.to_z,   0, 15)

    local only_unpassable    = data.only_unpassable    and true or false
    local only_with_creature = data.only_with_creature and true or false

    local results, occurrences, scanned = scanArea(
        x1, y1, z1, x2, y2, z2, target_id, only_unpassable, only_with_creature
    )
    state.results = results
    state.target_id = target_id

    -- Constroi os itens da list
    local list_items = {}
    if #results == 0 then
        list_items[#list_items + 1] = {
            text = string.format("Nenhum tile com item ID %d encontrado.", target_id),
            tooltip = ""
        }
    else
        for i, hit in ipairs(results) do
            list_items[#list_items + 1] = {
                text = formatResultLine(hit, target_id),
                tooltip = formatTooltip(hit),
            }
        end
    end

    local filter_parts = {}
    if only_unpassable    then filter_parts[#filter_parts + 1] = "unpassable" end
    if only_with_creature then filter_parts[#filter_parts + 1] = "creature"   end
    local filter_note = (#filter_parts > 0)
        and (" [filtros: " .. table.concat(filter_parts, ", ") .. "]")
        or ""
    d:modify {
        results_list = { items = list_items, selection = 0 },
        lbl_status = { text = string.format(
            "Encontrados %d tile(s) com ID %d (%d ocorrencia(s)) em %d tile(s) escaneado(s).%s",
            #results, target_id, occurrences, scanned, filter_note
        ) }
    }

    print(string.format(
        "Find Item by ID: %d tile(s) com ID %d em %d tile(s) escaneado(s).",
        #results, target_id, scanned
    ))
end

local function teleportSelected(d)
    local idx = d.data.results_list
    if not idx or idx <= 0 or not state.results[idx] then
        app.alert("Selecione uma linha da lista primeiro.")
        return
    end
    local hit = state.results[idx]
    if app.setCameraPosition then
        app.setCameraPosition(hit.x, hit.y, hit.z)
        d:modify { lbl_status = { text = string.format(
            "Teleportado para (%d, %d, %d).", hit.x, hit.y, hit.z
        ) } }
    end
end

dlg:box { orient = "horizontal", padding = 4, margin = 4, expand = false }
    dlg:button {
        text = "Buscar",
        onclick = function(d) runSearch(d) end
    }
    dlg:button {
        text = "Teleportar Selecionado",
        onclick = function(d) teleportSelected(d) end
    }
    dlg:button {
        text = "Resultado -> Copiar",
        onclick = function(d)
            local idx = d.data.results_list
            if not idx or idx <= 0 or not state.results[idx] then
                app.alert("Selecione uma linha da lista primeiro.")
                return
            end
            local hit = state.results[idx]
            d:modify { pos_text = { text = formatPosition(hit.x, hit.y, hit.z) } }
        end
    }
    dlg:button {
        text = "Fechar",
        onclick = function(d) d:close() end
    }
dlg:endbox()

dlg:show { wait = false }

print("Find Item by ID: dialog aberto.")
