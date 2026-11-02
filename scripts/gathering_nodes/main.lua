-- Nodes de gathering do BlackTalon: realca no mapa, no andar e na area que estao na
-- tela, todo item que o server trata como node (mining, woodcutting, herbalist e os
-- crystal nodes do Alien Crystal Tree). So desenha: nao mexe no mapa.
--
-- Ligar/desligar: Ctrl+Shift+G (roda este script de novo) ou o menu Show. O script roda
-- sozinho ao abrir o RME so para o item existir no menu; comeca desligado.
--
-- A fonte e o server: blacktalon-workspace\server\data\scripts\mining\1_table.lua. O
-- mining_table.lua desta pasta e um HARD LINK para ele (o RME so le dentro da propria
-- pasta de scripts), criado por .blacktalon/scripts/link_gathering_table.cjs. Se um git
-- checkout/pull trocar o arquivo do server, o link quebra e o RME fica com a tabela
-- velha: rode o .cjs de novo.
--
-- Texto da tela so em ASCII (a camada Lua do editor quebra acento UTF-8).

local OVERLAY_ID = "bt_gathering_nodes"
local SHOW_LABEL = "Nodes de gathering (BlackTalon)"
local TABLE_FILE = "mining_table.lua"

-- AlienNodes.NODE_BASE e NODE_COUNT, em aliencrystaltree/12_crystal_nodes.lua
local CRYSTAL_BASE = 56515
local CRYSTAL_COUNT = 32

local SECTOR = 32
local GROUND_FLOOR = 7
local MAX_VIEW_TILES = 200000 -- acima disso (zoom muito longe) a varredura trava a tela
local SCROLL_GRACE_MS = 300
local HOVER_REACH = 2         -- o sprite grande cresce para noroeste do tile: ate 3x3
local FILL_ALPHA = 80
local OUTLINE_ALPHA = 240
local MAX_LABEL_ZOOM = 2.0    -- mais longe que isso os nomes viram uma massa: fica so a cor

local KINDS = {
    stone = { label = "Mining", color = { 255, 150, 30 } },
    tree = { label = "Woodcutting", color = { 70, 230, 90 } },
    plant = { label = "Herbalist", color = { 240, 90, 220 } },
    crystal = { label = "Alien crystal", color = { 70, 220, 255 } },
}

-- Rodar o script de novo executa o arquivo no mesmo estado Lua: o estado fica aqui.
BTGatheringNodes = BTGatheringNodes or { sizes = {} }
local S = BTGatheringNodes

-- ---------------------------------------------------------------------------
-- Tabela
-- ---------------------------------------------------------------------------

-- A tabela do server so tem constantes e tabelas: roda num ambiente vazio, para os
-- globais dela (toolsTable, STONE_PICK...) nao vazarem para os outros scripts do RME.
local function loadNodes()
    local src, readErr = app.readFile(TABLE_FILE)
    if not src then
        return nil, readErr
    end
    local env = {}
    local chunk, loadErr = load(src, "=" .. TABLE_FILE, "t", env)
    if not chunk then
        return nil, loadErr
    end
    local ok, runErr = pcall(chunk)
    if not ok then
        return nil, runErr
    end

    local nodes = {}
    local function addGroup(group, kind)
        for id, cfg in pairs(group or {}) do
            local product = cfg.mineableOres and cfg.mineableOres[1]
            nodes[id] = { kind = kind, product = product and product.name }
        end
    end
    addGroup(env.stonesTable, "stone")
    addGroup(env.treesTable, "tree")
    addGroup(env.plantsTable, "plant")
    if not next(nodes) then
        return nil, "tabela sem nodes"
    end
    for id = CRYSTAL_BASE, CRYSTAL_BASE + CRYSTAL_COUNT - 1 do
        nodes[id] = { kind = "crystal" }
    end
    return nodes
end

-- ---------------------------------------------------------------------------
-- Varredura
-- ---------------------------------------------------------------------------

