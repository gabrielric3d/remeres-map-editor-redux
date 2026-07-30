-- Find by Action ID
-- Procura um Action ID dentro de uma area retangular (from_pos -> to_pos).
-- Para cada tile onde algum item (ground ou nao-ground) tem o action id
-- escolhido, registra a posicao e os IDs dos itens que batem. Os resultados
-- sao exibidos numa lista; selecionar/duplo clicar teleporta a camera para a
-- posicao.

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

-- Coleta os itens do tile (ground + nao-ground) que tem o action id alvo.
-- Cada entrada tem: id, action_id, unique_id, ground (bool).
-- Se filter_item_id for informado (> 0), so considera itens cujo id bate.
-- Retorna a lista de matches (pode ser vazia).
local function collectActionMatches(tile, target_aid, filter_item_id)
    local matches = {}

    local function consider(it, is_ground)
        if it.actionId ~= target_aid then return end
        if filter_item_id and filter_item_id > 0 and it.id ~= filter_item_id then
            return
        end
        matches[#matches + 1] = {
            id = it.id,
            action_id = it.actionId,
            unique_id = it.uniqueId,
            ground = is_ground,
        }
    end

    if tile.hasGround and tile.ground then
        consider(tile.ground, true)
    end

    local items = tile.items
    if items then
        for i = 1, #items do
            local it = items[i]
            if it then consider(it, false) end
        end
    end

    return matches
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

-- Faz o scan da area procurando o target_aid. Retorna lista de hits:
--   { { x=, y=, z=, matches={ {id, action_id, unique_id, ground}, ... },
--       creature_name }, ... }
-- Tambem retorna o total de itens com o action id e quantos tiles foram
-- escaneados.
local function scanArea(x1, y1, z1, x2, y2, z2, target_aid, filter_item_id)
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
                    local matches = collectActionMatches(tile, target_aid, filter_item_id)
                    if #matches > 0 then
                        total_occurrences = total_occurrences + #matches
                        results[#results + 1] = {
                            x = x, y = y, z = z,
                            matches = matches,
                            creature_name = getTileCreatureName(tile),
                        }
                    end
                end
            end
        end
    end

    return results, total_occurrences, tiles_scanned
end

