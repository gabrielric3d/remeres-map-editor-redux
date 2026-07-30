-- Fill Missing Corners
-- Escaneia tiles e adiciona CORNER (cnw/cne/csw/cse) e DIAGONAL (dnw/dne/dsw/dse)
-- que estejam faltando.
--
-- CORNER interno (concavo) — items no MESMO tile:
--   N + W  ->  CNW
--   N + E  ->  CNE
--   S + W  ->  CSW
--   S + E  ->  CSE
--
-- DIAGONAL externo (convexo) — detectado pelos VIZINHOS:
--   N-neighbor tem 'w' E W-neighbor tem 'n'  ->  adiciona DNW em T
--   N-neighbor tem 'e' E E-neighbor tem 'n'  ->  adiciona DNE em T
--   S-neighbor tem 'w' E W-neighbor tem 's'  ->  adiciona DSW em T
--   S-neighbor tem 'e' E E-neighbor tem 's'  ->  adiciona DSE em T
--   (T nao deve ter a borda cardeal correspondente: se tem 'n' ou 'w', nao vira dnw, etc.)
--
-- Modos:
--   * Selecao  : itera sobre os tiles selecionados
--   * Retangulo: itera sobre a regiao cuboide (from_pos -> to_pos)
--
-- Opcao Preview: conta quantos items seriam adicionados sem modificar o mapa.

if not app then
    print("Error: RME Lua API not found.")
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

-- Retorna true se o tile contém ao menos um item com esse ID
-- (ignora o ground, checa apenas os items na pilha).
local function tileHasItemId(tile, id)
    if not tile or not id or id <= 0 then return false end
    local items = tile.items
    if not items then return false end
    for i = 1, #items do
        local it = items[i]
        if it and it.id == id then
            return true
        end
    end
    return false
end

-- Tenta adicionar o item corner no tile. Retorna true se adicionou.
local function addCorner(tile, cornerId)
    if not tile or not cornerId or cornerId <= 0 then return false end
    local ok, added = pcall(function() return tile:addItem(cornerId) end)
    return ok and added ~= nil
end

-- Atalho para pegar um vizinho (pode ser nil se tile nao existir no mapa).
local function neighbor(tile, dx, dy)
    if not tile then return nil end
    return map:getTile(tile.x + dx, tile.y + dy, tile.z)
end

