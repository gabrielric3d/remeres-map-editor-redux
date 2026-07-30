-- Action ID Clusters
-- Escaneia uma area retangular (from_pos -> to_pos) procurando tiles que
-- possuem algum item (ground ou nao-ground) com action id. Em seguida agrupa
-- os tiles cujos action ids estao "perto" uns dos outros (distancia de
-- Chebyshev <= raio, mesmo andar). Tiles isolados (sem nenhum outro action id
-- dentro do raio) sao descartados por padrao -- ou seja, so registra um grupo
-- quando ha pelo menos 2 posicoes proximas. O resultado e uma tabela Lua no
-- formato:
--
--   STATUES = {
--       [1] = {
--           Position(5491, 4107, 4),
--           Position(5492, 4107, 4),
--       },
--       [2] = {
--           Position(5571, 4141, 4),
--           Position(5572, 4141, 4),
--       },
--   }
--
-- A tabela e impressa no console e copiada para o clipboard. Cada grupo na
-- lista pode ser clicado para teleportar a camera ate ele.
--
-- A area pode ser preenchida de 3 formas:
--   1. Digitando manualmente os campos From/To.
--   2. Colando uma posicao nos campos "Colar From/To" (deteccao automatica).
--   3. Map Picker: desenhe uma selecao no mapa e clique "Pegar Selecao", ou
--      marque "Seguir selecao" para preencher From/To em tempo real.

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

local function posKey(x, y, z)
    return x .. "," .. y .. "," .. z
end