local function nodeTiles(id)
    local size = S.sizes[id]
    if not size then
        local ok, img = pcall(Image.fromItemSprite, id)
        local valid = ok and img.valid
        size = {
            w = valid and math.max(1, img.width // 32) or 1,
            h = valid and math.max(1, img.height // 32) or 1,
        }
        S.sizes[id] = size
    end
    return size.w, size.h
end

local function scanSector(map, sx, sy, z)
    local found = {}
    for y = sy * SECTOR, sy * SECTOR + SECTOR - 1 do
        for x = sx * SECTOR, sx * SECTOR + SECTOR - 1 do
            local tile = map:getTile(x, y, z)
            if tile and tile.itemCount > 0 then
                for _, item in ipairs(tile.items) do
                    local id = item.id
                    if S.nodes[id] then
                        found[#found + 1] = { x = x, y = y, id = id }
                    end
                end
            end
        end
    end
    return found
end

local function clearCache()
    S.sectors = {}
end

-- O desenho do overlay e cacheado pela vista, entao o ondraw so roda quando a vista muda
-- ou quando o mapa/selecao mudam (historico de desfazer). O ctx.view nao traz o scroll em
-- pixels: mesmos limites com a tela PARADA = o mapa mudou e a varredura vale de novo;
-- mesmos limites logo depois de mover = rolagem dentro do mesmo tile, e o que esta
-- guardado continua certo.
local function syncCache(map, v)
    local now = app.getTime()
    local last = S.lastView
    local sameView = last and last.x1 == v.x1 and last.y1 == v.y1 and last.x2 == v.x2
        and last.y2 == v.y2 and last.z == v.z and last.zoom == v.zoom

    if not S.sectors or S.cacheMap ~= map.name then
        clearCache()
        S.cacheMap = map.name
    elseif sameView and now - (S.lastMove or 0) >= SCROLL_GRACE_MS then
        clearCache()
    end
    if not sameView then
        S.lastMove = now
    end
    S.lastView = { x1 = v.x1, y1 = v.y1, x2 = v.x2, y2 = v.y2, z = v.z, zoom = v.zoom }
end

-- ---------------------------------------------------------------------------
-- Overlay
-- ---------------------------------------------------------------------------

local function drawMessage(ctx, text)
    ctx.text({ screen = true, x = 12, y = 40, text = text, color = { 255, 220, 120, 255 } })
end

local function drawNode(ctx, node, z, showLabel)
    local w, h = nodeTiles(node.id)
    local kind = KINDS[S.nodes[node.id].kind]
    local c = kind.color
    local x, y = node.x - (w - 1), node.y - (h - 1)
    ctx.rect({ x = x, y = y, z = z, w = w, h = h, filled = true, color = { c[1], c[2], c[3], FILL_ALPHA } })
    ctx.rect({ x = x, y = y, z = z, w = w, h = h, filled = false, width = 2, color = { c[1], c[2], c[3], OUTLINE_ALPHA } })
    if showLabel then
        ctx.text({ x = x, y = y - 1, z = z, text = kind.label, color = { c[1], c[2], c[3], 255 } })
    end
end

local function drawNodes(ctx)
    if not S.nodes then
        drawMessage(ctx, "Nodes: tabela nao carregou. Rode: node .blacktalon/scripts/link_gathering_table.cjs")
        return
    end
    local map = app.map
    if not map then
        return
    end

    local v = ctx.view
    -- acima do terreo o andar e desenhado deslocado para noroeste: o canto da tela mostra
    -- o tile (x1 + deslocamento), como no RenderView::getScreenPosition
    local offset = v.z <= GROUND_FLOOR and (GROUND_FLOOR - v.z) or 0
    local x1, y1 = math.max(0, v.x1 + offset), math.max(0, v.y1 + offset)
    local x2, y2 = v.x2 + offset + 1, v.y2 + offset + 1
    if (x2 - x1 + 1) * (y2 - y1 + 1) > MAX_VIEW_TILES then
        drawMessage(ctx, "Nodes: zoom longe demais, aproxime para ver o realce")
        return
    end

    syncCache(map, v)
    local floorCache = S.sectors[v.z]
    if not floorCache then
        floorCache = {}
        S.sectors[v.z] = floorCache
    end
    for sy = y1 // SECTOR, y2 // SECTOR do
        for sx = x1 // SECTOR, x2 // SECTOR do
            local key = sy * 65536 + sx
            local sector = floorCache[key]
            if not sector then
                sector = scanSector(map, sx, sy, v.z)
                floorCache[key] = sector
            end
            for _, node in ipairs(sector) do
                drawNode(ctx, node, v.z, v.zoom <= MAX_LABEL_ZOOM)
            end
        end
    end
end

local function describe(id)
    local node = S.nodes[id]
    local text = KINDS[node.kind].label
    if node.product then
        text = text .. ": " .. node.product
    end
    return string.format("%s (item %d)", text, id)
end

local function hoverNode(info)
    local map = app.map
    if not S.nodes or not map or not info.pos then
        return nil
    end
    local px, py, z = info.pos.x, info.pos.y, info.pos.z
    for dy = 0, HOVER_REACH do
        for dx = 0, HOVER_REACH do
            local tile = map:getTile(px + dx, py + dy, z)
            if tile and tile.itemCount > 0 then
                for _, item in ipairs(tile.items) do
                    local id = item.id
                    if S.nodes[id] then
                        local w, h = nodeTiles(id)
                        if dx < w and dy < h then
                            return {
                                tooltip = { text = describe(id), color = { 255, 255, 255 } },
                                highlight = {
                                    x = px + dx - (w - 1), y = py + dy - (h - 1), z = z, w = w, h = h,
                                    filled = false, width = 3, color = { 255, 255, 255, 255 },
                                },
                            }
                        end
                    end
                end
            end
        end
    end
    return nil
end

-- ---------------------------------------------------------------------------
-- Ligar / desligar
-- ---------------------------------------------------------------------------

local nodes, loadError = loadNodes()
S.nodes = nodes
clearCache()
if not nodes then
    print("[gathering_nodes] tabela nao carregou: " .. tostring(loadError))
end

-- a primeira execucao e o autorun ao abrir o RME: so registra, desligado. Nela a janela
-- principal ainda nao existe (o Lua inicia antes do MainFrame) e app.refresh() derruba o
-- editor: acessa o tabbook nulo.
local isStartup = not S.registered
local enabled = not isStartup and not S.enabled
S.registered = true
S.enabled = enabled

app.mapView:addOverlay(OVERLAY_ID, { ondraw = drawNodes, onhover = hoverNode })
app.mapView:registerShow(SHOW_LABEL, OVERLAY_ID, {
    enabled = enabled,
    ontoggle = function(on)
        S.enabled = on
    end,
})
if not isStartup then
    app.refresh()
end