-- Processa um tile, devolvendo quantos items seriam/foram adicionados.
local function processTile(tile, ids, hits)
    if not tile then return 0 end

    local hasN = tileHasItemId(tile, ids.n)
    local hasE = tileHasItemId(tile, ids.e)
    local hasS = tileHasItemId(tile, ids.s)
    local hasW = tileHasItemId(tile, ids.w)

    local count = 0

    -- ---- CORNERS (internos, mesmo tile) ------------------------------------
    -- N + W -> CNW
    if hasN and hasW and ids.cnw > 0 and not tileHasItemId(tile, ids.cnw) then
        count = count + 1
        if hits then hits[#hits + 1] = { tile = tile, id = ids.cnw } end
    end
    -- N + E -> CNE
    if hasN and hasE and ids.cne > 0 and not tileHasItemId(tile, ids.cne) then
        count = count + 1
        if hits then hits[#hits + 1] = { tile = tile, id = ids.cne } end
    end
    -- S + W -> CSW
    if hasS and hasW and ids.csw > 0 and not tileHasItemId(tile, ids.csw) then
        count = count + 1
        if hits then hits[#hits + 1] = { tile = tile, id = ids.csw } end
    end
    -- S + E -> CSE
    if hasS and hasE and ids.cse > 0 and not tileHasItemId(tile, ids.cse) then
        count = count + 1
        if hits then hits[#hits + 1] = { tile = tile, id = ids.cse } end
    end

    -- ---- DIAGONAIS (externos, checa vizinhos) ------------------------------
    -- DNW: N-neighbor tem 'w' E W-neighbor tem 'n'; T nao tem 'n' nem 'w'.
    if ids.dnw > 0 and not hasN and not hasW and not tileHasItemId(tile, ids.dnw) then
        local nTile = neighbor(tile, 0, -1)
        local wTile = neighbor(tile, -1, 0)
        if tileHasItemId(nTile, ids.w) and tileHasItemId(wTile, ids.n) then
            count = count + 1
            if hits then hits[#hits + 1] = { tile = tile, id = ids.dnw } end
        end
    end
    -- DNE: N-neighbor tem 'e' E E-neighbor tem 'n'; T nao tem 'n' nem 'e'.
    if ids.dne > 0 and not hasN and not hasE and not tileHasItemId(tile, ids.dne) then
        local nTile = neighbor(tile, 0, -1)
        local eTile = neighbor(tile, 1, 0)
        if tileHasItemId(nTile, ids.e) and tileHasItemId(eTile, ids.n) then
            count = count + 1
            if hits then hits[#hits + 1] = { tile = tile, id = ids.dne } end
        end
    end
    -- DSW: S-neighbor tem 'w' E W-neighbor tem 's'; T nao tem 's' nem 'w'.
    if ids.dsw > 0 and not hasS and not hasW and not tileHasItemId(tile, ids.dsw) then
        local sTile = neighbor(tile, 0, 1)
        local wTile = neighbor(tile, -1, 0)
        if tileHasItemId(sTile, ids.w) and tileHasItemId(wTile, ids.s) then
            count = count + 1
            if hits then hits[#hits + 1] = { tile = tile, id = ids.dsw } end
        end
    end
    -- DSE: S-neighbor tem 'e' E E-neighbor tem 's'; T nao tem 's' nem 'e'.
    if ids.dse > 0 and not hasS and not hasE and not tileHasItemId(tile, ids.dse) then
        local sTile = neighbor(tile, 0, 1)
        local eTile = neighbor(tile, 1, 0)
        if tileHasItemId(sTile, ids.e) and tileHasItemId(eTile, ids.s) then
            count = count + 1
            if hits then hits[#hits + 1] = { tile = tile, id = ids.dse } end
        end
    end

    return count
end

-- Processa um iterador de tiles.
local function processTiles(tiles_iter, ids, preview)
    local matched, applied = 0, 0
    local hits = preview and nil or {}

    for tile in tiles_iter do
        matched = matched + processTile(tile, ids, hits)
    end

    if not preview and hits and #hits > 0 then
        app.transaction("Fill missing corners", function()
            for _, h in ipairs(hits) do
                if addCorner(h.tile, h.id) then
                    applied = applied + 1
                end
            end
        end)
    end

    return matched, applied
end

-- Iteradores ------------------------------------------------------------------

local function selectionIter()
    local sel = app.selection
    if not sel or sel.isEmpty then
        return function() return nil end
    end
    local tiles = sel.tiles
    local i = 0
    return function()
        i = i + 1
        return tiles[i]
    end
end

local function rectIter(x1, y1, z1, x2, y2, z2)
    x1, x2 = normalizeRange(x1, x2)
    y1, y2 = normalizeRange(y1, y2)
    z1, z2 = normalizeRange(z1, z2)

    local x, y, z = x1, y1, z1
    local done = false
    return function()
        if done then return nil end
        while true do
            local tile = map:getTile(x, y, z)
            x = x + 1
            if x > x2 then
                x = x1
                y = y + 1
                if y > y2 then
                    y = y1
                    z = z + 1
                    if z > z2 then
                        done = true
                    end
                end
            end
            if tile then return tile end
            if done then return nil end
        end
    end
end

-- ---------------------------------------------------------------------------
-- Defaults (baseados em seleção / câmera)
-- ---------------------------------------------------------------------------

local defaults = {
    from_x = 0, from_y = 0, from_z = 7,
    to_x = 0,   to_y = 0,   to_z = 7,
    -- IDs do exemplo do usuario:
    n = 18184, e = 18186, s = 18185, w = 18187,
    cnw = 18190, cne = 18191, csw = 18189, cse = 18188,
    dnw = 18194, dne = 18195, dsw = 18193, dse = 18192,
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
            defaults.from_x = (cam.x or 1000) - 10
            defaults.from_y = (cam.y or 1000) - 10
            defaults.from_z = cam.z or 7
            defaults.to_x   = (cam.x or 1000) + 10
            defaults.to_y   = (cam.y or 1000) + 10
            defaults.to_z   = cam.z or 7
        end
    end
end

local selection_has_tiles = (app.selection and not app.selection.isEmpty)

-- ---------------------------------------------------------------------------
-- Dialog
-- ---------------------------------------------------------------------------

local dlg = Dialog {
    title = "Fill Missing Corners & Diagonals",
    width = 540,
    height = 700,
    resizable = false,
}

dlg:label {
    text = "Preenche CORNER (concavo) e DIAGONAL (convexo) faltantes baseado nas bordas cardeais ja colocadas."
}
dlg:newrow()
dlg:label {
    text = "CORNER: detectado no proprio tile (N+W=>CNW, etc). DIAGONAL: detectado pelos vizinhos."
}
dlg:newrow()

-- Mode selector
dlg:box { orient = "vertical", label = "Area Alvo", padding = 8, margin = 4, expand = false }
    dlg:radio {
        id = "mode_selection",
        text = selection_has_tiles
            and string.format("Usar selecao atual (%d tiles)", app.selection.size)
            or  "Usar selecao atual (vazia - selecione primeiro)",
        selected = selection_has_tiles
    }
    dlg:newrow()
    dlg:radio {
        id = "mode_rect",
        text = "Usar regiao retangular (from pos -> to pos)",
        selected = not selection_has_tiles
    }
    dlg:newrow()

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

-- Bordas cardeais (detecao)
dlg:box { orient = "vertical", label = "IDs das Bordas Cardeais (ja presentes)", padding = 8, margin = 4, expand = false }
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:number { id = "n_id", label = "N:", value = defaults.n, min = 0, max = 65535 }
        dlg:number { id = "e_id", label = "E:", value = defaults.e, min = 0, max = 65535 }
        dlg:number { id = "s_id", label = "S:", value = defaults.s, min = 0, max = 65535 }
        dlg:number { id = "w_id", label = "W:", value = defaults.w, min = 0, max = 65535 }
    dlg:endbox()
dlg:endbox()

-- Corners internos (a adicionar)
dlg:box { orient = "vertical", label = "IDs dos CORNER items - concavo (serao adicionados)", padding = 8, margin = 4, expand = false }
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:number { id = "cnw_id", label = "CNW:", value = defaults.cnw, min = 0, max = 65535 }
        dlg:number { id = "cne_id", label = "CNE:", value = defaults.cne, min = 0, max = 65535 }
    dlg:endbox()
    dlg:newrow()
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:number { id = "csw_id", label = "CSW:", value = defaults.csw, min = 0, max = 65535 }
        dlg:number { id = "cse_id", label = "CSE:", value = defaults.cse, min = 0, max = 65535 }
    dlg:endbox()
dlg:endbox()

-- Diagonais externos (a adicionar)
dlg:box { orient = "vertical", label = "IDs dos DIAGONAL items - convexo (serao adicionados)", padding = 8, margin = 4, expand = false }
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:number { id = "dnw_id", label = "DNW:", value = defaults.dnw, min = 0, max = 65535 }
        dlg:number { id = "dne_id", label = "DNE:", value = defaults.dne, min = 0, max = 65535 }
    dlg:endbox()
    dlg:newrow()
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:number { id = "dsw_id", label = "DSW:", value = defaults.dsw, min = 0, max = 65535 }
        dlg:number { id = "dse_id", label = "DSE:", value = defaults.dse, min = 0, max = 65535 }
    dlg:endbox()
    dlg:newrow()
    dlg:label { text = "Dica: deixe um ID em 0 para desabilita-lo." }
dlg:endbox()

-- Status
dlg:separator()
dlg:label { id = "lbl_status", text = "Pronto." }
dlg:newrow()

-- Acao
local function runOperation(d, preview)
    local data = d.data

    local ids = {
        n   = clampInt(data.n_id,   0, 65535),
        e   = clampInt(data.e_id,   0, 65535),
        s   = clampInt(data.s_id,   0, 65535),
        w   = clampInt(data.w_id,   0, 65535),
        cnw = clampInt(data.cnw_id, 0, 65535),
        cne = clampInt(data.cne_id, 0, 65535),
        csw = clampInt(data.csw_id, 0, 65535),
        cse = clampInt(data.cse_id, 0, 65535),
        dnw = clampInt(data.dnw_id, 0, 65535),
        dne = clampInt(data.dne_id, 0, 65535),
        dsw = clampInt(data.dsw_id, 0, 65535),
        dse = clampInt(data.dse_id, 0, 65535),
    }

    if ids.n == 0 and ids.e == 0 and ids.s == 0 and ids.w == 0 then
        app.alert("Informe pelo menos um ID de borda cardeal (N/E/S/W).")
        return
    end

    local use_selection = data.mode_selection and true or false
    local iter
    if use_selection then
        if not app.selection or app.selection.isEmpty then
            app.alert("Selecao vazia. Selecione uma area ou use modo retangular.")
            return
        end
        iter = selectionIter()
    else
        local x1 = clampInt(data.from_x, 0, 65535)
        local y1 = clampInt(data.from_y, 0, 65535)
        local z1 = clampInt(data.from_z, 0, 15)
        local x2 = clampInt(data.to_x,   0, 65535)
        local y2 = clampInt(data.to_y,   0, 65535)
        local z2 = clampInt(data.to_z,   0, 15)
        iter = rectIter(x1, y1, z1, x2, y2, z2)
    end

    local matched, applied = processTiles(iter, ids, preview)

    local status
    if preview then
        status = string.format("Preview: %d item(s) seriam adicionados.", matched)
    else
        status = string.format("Adicionados %d / %d item(s).", applied, matched)
        if app.refresh then app.refresh() end
    end

    d:modify { lbl_status = { text = status } }
    print(status)
end

dlg:box { orient = "horizontal", padding = 4, margin = 4, expand = false }
    dlg:button {
        text = "Preview",
        onclick = function(d) runOperation(d, true) end
    }
    dlg:button {
        text = "Aplicar",
        onclick = function(d) runOperation(d, false) end
    }
    dlg:button {
        text = "Fechar",
        onclick = function(d) d:close() end
    }
dlg:endbox()

dlg:show { wait = false }

print("Fill Missing Corners & Diagonals: dialog aberto.")