-- Extrai numeros de uma string (ate 6). Aceita varios formatos:
--   "1267, 46, 7"  /  "1267,46,7"  /  "1267 46 7"
--   "x:1267 y:46 z:7"  /  "{x=1267, y=46, z=7}"  /  "Position(1267, 46, 7)"
-- Retorna a lista de numeros encontrados (pode ter ate 6: from + to).
local function parseNumbers(text)
    if not text or text == "" then return {} end
    local nums = {}
    for n in string.gmatch(text, "%-?%d+") do
        nums[#nums + 1] = tonumber(n)
        if #nums == 6 then break end
    end
    return nums
end

local function formatPosition(x, y, z)
    return string.format("%d, %d, %d", x, y, z)
end

-- Coleta os action ids (> 0) presentes no tile (ground + itens). Retorna a
-- lista de action ids encontrados (pode ter repetidos/varios). Vazia => o tile
-- nao tem nenhum action id.
local function collectTileActionIds(tile)
    local aids = {}

    local function consider(it)
        if not it then return end
        local aid = it.actionId
        if aid and aid > 0 then
            aids[#aids + 1] = aid
        end
    end

    if tile.hasGround and tile.ground then
        consider(tile.ground)
    end

    local items = tile.items
    if items then
        for i = 1, #items do
            consider(items[i])
        end
    end

    return aids
end

-- Faz o scan da area e retorna a lista de "action tiles":
--   { { x=, y=, z=, aid= (action id representativo = o menor encontrado),
--       aids= { todos os aids do tile } }, ... }
-- target_aid: se > 0, so considera tiles que tenham esse action id especifico
--             (e o aid representativo passa a ser esse). Se 0, considera
--             qualquer action id.
local function scanArea(x1, y1, z1, x2, y2, z2, target_aid)
    x1, x2 = normalizeRange(x1, x2)
    y1, y2 = normalizeRange(y1, y2)
    z1, z2 = normalizeRange(z1, z2)

    local tiles = {}
    local tiles_scanned = 0

    for z = z1, z2 do
        for y = y1, y2 do
            for x = x1, x2 do
                local tile = map:getTile(x, y, z)
                if tile then
                    tiles_scanned = tiles_scanned + 1
                    local aids = collectTileActionIds(tile)
                    if #aids > 0 then
                        if target_aid > 0 then
                            -- Filtro: so entra se possuir o action id pedido.
                            local found = false
                            for _, a in ipairs(aids) do
                                if a == target_aid then found = true break end
                            end
                            if found then
                                tiles[#tiles + 1] = {
                                    x = x, y = y, z = z,
                                    aid = target_aid, aids = aids,
                                }
                            end
                        else
                            -- Qualquer action id: aid representativo = o menor.
                            local minAid = aids[1]
                            for _, a in ipairs(aids) do
                                if a < minAid then minAid = a end
                            end
                            tiles[#tiles + 1] = {
                                x = x, y = y, z = z,
                                aid = minAid, aids = aids,
                            }
                        end
                    end
                end
            end
        end
    end

    return tiles, tiles_scanned
end

-- Union-Find (disjoint set) sobre os indices das action tiles.
local function makeUnionFind(n)
    local parent = {}
    for i = 1, n do parent[i] = i end

    local function find(i)
        while parent[i] ~= i do
            parent[i] = parent[parent[i]]
            i = parent[i]
        end
        return i
    end

    local function union(a, b)
        local ra, rb = find(a), find(b)
        if ra ~= rb then parent[ra] = rb end
    end

    return find, union
end

-- Agrupa as action tiles. Dois tiles ficam no mesmo grupo se estiverem no mesmo
-- andar e a distancia de Chebyshev (max(|dx|, |dy|)) for <= radius. O
-- agrupamento e transitivo (uma fileira de statues espacadas <= radius vira um
-- unico grupo). Se same_aid for true, so conecta tiles que compartilham o mesmo
-- action id representativo.
-- Retorna a lista de grupos; cada grupo e uma lista de tiles {x,y,z,aid}.
local function clusterTiles(tiles, radius, same_aid)
    local n = #tiles
    if n == 0 then return {} end

    -- Indice espacial: posKey -> lista de indices de tiles naquela posicao.
    local byPos = {}
    for i = 1, n do
        local t = tiles[i]
        local key = posKey(t.x, t.y, t.z)
        local bucket = byPos[key]
        if not bucket then bucket = {}; byPos[key] = bucket end
        bucket[#bucket + 1] = i
    end

    local find, union = makeUnionFind(n)

    for i = 1, n do
        local t = tiles[i]
        for dy = -radius, radius do
            for dx = -radius, radius do
                if not (dx == 0 and dy == 0) then
                    local bucket = byPos[posKey(t.x + dx, t.y + dy, t.z)]
                    if bucket then
                        for _, j in ipairs(bucket) do
                            if (not same_aid) or tiles[j].aid == t.aid then
                                union(i, j)
                            end
                        end
                    end
                end
            end
        end
    end

    -- Agrupa por raiz.
    local groupsByRoot = {}
    for i = 1, n do
        local r = find(i)
        local g = groupsByRoot[r]
        if not g then g = {}; groupsByRoot[r] = g end
        g[#g + 1] = tiles[i]
    end

    local groups = {}
    for _, g in pairs(groupsByRoot) do
        groups[#groups + 1] = g
    end
    return groups
end

-- Ordena as posicoes de um grupo (z, depois y, depois x) -- mesma ordem do
-- exemplo de referencia.
local function sortGroupPositions(g)
    table.sort(g, function(a, b)
        if a.z ~= b.z then return a.z < b.z end
        if a.y ~= b.y then return a.y < b.y end
        return a.x < b.x
    end)
end

-- Ordena os grupos pelo canto superior-esquerdo (z, y, x do primeiro tile) para
-- saida deterministica.
local function sortGroups(groups)
    for _, g in ipairs(groups) do
        sortGroupPositions(g)
    end
    table.sort(groups, function(a, b)
        local pa, pb = a[1], b[1]
        if pa.z ~= pb.z then return pa.z < pb.z end
        if pa.y ~= pb.y then return pa.y < pb.y end
        return pa.x < pb.x
    end)
end

-- Gera o texto da tabela Lua no formato STATUES.
local function buildTableText(table_name, groups)
    local lines = {}
    lines[#lines + 1] = table_name .. " = {"
    for i, g in ipairs(groups) do
        lines[#lines + 1] = string.format("    [%d] = {", i)
        for _, p in ipairs(g) do
            lines[#lines + 1] = string.format(
                "        Position(%d, %d, %d),", p.x, p.y, p.z
            )
        end
        lines[#lines + 1] = "    },"
    end
    lines[#lines + 1] = "}"
    return table.concat(lines, "\n")
end

-- Texto de uma linha da lista de previa de grupos.
local function formatGroupLine(index, g)
    local p = g[1]
    -- Coleta os aids distintos do grupo para exibir.
    local seen, aidList = {}, {}
    for _, t in ipairs(g) do
        if not seen[t.aid] then
            seen[t.aid] = true
            aidList[#aidList + 1] = t.aid
        end
    end
    table.sort(aidList)
    local aidStr = table.concat(aidList, ",")
    return string.format(
        "Grupo %d: %d posicao(oes)  aid:%s  inicio em (%d, %d, %d)",
        index, #g, aidStr, p.x, p.y, p.z
    )
end

-- ---------------------------------------------------------------------------
-- Defaults (vem da selecao atual ou da camera)
-- ---------------------------------------------------------------------------

local defaults = {
    action_id = 0,        -- 0 = qualquer action id
    radius = 4,
    min_size = 2,
    same_aid = false,
    table_name = "STATUES",
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

-- Estado compartilhado entre callbacks.
local state = {
    groups = {},
    table_text = "",
    table_name = defaults.table_name,
    followSelection = false,
}

-- Lifecycle do Map Picker (precisam existir antes do Dialog{} por causa do
-- onclose). applySelectionBounds e definido depois que o dialog existe.
local pickerListenerId = nil
local dialogAlive = true
local applySelectionBounds

-- ---------------------------------------------------------------------------
-- Dialog
-- ---------------------------------------------------------------------------

local dlg = Dialog {
    title = "Action ID Clusters",
    width = 580,
    height = 620,
    resizable = true,
    onclose = function()
        dialogAlive = false
        if pickerListenerId and app.events then
            app.events:off(pickerListenerId)
            pickerListenerId = nil
        end
    end,
}

dlg:label {
    text = "Agrupa tiles com action id proximos (raio) e gera uma tabela de Positions.",
}
dlg:newrow()

-- Parametros principais
dlg:box { orient = "vertical", label = "Parametros", padding = 8, margin = 4, expand = false }
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:number { id = "action_id", label = "Action ID (0 = qualquer):", value = defaults.action_id, min = 0, max = 65535 }
        dlg:number { id = "radius", label = "Raio:", value = defaults.radius, min = 1, max = 50 }
    dlg:endbox()
    dlg:newrow()
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:number { id = "min_size", label = "Tamanho minimo do grupo:", value = defaults.min_size, min = 1, max = 9999 }
        dlg:input { id = "table_name", label = "Nome da tabela:", text = defaults.table_name }
    dlg:endbox()
    dlg:newrow()
    dlg:check {
        id = "same_aid",
        text = "Agrupar apenas action ids iguais (nao misturar aids diferentes no mesmo grupo)",
        selected = defaults.same_aid
    }
dlg:endbox()

-- Map Picker / Colar posicao
dlg:box { orient = "vertical", label = "Map Picker / Colar Posicao", padding = 8, margin = 4, expand = false }
    dlg:check {
        id = "follow_sel",
        text = "Seguir selecao do mapa (desenhe uma area no mapa para preencher From/To)",
        selected = false,
        onchange = function(d)
            state.followSelection = d.data.follow_sel and true or false
            if state.followSelection then
                applySelectionBounds(false)
            end
        end
    }
    dlg:newrow()
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:button {
            text = "Pegar Selecao -> Area",
            onclick = function() applySelectionBounds(true) end
        }
        dlg:button {
            text = "Copiar From",
            onclick = function(d)
                if not app.setClipboard then return end
                app.setClipboard(formatPosition(
                    clampInt(d.data.from_x, 0, 65535),
                    clampInt(d.data.from_y, 0, 65535),
                    clampInt(d.data.from_z, 0, 15)))
            end
        }
        dlg:button {
            text = "Copiar To",
            onclick = function(d)
                if not app.setClipboard then return end
                app.setClipboard(formatPosition(
                    clampInt(d.data.to_x, 0, 65535),
                    clampInt(d.data.to_y, 0, 65535),
                    clampInt(d.data.to_z, 0, 15)))
            end
        }
    dlg:endbox()
    dlg:newrow()
    -- Colar com deteccao automatica: ao colar/digitar uma posicao, os campos
    -- From (ou To) sao preenchidos sozinhos. Se colar 6 numeros no campo From,
    -- ele preenche From e To de uma vez.
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:input {
            id = "paste_from",
            label = "Colar From:",
            text = "",
            onchange = function(d)
                local nums = parseNumbers(d.data.paste_from)
                if #nums < 3 then return end
                local mods = {
                    from_x = { value = clampInt(nums[1], 0, 65535) },
                    from_y = { value = clampInt(nums[2], 0, 65535) },
                    from_z = { value = clampInt(nums[3], 0, 15) },
                }
                if #nums >= 6 then
                    mods.to_x = { value = clampInt(nums[4], 0, 65535) }
                    mods.to_y = { value = clampInt(nums[5], 0, 65535) }
                    mods.to_z = { value = clampInt(nums[6], 0, 15) }
                end
                mods.lbl_status = { text = "Posicao detectada e aplicada em From." ..
                    ((#nums >= 6) and " (To tambem)" or "") }
                d:modify(mods)
            end
        }
    dlg:endbox()
    dlg:newrow()
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:input {
            id = "paste_to",
            label = "Colar To:",
            text = "",
            onchange = function(d)
                local nums = parseNumbers(d.data.paste_to)
                if #nums < 3 then return end
                d:modify {
                    to_x = { value = clampInt(nums[1], 0, 65535) },
                    to_y = { value = clampInt(nums[2], 0, 65535) },
                    to_z = { value = clampInt(nums[3], 0, 15) },
                    lbl_status = { text = "Posicao detectada e aplicada em To." },
                }
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
dlg:label { id = "lbl_status", text = "Pressione 'Gerar' para escanear a area e montar a tabela." }
dlg:newrow()

dlg:label { text = "Grupos (selecione uma linha para teleportar):" }
dlg:newrow()
dlg:list {
    id = "groups_list",
    height = 200,
    expand = true,
    items = { { text = "(nenhum grupo ainda)", tooltip = "" } },
    onchange = function(d)
        local idx = d.data.groups_list
        if not idx or idx <= 0 then return end
        local g = state.groups[idx]
        if not g or not g[1] then return end
        if app.setCameraPosition then
            local p = g[1]
            app.setCameraPosition(p.x, p.y, p.z)
            d:modify { lbl_status = { text = string.format(
                "Teleportado para o grupo %d em (%d, %d, %d). %d grupo(s) no total.",
                idx, p.x, p.y, p.z, #state.groups
            ) } }
        end
    end,
}
dlg:newrow()

-- ---------------------------------------------------------------------------
-- Map Picker: aplica os limites da selecao atual nos campos From/To
-- ---------------------------------------------------------------------------

applySelectionBounds = function(announce)
    if not dialogAlive then return false end
    local sel = app.selection
    if not sel or sel.isEmpty or not sel.bounds then
        if announce then app.alert("Nenhuma selecao ativa no mapa. Selecione uma area primeiro.") end
        return false
    end
    local b = sel.bounds
    if not b.min or not b.max then
        if announce then app.alert("Nao foi possivel ler os limites da selecao.") end
        return false
    end
    dlg:modify {
        from_x = { value = b.min.x }, from_y = { value = b.min.y }, from_z = { value = b.min.z },
        to_x = { value = b.max.x },   to_y = { value = b.max.y },   to_z = { value = b.max.z },
        lbl_status = { text = string.format(
            "Picker: area = (%d, %d, %d) -> (%d, %d, %d).",
            b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z
        ) },
    }
    return true
end

-- Listener: quando a selecao do mapa muda e "Seguir selecao" esta ligado,
-- atualiza From/To automaticamente. Removido no onclose do dialog.
if app.events then
    pickerListenerId = app.events:on("selectionChange", function()
        if not dialogAlive then return end
        if not state.followSelection then return end
        applySelectionBounds(false)
    end)
end

-- ---------------------------------------------------------------------------
-- Acoes
-- ---------------------------------------------------------------------------

local function runGenerate(d)
    local data = d.data

    local target_aid = clampInt(data.action_id, 0, 65535)
    local radius = clampInt(data.radius, 1, 50)
    local min_size = clampInt(data.min_size, 1, 9999)
    local same_aid = data.same_aid and true or false
    local table_name = (data.table_name and data.table_name ~= "")
        and tostring(data.table_name) or "STATUES"

    local x1 = clampInt(data.from_x, 0, 65535)
    local y1 = clampInt(data.from_y, 0, 65535)
    local z1 = clampInt(data.from_z, 0, 15)
    local x2 = clampInt(data.to_x,   0, 65535)
    local y2 = clampInt(data.to_y,   0, 65535)
    local z2 = clampInt(data.to_z,   0, 15)

    local tiles, scanned = scanArea(x1, y1, z1, x2, y2, z2, target_aid)
    local allGroups = clusterTiles(tiles, radius, same_aid)

    -- Descarta grupos menores que o tamanho minimo ("se possuir [vizinho] perto").
    local groups = {}
    for _, g in ipairs(allGroups) do
        if #g >= min_size then groups[#groups + 1] = g end
    end
    sortGroups(groups)

    state.groups = groups
    state.table_name = table_name
    state.table_text = buildTableText(table_name, groups)

    -- Lista de previa.
    local list_items = {}
    if #groups == 0 then
        list_items[#list_items + 1] = {
            text = "Nenhum grupo encontrado com os parametros atuais.",
            tooltip = ""
        }
    else
        for i, g in ipairs(groups) do
            list_items[#list_items + 1] = {
                text = formatGroupLine(i, g),
                tooltip = "",
            }
        end
    end

    -- Copia automaticamente a tabela para o clipboard.
    local copied = ""
    if #groups > 0 and app.setClipboard then
        app.setClipboard(state.table_text)
        copied = " (tabela copiada para o clipboard)"
    end

    local aid_note = (target_aid > 0)
        and string.format(" [action id %d]", target_aid) or " [qualquer action id]"
    d:modify {
        groups_list = { items = list_items, selection = 0 },
        lbl_status = { text = string.format(
            "%d grupo(s) com >= %d tile(s), raio %d, em %d tile(s) com action id de %d escaneado(s).%s%s",
            #groups, min_size, radius, #tiles, scanned, aid_note, copied
        ) }
    }

    print(string.format(
        "Action ID Clusters: %d grupo(s)%s. %d tile(s) com action id em %d escaneado(s).",
        #groups, aid_note, #tiles, scanned
    ))
    if #groups > 0 then
        print(state.table_text)
    end
end

local function copyTable(d)
    if state.table_text == "" or #state.groups == 0 then
        app.alert("Nada para copiar. Clique em 'Gerar' primeiro.")
        return
    end
    if not app.setClipboard then
        app.alert("API de clipboard indisponivel nesta versao.")
        return
    end
    app.setClipboard(state.table_text)
    d:modify { lbl_status = { text = string.format(
        "Tabela '%s' com %d grupo(s) copiada para o clipboard.",
        state.table_name, #state.groups
    ) } }
    print(string.format(
        "Action ID Clusters: tabela '%s' (%d grupo(s)) copiada para o clipboard.",
        state.table_name, #state.groups
    ))
end

local function teleportSelected(d)
    local idx = d.data.groups_list
    if not idx or idx <= 0 or not state.groups[idx] then
        app.alert("Selecione um grupo na lista primeiro.")
        return
    end
    local p = state.groups[idx][1]
    if p and app.setCameraPosition then
        app.setCameraPosition(p.x, p.y, p.z)
        d:modify { lbl_status = { text = string.format(
            "Teleportado para o grupo %d em (%d, %d, %d).", idx, p.x, p.y, p.z
        ) } }
    end
end

dlg:box { orient = "horizontal", padding = 4, margin = 4, expand = false }
    dlg:button {
        text = "Gerar",
        onclick = function(d) runGenerate(d) end
    }
    dlg:button {
        text = "Copiar Tabela",
        onclick = function(d) copyTable(d) end
    }
    dlg:button {
        text = "Teleportar Selecionado",
        onclick = function(d) teleportSelected(d) end
    }
    dlg:button {
        text = "Fechar",
        onclick = function(d) d:close() end
    }
dlg:endbox()

dlg:show { wait = false }

print("Action ID Clusters: dialog aberto.")
