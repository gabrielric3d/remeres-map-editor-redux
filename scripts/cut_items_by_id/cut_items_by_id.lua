-- Cut Items by ID
--
-- Filtra a seleção atual do mapa deixando apenas os itens cujo ID esteja na
-- lista/faixa configurada e recorta esses itens para o clipboard (Ctrl+V).
--
-- Fluxo de uso:
--   1. Selecione a área no mapa (ferramenta de seleção).
--   2. Rode o script (Ctrl+Shift+X ou pelo Lua Scripts window).
--   3. Clique em "Cut" -> os itens 56515-56518 (default) saem do mapa e ficam
--      no clipboard, prontos para colar com Ctrl+V.
--
-- Formato aceito no campo de IDs: "56515-56518", "100, 200", "56515-56518, 1234"

if not app then
    print("Error: RME Lua API not found.")
    return
end

if not app.hasMap() or not app.map then
    app.alert("Nenhum mapa aberto.")
    return
end

local DEFAULT_IDS = "56515-56518"

-- ---------------------------------------------------------------------------
-- Parsing de IDs
-- ---------------------------------------------------------------------------

-- Aceita "a-b" (faixa) e "a" (avulso), separados por vírgula/espaço/;
-- Retorna { {from = n, to = n}, ... } ou nil + mensagem de erro.
local function parseIdSpec(spec)
    spec = tostring(spec or ""):gsub("%s+", "")
    if spec == "" then
        return nil, "Informe pelo menos um ID."
    end

    local ranges = {}
    for chunk in spec:gmatch("[^,;]+") do
        local a, b = chunk:match("^(%d+)%-(%d+)$")
        if a then
            a, b = tonumber(a), tonumber(b)
            if a > b then a, b = b, a end
            ranges[#ranges + 1] = { from = a, to = b }
        else
            local single = chunk:match("^(%d+)$")
            if not single then
                return nil, string.format("Trecho invalido: '%s'", chunk)
            end
            single = tonumber(single)
            ranges[#ranges + 1] = { from = single, to = single }
        end
    end

    if #ranges == 0 then
        return nil, "Informe pelo menos um ID."
    end
    return ranges
end

local function matchesId(ranges, id)
    for i = 1, #ranges do
        local r = ranges[i]
        if id >= r.from and id <= r.to then
            return true
        end
    end
    return false
end

-- ---------------------------------------------------------------------------
-- Coleta: quais itens da seleção atual batem com os IDs
-- ---------------------------------------------------------------------------

-- Retorna uma lista { {tile = t, item = it, isGround = bool}, ... } e a
-- contagem de tiles atingidos.
local function collectMatches(ranges, include_ground)
    local sel = app.selection
    local hits = {}
    local tiles_touched = 0

    if not sel or sel.isEmpty then
        return hits, 0
    end

    -- Snapshot dos tiles: vamos mexer na seleção depois, entao nao da pra
    -- iterar sobre ela enquanto modifica.
    local tiles = sel.tiles
    for i = 1, #tiles do
        local tile = tiles[i]
        if tile then
            local before = #hits

            local items = tile.items
            if items then
                for j = 1, #items do
                    local it = items[j]
                    if it and matchesId(ranges, it.id) then
                        hits[#hits + 1] = { tile = tile, item = it, isGround = false }
                    end
                end
            end

            if include_ground then
                local ground = tile.ground
                if ground and matchesId(ranges, ground.id) then
                    hits[#hits + 1] = { tile = tile, item = ground, isGround = true }
                end
            end

            if #hits > before then
                tiles_touched = tiles_touched + 1
            end
        end
    end

    return hits, tiles_touched
end

-- ---------------------------------------------------------------------------
-- Reescreve a seleção para conter apenas os itens que casaram
-- ---------------------------------------------------------------------------

local function reselectOnly(hits)
    local sel = app.selection
    if not sel then
        return false
    end

    -- clear() + add(tile, item) dentro de uma unica sessao interna: nao gera
    -- entrada de undo para a mudanca de selecao em si (o cut gera a sua).
    sel:transaction(function()
        sel:clear()
        for i = 1, #hits do
            local h = hits[i]
            if h.tile and h.item then
                sel:add(h.tile, h.item)
            end
        end
    end)

    return not sel.isEmpty
end

-- ---------------------------------------------------------------------------
-- Operação principal
-- ---------------------------------------------------------------------------

local function runOperation(d, preview)
    local data = d.data

    local ranges, err = parseIdSpec(data.ids)
    if not ranges then
        d:modify { lbl_status = { text = "Erro: " .. err } }
        return
    end

    local include_ground = data.include_ground and true or false

    local sel = app.selection
    if not sel or sel.isEmpty then
        d:modify { lbl_status = { text = "Selecao vazia. Selecione a area no mapa primeiro." } }
        return
    end

    local selected_tiles = sel.size
    local hits, tiles_touched = collectMatches(ranges, include_ground)

    if #hits == 0 then
        local status = string.format(
            "Nenhum item encontrado nos IDs informados (%d tile(s) na selecao).",
            selected_tiles
        )
        d:modify { lbl_status = { text = status } }
        print(status)
        return
    end

    if preview then
        local status = string.format(
            "Preview: %d item(s) em %d tile(s) seriam recortados.",
            #hits, tiles_touched
        )
        d:modify { lbl_status = { text = status } }
        print(status)
        return
    end

    if not reselectOnly(hits) then
        local status = "Falha ao refazer a selecao com os itens filtrados."
        d:modify { lbl_status = { text = status } }
        print(status)
        return
    end

    -- app.cut() recorta os itens selecionados (item-level) para o copybuffer,
    -- que é exatamente o buffer usado pelo Ctrl+V. Só funciona em selection
    -- mode; se estiver em drawing mode nada é copiado.
    app.cut()

    if not app.canPaste() then
        local status = "Cut falhou: mude para o modo de selecao (Selection Mode) e tente de novo. "
            .. "A selecao ja esta filtrada nos itens alvo."
        d:modify { lbl_status = { text = status } }
        print(status)
        return
    end

    if app.refresh then app.refresh() end

    local status = string.format(
        "Recortados %d item(s) de %d tile(s). Cole com Ctrl+V.",
        #hits, tiles_touched
    )
    d:modify { lbl_status = { text = status } }
    print(status)
end

-- Só copia (não remove do mapa), também filtrando por ID.
local function runCopyOnly(d)
    local data = d.data

    local ranges, err = parseIdSpec(data.ids)
    if not ranges then
        d:modify { lbl_status = { text = "Erro: " .. err } }
        return
    end

    local sel = app.selection
    if not sel or sel.isEmpty then
        d:modify { lbl_status = { text = "Selecao vazia. Selecione a area no mapa primeiro." } }
        return
    end

    local hits, tiles_touched = collectMatches(ranges, data.include_ground and true or false)
    if #hits == 0 then
        d:modify { lbl_status = { text = "Nenhum item encontrado nos IDs informados." } }
        return
    end

    if not reselectOnly(hits) then
        d:modify { lbl_status = { text = "Falha ao refazer a selecao com os itens filtrados." } }
        return
    end

    -- app.copy() escreve direto no copybuffer, entao funciona mesmo fora do
    -- selection mode (ao contrario do cut).
    app.copy()

    local status = string.format(
        "Copiados %d item(s) de %d tile(s) (mapa intacto). Cole com Ctrl+V.",
        #hits, tiles_touched
    )
    d:modify { lbl_status = { text = status } }
    print(status)
end

-- ---------------------------------------------------------------------------
-- Dialog
-- ---------------------------------------------------------------------------

local sel_now = app.selection
local sel_info = (sel_now and not sel_now.isEmpty)
    and string.format("Selecao atual: %d tile(s).", sel_now.size)
    or  "Selecao atual: vazia."

local dlg = Dialog {
    title = "Cut Items by ID",
    width = 460,
    height = 300,
    resizable = false,
}

dlg:label { text = "Filtra a selecao pelos IDs abaixo e recorta para o clipboard (Ctrl+V)." }
dlg:newrow()
dlg:label { id = "lbl_sel", text = sel_info }
dlg:newrow()

dlg:box { orient = "vertical", label = "Item IDs", padding = 8, margin = 4, expand = false }
    dlg:input { id = "ids", label = "IDs:", text = DEFAULT_IDS }
    dlg:newrow()
    dlg:label { text = "Formato: 56515-56518, 1234 (faixas e IDs avulsos)" }
dlg:endbox()

dlg:box { orient = "vertical", label = "Opcoes", padding = 8, margin = 4, expand = false }
    dlg:check {
        id = "include_ground",
        text = "Incluir ground (CUIDADO: pode deixar tiles sem chao)",
        selected = false
    }
dlg:endbox()

dlg:separator()
dlg:label { id = "lbl_status", text = "Pronto." }
dlg:newrow()

dlg:box { orient = "horizontal", padding = 4, margin = 4, expand = false }
    dlg:button {
        text = "Preview",
        onclick = function(d) runOperation(d, true) end
    }
    dlg:button {
        text = "Copy",
        onclick = function(d) runCopyOnly(d) end
    }
    dlg:button {
        text = "Cut",
        onclick = function(d) runOperation(d, false) end
    }
    dlg:button {
        text = "Fechar",
        onclick = function(d) d:close() end
    }
dlg:endbox()

dlg:show { wait = false }

print("Cut Items by ID: dialogo aberto.")