-- Monta o texto exibido em uma linha da list.
local function formatResultLine(hit, target_aid)
    local parts = {}
    for _, m in ipairs(hit.matches) do
        local tag = m.ground and " (ground)" or ""
        local uid = (m.unique_id and m.unique_id > 0)
            and string.format(" uid:%d", m.unique_id) or ""
        parts[#parts + 1] = string.format("id:%d%s%s", m.id, tag, uid)
    end
    local items_str = (#parts > 0) and table.concat(parts, ", ") or "?"
    local creature_suffix = hit.creature_name
        and string.format("  | creature: %s", hit.creature_name) or ""
    return string.format(
        "(%d, %d, %d)  aid:%d  ->  %s%s",
        hit.x, hit.y, hit.z, target_aid, items_str, creature_suffix
    )
end

local function formatTooltip(hit, target_aid)
    local lines = {}
    lines[#lines + 1] = string.format("Pos: (%d, %d, %d)", hit.x, hit.y, hit.z)
    lines[#lines + 1] = string.format("Action ID procurado: %d", target_aid)
    lines[#lines + 1] = string.format("Itens com este action id: %d", #hit.matches)
    lines[#lines + 1] = string.format("Criatura: %s",
        hit.creature_name or "nenhuma")
    for _, m in ipairs(hit.matches) do
        local tag = m.ground and " [ground]" or ""
        local uid = (m.unique_id and m.unique_id > 0)
            and string.format(" unique:%d", m.unique_id) or ""
        lines[#lines + 1] = string.format("  - id %d%s%s", m.id, tag, uid)
    end
    return table.concat(lines, "\n")
end

-- ---------------------------------------------------------------------------
-- Defaults (vem da selecao atual ou da camera)
-- ---------------------------------------------------------------------------

local defaults = {
    action_id = 100,
    filter_item_id = 0,
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
            defaults.from_x = (cam.x or 1000) - 50
            defaults.from_y = (cam.y or 1000) - 50
            defaults.from_z = cam.z or 7
            defaults.to_x   = (cam.x or 1000) + 50
            defaults.to_y   = (cam.y or 1000) + 50
            defaults.to_z   = cam.z or 7
        end
    end
end

-- Estado do script (compartilhado entre callbacks)
local state = {
    results = {},     -- ultimo scan
    target_aid = 0,
    filter_item_id = 0,
}

-- ---------------------------------------------------------------------------
-- Dialog
-- ---------------------------------------------------------------------------

local dlg = Dialog {
    title = "Find by Action ID",
    width = 560,
    height = 540,
    resizable = true,
}

dlg:label {
    text = "Procura um Action ID numa area retangular e lista as posicoes encontradas.",
}
dlg:newrow()

-- Action ID alvo
dlg:box { orient = "horizontal", label = "Action ID Procurado", padding = 8, margin = 4, expand = false }
    dlg:number { id = "action_id", label = "Action ID:", value = defaults.action_id, min = 0, max = 65535 }
dlg:endbox()

-- Filtro opcional por Item ID
dlg:box { orient = "vertical", label = "Filtro por Item ID", padding = 8, margin = 4, expand = false }
    dlg:check {
        id = "use_item_filter",
        text = "Somente quando o action id estiver neste item id:",
        selected = false
    }
    dlg:newrow()
    dlg:number { id = "filter_item_id", label = "Item ID:", value = defaults.filter_item_id, min = 0, max = 65535 }
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

    local target_aid = clampInt(data.action_id, 0, 65535)
    if target_aid <= 0 then
        app.alert("Informe um Action ID valido (> 0).")
        return
    end

    -- Filtro por item id: so vale se o checkbox estiver marcado e o id > 0.
    local filter_item_id = 0
    if data.use_item_filter then
        filter_item_id = clampInt(data.filter_item_id, 0, 65535)
        if filter_item_id <= 0 then
            app.alert("Filtro por Item ID marcado: informe um Item ID valido (> 0).")
            return
        end
    end

    local x1 = clampInt(data.from_x, 0, 65535)
    local y1 = clampInt(data.from_y, 0, 65535)
    local z1 = clampInt(data.from_z, 0, 15)
    local x2 = clampInt(data.to_x,   0, 65535)
    local y2 = clampInt(data.to_y,   0, 65535)
    local z2 = clampInt(data.to_z,   0, 15)

    local results, occurrences, scanned = scanArea(
        x1, y1, z1, x2, y2, z2, target_aid, filter_item_id
    )
    state.results = results
    state.target_aid = target_aid
    state.filter_item_id = filter_item_id

    -- Constroi os itens da list
    local list_items = {}
    if #results == 0 then
        list_items[#list_items + 1] = {
            text = string.format("Nenhum tile com action id %d encontrado.", target_aid),
            tooltip = ""
        }
    else
        for i, hit in ipairs(results) do
            list_items[#list_items + 1] = {
                text = formatResultLine(hit, target_aid),
                tooltip = formatTooltip(hit, target_aid),
            }
        end
    end

    local filter_note = (filter_item_id > 0)
        and string.format(" [filtro: item id %d]", filter_item_id) or ""
    d:modify {
        results_list = { items = list_items, selection = 0 },
        lbl_status = { text = string.format(
            "Encontrados %d tile(s) com action id %d (%d item(ns)) em %d tile(s) escaneado(s).%s",
            #results, target_aid, occurrences, scanned, filter_note
        ) }
    }

    print(string.format(
        "Find by Action ID: %d tile(s) com action id %d em %d tile(s) escaneado(s).%s",
        #results, target_aid, scanned, filter_note
    ))
end

-- Monta o texto da tabela completa (todas as posicoes encontradas) para
-- copiar ao clipboard. Uma linha por tile, com pos, action id e itens.
local function buildResultsText()
    local lines = {}
    lines[#lines + 1] = string.format(
        "Find by Action ID -> action id %d%s | %d resultado(s)",
        state.target_aid,
        (state.filter_item_id > 0)
            and string.format(" (item id %d)", state.filter_item_id) or "",
        #state.results
    )
    for _, hit in ipairs(state.results) do
        local parts = {}
        for _, m in ipairs(hit.matches) do
            local tag = m.ground and " (ground)" or ""
            local uid = (m.unique_id and m.unique_id > 0)
                and string.format(" uid:%d", m.unique_id) or ""
            parts[#parts + 1] = string.format("id:%d%s%s", m.id, tag, uid)
        end
        local items_str = (#parts > 0) and table.concat(parts, ", ") or "?"
        lines[#lines + 1] = string.format(
            "%d, %d, %d\t%s", hit.x, hit.y, hit.z, items_str
        )
    end
    return table.concat(lines, "\n")
end

local function copyResultsTable(d)
    if #state.results == 0 then
        app.alert("Nenhum resultado para copiar. Faca uma busca primeiro.")
        return
    end
    if not app.setClipboard then
        app.alert("API de clipboard indisponivel nesta versao.")
        return
    end
    app.setClipboard(buildResultsText())
    d:modify { lbl_status = { text = string.format(
        "Tabela com %d resultado(s) copiada para o clipboard.", #state.results
    ) } }
    print(string.format(
        "Find by Action ID: %d resultado(s) copiado(s) para o clipboard.",
        #state.results
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
        text = "Copiar Tabela Completa",
        onclick = function(d) copyResultsTable(d) end
    }
    dlg:button {
        text = "Fechar",
        onclick = function(d) d:close() end
    }
dlg:endbox()

dlg:show { wait = false }

print("Find by Action ID: dialog aberto.")
