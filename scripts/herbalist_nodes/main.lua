-- Nos de Herbalist do mundo (BlackTalon, plano 179): desenha no mapa cada no (arvore de fruta ou
-- planta) gerado por gen_herbalist_nodes.js, deixa por/mover/apagar o no do tile SELECIONADO,
-- trocar a especie, travar (locked) e desligar (enabled), conferir e salvar.
--
-- A fonte e o server: blacktalon-workspace\server\data\scripts\mining\herbalist_nodes.json, lido por
-- mining\4_herbalist_world_nodes.lua. O nodes.json desta pasta e um HARD LINK para ele (o RME so
-- le/grava dentro da propria pasta de scripts), criado por .blacktalon/scripts/link_herbalist_nodes.cjs.
-- Se um git checkout/pull trocar o arquivo do server, o link quebra: rode o .cjs de novo.
--
-- Arvore (apple, banana, orange, lemon): o sprite e 3x3 e cresce para NOROESTE do tile do no; o
-- overlay mostra esse retangulo. locked = o gerador mantem o no ao rodar de novo; enabled = false
-- tira o no do jogo sem apagar do arquivo.
--
-- Vivos (plano 180): alem dos nos base, o overlay mostra os brotos/arvores vivos que o server espalha
-- (live.json = espelho que o server grava em runtime/herbalist_live_view.json) e, com um no base
-- selecionado e "Mostrar raio" ligado, o raio e os tiles do pool dele (pool.json = herbalist_pool.json).
-- Os dois sao HARD LINKs do server e so de LEITURA aqui (nunca save). Vivo cuja origem nao e um no
-- habilitado aparece em vermelho (orfao). "Recarregar vivos" rele os dois e aplica Instancia/raio.
--
-- Texto da tela so em ASCII (a camada Lua do editor quebra acento UTF-8).

if not app or not app.hasMap() or not app.map then
    print("[herbalist_nodes] no map open")
    if app then app.alert("Nenhum mapa aberto.") end
    return
end

local OVERLAY_ID = "bt_herbalist_nodes"
local store = app.storage("nodes.json")
local liveStore = app.storage("live.json")
local poolStore = app.storage("pool.json")

-- Rodar o script de novo executa o arquivo no mesmo estado Lua: o estado fica aqui.
BTHerbalistNodes = BTHerbalistNodes or {}
local S = BTHerbalistNodes

local TREES = { apple = true, banana = true, orange = true, lemon = true }
local DEFAULT_KEYS = { "apple", "banana", "orange", "lemon", "prickly_pear", "soul_mushroom", "frost_leaf", "azure_flower" }
local MIN_SPACING = 3

-- ---------------------------------------------------------------------------
-- Dados
-- ---------------------------------------------------------------------------

local function loadData()
    local data = store:load()
    if type(data) ~= "table" then return false end
    if type(data.species) ~= "table" then data.species = {} end
    if type(data.nodes) ~= "table" then data.nodes = {} end
    S.data = data
    S.dirty = false
    return true
end

if not loadData() then
    app.alert("Nao achei o nodes.json valido em\n" .. tostring(store.path) ..
        "\n\nRode: node .blacktalon/scripts/link_herbalist_nodes.cjs (no blacktalon-workspace).")
    return
end

local function nodes()
    return S.data.nodes
end

-- vivos e pool: so leitura; ausente/quebrado = vazio (o overlay dos nos base segue funcionando)
local function loadLive()
    local ok, data = pcall(function() return liveStore:load() end)
    S.live = (ok and type(data) == "table" and type(data.entries) == "table") and data.entries or {}
    local ok2, pool = pcall(function() return poolStore:load() end)
    if ok2 and type(pool) == "table" and type(pool.origins) == "table" then
        S.pool = pool
    else
        S.pool = { radius = 7, origins = {} }
    end
end
loadLive()
S.liveInstance = S.liveInstance or 0
if S.showRadius == nil then S.showRadius = true end

local function current()
    return nodes()[S.sel or 0]
end

local function isEnabled(n) return n.enabled ~= false end
local function isTree(n) return TREES[n.key] == true end

-- as especies que o combobox oferece: as oito do plano mais as que o arquivo trouxer
local function speciesIds()
    local seen, ids = {}, {}
    for _, k in ipairs(DEFAULT_KEYS) do seen[k] = true; ids[#ids + 1] = k end
    local extra = {}
    for k in pairs(S.data.species) do
        if not seen[k] then extra[#extra + 1] = k end
    end
    table.sort(extra)
    for _, k in ipairs(extra) do ids[#ids + 1] = k end
    return ids
end

local function nodeAt(x, y, z)
    for i, n in ipairs(nodes()) do
        if n.x == x and n.y == y and n.z == z then return i end
    end
    return nil
end

local function nodeTitle(i, n)
    return string.format("%-3d %-13s (%d,%d,%d)%s%s", i, tostring(n.key), n.x, n.y, n.z,
        n.locked and " L" or "", isEnabled(n) and "" or " OFF")
end

-- ---------------------------------------------------------------------------
-- Overlay
-- ---------------------------------------------------------------------------

-- "#rrggbb" -> {r, g, b}; aceita tambem {r, g, b}
local function colorOf(key)
    local sp = S.data.species[key]
    local c = sp and sp.color
    if type(c) == "string" then
        local r, g, b = c:match("^#?(%x%x)(%x%x)(%x%x)$")
        if r then return { tonumber(r, 16), tonumber(g, 16), tonumber(b, 16) } end
    elseif type(c) == "table" and c[1] then
        return { c[1], c[2] or 255, c[3] or 255 }
    end
    return { 220, 220, 220 }
end

local function rgba(key, a)
    local c = colorOf(key)
    return { c[1], c[2], c[3], a }
end

local function inView(v, x, y)
    return x >= v.x1 - 4 and x <= v.x2 + 4 and y >= v.y1 - 4 and y <= v.y2 + 4
end

-- ids de item de cada no, caso o JSON nao traga species[key].node
local NODE_ITEM = {
    apple = 58398, banana = 58400, orange = 58402, lemon = 58404,
    prickly_pear = 58406, soul_mushroom = 58408, frost_leaf = 58410, azure_flower = 58412,
}

local function itemIdOf(key)
    local sp = S.data.species[key]
    return (sp and tonumber(sp.node)) or NODE_ITEM[key]
end

-- Sem ctx.item (RME antigo, sem rebuild) cai no ctx.image, que cobre UM tile so; so guarda sprite valido
local spriteCache = {}
local function spriteFor(itemId)
    local img = spriteCache[itemId]
    if img == nil then
        local ok, s = pcall(Image.fromItemSprite, itemId)
        if ok and s and s.valid and s.isFromSprite then
            img = s
        else
            img = false
        end
        spriteCache[itemId] = img
    end
    return img
end

local function drawItem(ctx, x, y, z, id, opacity)
    if not id then return end
    if ctx.item then
        ctx.item({ x = x, y = y, z = z, id = id, opacity = opacity })
    else
        local img = spriteFor(id)
        if img then ctx.image({ image = img, x = x, y = y, z = z, opacity = opacity }) end
    end
end

local function drawSprite(ctx, n, z, on)
    drawItem(ctx, n.x, n.y, z, itemIdOf(n.key), on and 1.0 or 0.45)
end

-- ids dos estagios de broto (plano 180), caso o JSON nao traga species[key].stages
local STAGE_ITEMS = {
    apple = { 58414, 58415, 58416 }, banana = { 58417, 58418, 58419 },
    orange = { 58420, 58421, 58422 }, lemon = { 58423, 58424, 58425 },
    prickly_pear = { 58426, 58427 }, soul_mushroom = { 58428, 58429 },
    frost_leaf = { 58430, 58431 }, azure_flower = { 58432, 58433 },
}

local function stagesOf(key)
    local sp = S.data.species[key]
    return (sp and type(sp.stages) == "table" and #sp.stages > 0 and sp.stages) or STAGE_ITEMS[key] or {}
end

-- estagio 1..#stages = broto; acima disso = adulto (o item do no)
local function liveItemId(e)
    local st = tonumber(e.stage) or 1
    return stagesOf(e.key)[st] or itemIdOf(e.key)
end

local function liveLabel(e)
    local st = tonumber(e.stage) or 1
    if st <= #stagesOf(e.key) then return "S" .. st end
    return "V" .. (tonumber(e.harvests) or 0)
end

-- origens "x:y:z" dos nos base habilitados (para achar vivo orfao); refeito a cada desenho
local function originSet()
    local set = {}
    for _, n in ipairs(nodes()) do
        if isEnabled(n) then set[n.x .. ":" .. n.y .. ":" .. n.z] = true end
    end
    return set
end

local ORPHAN_COLOR = { 255, 60, 60 }

pcall(function() app.mapView:removeOverlay(OVERLAY_ID) end)
app.mapView:addOverlay(OVERLAY_ID, {
    ondraw = function(ctx)
        local v = ctx.view
        local origins = originSet()
        for _, e in ipairs(S.live or {}) do
            if e.z == v.z and (tonumber(e.i) or 0) == S.liveInstance and inView(v, e.x, e.y) then
                local orphan = not origins[tostring(e.origin)]
                local c = orphan and ORPHAN_COLOR or colorOf(e.key)
                drawItem(ctx, e.x, e.y, v.z, liveItemId(e), 0.8)
                ctx.rect({ x = e.x, y = e.y, z = v.z, filled = false, width = 1, style = "dashed",
                    color = { c[1], c[2], c[3], 230 } })
                ctx.text({ x = e.x, y = e.y, z = v.z, text = liveLabel(e), color = { c[1], c[2], c[3], 255 } })
            end
        end
        -- raio e tiles do pool do no base selecionado
        local cur = current()
        if S.showRadius and cur and cur.z == v.z then
            local po = S.pool and S.pool.origins and S.pool.origins[cur.x .. ":" .. cur.y .. ":" .. cur.z]
            local r = tonumber(S.pool and S.pool.radius) or 7
            ctx.rect({ x = cur.x - r, y = cur.y - r, z = v.z, w = 2 * r + 1, h = 2 * r + 1, filled = false,
                width = 1, style = "dashed", color = rgba(cur.key, 200) })
            if po and type(po.tiles) == "table" then
                for _, tl in ipairs(po.tiles) do
                    ctx.rect({ x = cur.x + tl[1], y = cur.y + tl[2], z = v.z, filled = true, color = rgba(cur.key, 70) })
                end
            end
        end
        for i, n in ipairs(nodes()) do
            if n.z == v.z and inView(v, n.x, n.y) then
                local sel = (i == S.sel)
                local on = isEnabled(n)
                -- o sprite do no, inteiro (arvore = 3x3 a noroeste)
                drawSprite(ctx, n, v.z, on)
                -- so o contorno: o sprite tem de aparecer
                if sel and isTree(n) then
                    -- o espaco que o sprite da arvore ocupa: 3x3 a noroeste, terminando no tile do no
                    ctx.rect({ x = n.x - 2, y = n.y - 2, z = v.z, w = 3, h = 3, filled = false, width = 1,
                        color = rgba(n.key, 140) })
                end
                ctx.rect({ x = n.x, y = n.y, z = v.z, filled = false, width = sel and 2 or 1,
                    style = on and "solid" or "dashed",
                    color = sel and { 255, 255, 255, 255 } or rgba(n.key, 200) })
                if n.locked then
                    ctx.text({ x = n.x, y = n.y, z = v.z, text = "L", color = { 255, 255, 255, 255 } })
                end
                if sel or (v.zoom or 1) <= 1 then
                    ctx.text({ x = n.x, y = n.y - 1, z = v.z, text = n.key .. " " .. i, color = rgba(n.key, 255) })
                end
            end
        end
    end,
})
pcall(function()
    app.mapView:registerShow("Nos de Herbalist (BlackTalon)", OVERLAY_ID, { enabled = true })
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
    local function bad(fmt, ...)
        problems = problems + 1
        lines[#lines + 1] = string.format(fmt, ...)
    end
    local list = nodes()
    for i, n in ipairs(list) do
        if isEnabled(n) then
            local where = string.format("%s %d (%d,%d,%d)", tostring(n.key), i, n.x, n.y, n.z)
            local t = app.map:getTile(n.x, n.y, n.z)
            if not t or not t.ground then
                bad("SEM CHAO: %s", where)
            else
                if t.isBlocking then bad("BLOQUEADO: %s", where) end
                if t.isPZ then bad("EM PZ: %s", where) end
                if (t.houseId or 0) ~= 0 then bad("EM CASA: %s", where) end
            end
            for j = i + 1, #list do
                local m = list[j]
                if isEnabled(m) and m.z == n.z and math.max(math.abs(m.x - n.x), math.abs(m.y - n.y)) < MIN_SPACING then
                    bad("COLADOS: %s e %s %d (%d,%d,%d) a menos de %d sqm", where, tostring(m.key), j, m.x, m.y, m.z, MIN_SPACING)
                end
            end
            if isTree(n) then
                for dy = -2, 0 do
                    for dx = -2, 0 do
                        if dx ~= 0 or dy ~= 0 then
                            local q = app.map:getTile(n.x + dx, n.y + dy, n.z)
                            local qp = string.format("(%d,%d,%d)", n.x + dx, n.y + dy, n.z)
                            if not q or not q.ground then
                                bad("ARVORE SEM CHAO NO 3x3 NOROESTE: %s, tile %s", where, qp)
                            elseif q.isBlocking then
                                bad("ARVORE COM O 3x3 NOROESTE OCUPADO: %s, tile %s", where, qp)
                            elseif q.isPZ or (q.houseId or 0) ~= 0 then
                                bad("ARVORE COM PZ/CASA NO 3x3 NOROESTE: %s, tile %s", where, qp)
                            end
                        end
                    end
                end
            end
        end
    end
    local pool = (S.pool and S.pool.origins) or {}
    for i, n in ipairs(list) do
        if isEnabled(n) then
            local po = pool[n.x .. ":" .. n.y .. ":" .. n.z]
            local where = string.format("%s %d (%d,%d,%d)", tostring(n.key), i, n.x, n.y, n.z)
            if not po then
                bad("SEM POOL: %s nao tem entrada no pool (rode gen_herbalist_nodes.js --pool-only)", where)
            elseif type(po.tiles) ~= "table" or #po.tiles == 0 then
                bad("POOL VAZIO: %s tem 0 tiles no pool (rode gen_herbalist_nodes.js --pool-only)", where)
            end
        end
    end
    if problems == 0 then
        lines[#lines + 1] = "Tudo certo: todo no ligado tem chao livre, fora de PZ e casa, sem vizinho a menos de 3 sqm, e a arvore tem o 3x3 noroeste livre."
    end
    return table.concat(lines, "\n"), problems
end

-- ---------------------------------------------------------------------------
-- A janela
-- ---------------------------------------------------------------------------

S.sel = (S.sel and nodes()[S.sel]) and S.sel or (#nodes() > 0 and 1 or nil)
S.pickKey = S.pickKey or "apple"

local function statusText()
    return (S.dirty and "ALTERACOES NAO SALVAS. " or "Salvo. ") .. #nodes() .. " nos. Arquivo: " .. tostring(store.path)
end

local function listItems()
    local items = {}
    for i, n in ipairs(nodes()) do
        items[#items + 1] = { text = nodeTitle(i, n) }
    end
    local sel = math.max(1, math.min(S.sel or 1, math.max(#items, 1)))
    return items, sel
end

local function refresh(d)
    local items, sel = listItems()
    local n = current()
    d:modify({
        list = { items = items, selection = sel },
        locked = { selected = n ~= nil and n.locked == true },
        enabled = { selected = n ~= nil and isEnabled(n) },
        lbl_status = { text = statusText() },
    })
end

local function goTo(n)
    if n then app.setCameraPosition(n.x, n.y, n.z) end
end

local function changed(d)
    S.dirty = true
    refresh(d)
    redraw()
end

-- um unico tile selecionado no mapa (nunca "o ultimo tile do mouse")
local function selectedTile()
    if (app.selection.size or 0) == 0 then return nil end
    local b = app.selection.bounds
    if b.min.x ~= b.max.x or b.min.y ~= b.max.y then return nil end
    return { x = b.min.x, y = b.min.y, z = b.min.z }
end

local function addNode(d, pos)
    if not pos then return end
    local i = nodeAt(pos.x, pos.y, pos.z)
    if i then
        app.alert(string.format("Ja existe um no ai: %s %d.", tostring(nodes()[i].key), i))
        return
    end
    local list = nodes()
    list[#list + 1] = { key = S.pickKey, region = "manual", x = pos.x, y = pos.y, z = pos.z, locked = true, enabled = true }
    S.sel = #list
    changed(d)
end

-- clicar (selecionar) um tile que tem no ja o escolhe na janela
local function pickAt(d, pos)
    local i = nodeAt(pos.x, pos.y, pos.z)
    if not i then return false end
    S.sel = i
    refresh(d)
    redraw()
    return true
end

-- rodar de novo fecha a janela anterior (senao acumulam)
if S.dlg then pcall(function() S.dlg:close() end) end
S.runChecks = runChecks

local dlg = Dialog({ title = "Nos de Herbalist (BlackTalon)", width = 720, height = 640, resizable = true })
S.dlg = dlg

dlg:label({ text = "Cada no e uma planta ou arvore de fruta que o server cria na instancia 0. Arvore ocupa 3x3 a noroeste." })
dlg:newrow()
dlg:label({ text = "Selecione UM tile no mapa e use os botoes. Travado (L) = o gerador mantem ao rodar de novo." })
dlg:newrow()
dlg:list({
    id = "list",
    height = 260,
    expand = true,
    items = (listItems()),
    onchange = function(d)
        S.sel = d.data.list
        refresh(d)
        redraw()
        goTo(current())
    end,
})
dlg:newrow()

dlg:box({ orient = "horizontal", label = "No do tile selecionado", padding = 4, margin = 4, expand = false })
dlg:combobox({
    id = "species",
    label = "Especie",
    options = speciesIds(),
    option = S.pickKey,
    onchange = function(d) S.pickKey = d.data.species end,
})
dlg:button({
    text = "Por no tile selecionado",
    onclick = function(d)
        local pos = selectedTile()
        if not pos then
            app.alert("Selecione UM tile no mapa.")
            return
        end
        addNode(d, pos)
    end,
})
dlg:button({
    text = "Mover o escolhido para o tile selecionado",
    onclick = function(d)
        local n = current()
        local pos = selectedTile()
        if not n or not pos then
            app.alert("Escolha um no na lista e selecione UM tile de destino no mapa.")
            return
        end
        local other = nodeAt(pos.x, pos.y, pos.z)
        if other and other ~= S.sel then
            app.alert("Ja existe outro no nesse tile.")
            return
        end
        n.x, n.y, n.z = pos.x, pos.y, pos.z
        n.locked = true
        changed(d)
    end,
})
dlg:button({
    text = "Apagar o escolhido",
    onclick = function(d)
        local n = current()
        if not n then return end
        table.remove(nodes(), S.sel)
        S.sel = (#nodes() > 0) and math.max(1, math.min(S.sel, #nodes())) or nil
        changed(d)
    end,
})
dlg:button({
    text = "Trocar a especie do escolhido",
    onclick = function(d)
        local n = current()
        if not n then return end
        n.key = S.pickKey
        n.locked = true
        changed(d)
    end,
})
dlg:button({
    text = "Ir ate ele",
    onclick = function() goTo(current()) end,
})
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Estado do escolhido", padding = 4, margin = 4, expand = false })
dlg:check({ id = "locked", text = "Travado (o gerador mantem)", selected = false })
dlg:check({ id = "enabled", text = "Ligado (desmarcar tira do jogo)", selected = true })
dlg:button({
    text = "Aplicar",
    onclick = function(d)
        local n = current()
        if not n then return end
        n.locked = d.data.locked == true
        n.enabled = d.data.enabled == true
        changed(d)
    end,
})
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", label = "Vivos (somente leitura)", padding = 4, margin = 4, expand = false })
dlg:number({ id = "live_instance", label = "Instancia", value = S.liveInstance, min = 0, max = 100000 })
dlg:check({ id = "show_radius", text = "Mostrar raio", selected = S.showRadius })
dlg:button({
    text = "Recarregar vivos",
    onclick = function(d)
        S.liveInstance = math.floor(tonumber(d.data.live_instance) or 0)
        S.showRadius = d.data.show_radius == true
        loadLive()
        redraw()
    end,
})
dlg:endbox()
dlg:newrow()

dlg:box({ orient = "horizontal", padding = 4, margin = 4, expand = false })
dlg:button({
    text = "Conferir",
    onclick = function()
        local text, problems = runChecks()
        print("[herbalist_nodes] conferencia:\n" .. text)
        app.alert((problems > 0 and (problems .. " problema(s):\n\n") or "") .. text)
    end,
})
dlg:button({
    text = "Recarregar do arquivo",
    onclick = function(d)
        loadLive()
        if loadData() then
            S.sel = (#nodes() > 0) and math.max(1, math.min(S.sel or 1, #nodes())) or nil
            refresh(d)
            redraw()
        else
            app.alert("nodes.json invalido ou ausente: " .. tostring(store.path))
        end
    end,
})
dlg:button({
    text = "Salvar",
    onclick = function(d)
        if store:save(S.data) then
            S.dirty = false
            refresh(d)
            app.alert("Salvo.\nNo server: /herbnodes reload (ou reiniciar) para valer.")
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

-- clicar (selecionar) um tile que tem no ja o escolhe na janela; o listener de uma execucao
-- anterior fica inerte porque compara S.dlg com a SUA janela
S.selectionListener = app.events:on("selectionChange", function()
    local pos = selectedTile()
    if pos and S.dlg == dlg then pickAt(dlg, pos) end
end)
