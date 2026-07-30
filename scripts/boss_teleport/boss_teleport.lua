-- Boss Teleport
-- Lista as salas de boss (nome + posicao) e teleporta a camera para a sala ao
-- clicar na entrada.
--
-- Fontes dos dados:
--   actions/leverboss/table.lua (configQuest) -> grupos Daily/Inferno/Depths/
--     Quest; pos = virtualRoom.centerPos, entradas "(room)" = room.centerPos
--     quando difere da sala virtual.
--   task/1_bossroom_table.lua (BOSSES_ROOMS) -> grupo Task Rooms;
--     pos = virtualRoom.playerPosition.
--
-- Para atualizar depois de mexer nas tables do servidor, rode
-- gen_from_server_table.py / gen_from_task_rooms.py e cole a saida no bloco
-- BOSSES abaixo: o sandbox Lua do editor nao tem `io`, entao os dados ficam
-- embutidos aqui.

if not app then
    print("Error: RME Lua API not found.")
    return
end

-- ============================================================================
-- Dados
-- ============================================================================

local BOSSES = {
    -- Daily Bosses
    { group = "Daily",    id = 930,  name = "Easterminator",                    x = 51,   y = 1643,  z = 6 },
    { group = "Daily",    id = 924,  name = "Grinch",                           x = 268,  y = 1956,  z = 5 },
    { group = "Daily",    id = 922,  name = "Blacktalon",                       x = 3174, y = 53,    z = 6 },
    { group = "Daily",    id = 901,  name = "Duke Krule",                       x = 1411, y = 1348,  z = 9 },
    { group = "Daily",    id = 902,  name = "Dragon Knight Aragorn",            x = 1628, y = 1141,  z = 5 },
    { group = "Daily",    id = 903,  name = "Heroic Dragon Knight Aragorn",     x = 1606, y = 1178,  z = 10 },
    { group = "Daily",    id = 904,  name = "Sureza",                           x = 1541, y = 616,   z = 12 },
    { group = "Daily",    id = 905,  name = "The Pale Worm",                    x = 1822, y = 802,   z = 12 },
    { group = "Daily",    id = 911,  name = "Saruman",                          x = 2154, y = 465,   z = 1 },
    { group = "Daily",    id = 912,  name = "Cursed King",                      x = 2691, y = 521,   z = 15 },
    { group = "Daily",    id = 912,  name = "Cursed King (room)",               x = 2651, y = 482,   z = 15 },
    { group = "Daily",    id = 913,  name = "Feroxa",                           x = 1739, y = 2038,  z = 12 },
    { group = "Daily",    id = 914,  name = "Crystal Warlord",                  x = 2301, y = 2091,  z = 3 },
    { group = "Daily",    id = 915,  name = "The last Lore Keeper",             x = 2004, y = 1741,  z = 14 },
    { group = "Daily",    id = 916,  name = "Moss Guardian",                    x = 3058, y = 2559,  z = 12 },
    { group = "Daily",    id = 918,  name = "Archangel Gabriel",                x = 1634, y = 3061,  z = 4 },
    { group = "Daily",    id = 919,  name = "Archangel Raphael",                x = 3102, y = 2978,  z = 5 },
    { group = "Daily",    id = 921,  name = "Archangel Michael",                x = 3357, y = 3175,  z = 4 },
    { group = "Daily",    id = 923,  name = "Sugar Daddy",                      x = 2493, y = 3929,  z = 4 },

    -- Inferno
    { group = "Inferno",  id = 931,  name = "Ossyrak, the Grave Sovereign",     x = 3920, y = 3395,  z = 11 },
    { group = "Inferno",  id = 932,  name = "Quarantine, the Eternal Patient",  x = 3962, y = 2880,  z = 8 },
    { group = "Inferno",  id = 933,  name = "Grotesque, the Eternal Ringmaster", x = 4776, y = 2726, z = 8 },
    { group = "Inferno",  id = 934,  name = "Drevan, the Bloodbound Aristocrat", x = 5226, y = 2726, z = 10 },
    { group = "Inferno",  id = 935,  name = "Jotunveil, the Eternal Glacier",   x = 4938, y = 3635,  z = 7 },
    { group = "Inferno",  id = 936,  name = "Amenrath, the Undying Pharaoh",    x = 6170, y = 3966,  z = 4 },
    { group = "Inferno",  id = 937,  name = "Igrath, Throne of Ash",            x = 5764, y = 2872,  z = 8 },
    -- 938 Warden Karash e 939 Morvane ainda nao tem sala definida na table.lua

    -- Eternal Depths
    { group = "Depths",   id = 925,  name = "Vhal'Ruun",                        x = 174,  y = 9147,  z = 9 },
    { group = "Depths",   id = 926,  name = "Nyxathor",                         x = 97,   y = 9530,  z = 10 },
    { group = "Depths",   id = 927,  name = "Cryothal",                         x = 1082, y = 10516, z = 3 },
    { group = "Depths",   id = 928,  name = "Thal'Xyrr",                        x = 401,  y = 11067, z = 2 },
    { group = "Depths",   id = 929,  name = "Thal'Runyx",                       x = 105,  y = 11609, z = 10 },

    -- Quest Bosses
    { group = "Quest",    id = 3533, name = "Ass Ripper",                       x = 2217, y = 1016,  z = 14 },
    { group = "Quest",    id = 3534, name = "Sariel",                           x = 770,  y = 11826, z = 1 },
    { group = "Quest",    id = 3516, name = "Cthulhu",                          x = 2257, y = 847,   z = 6 },
    { group = "Quest",    id = 3516, name = "Cthulhu (room)",                   x = 2333, y = 782,   z = 7 },
    { group = "Quest",    id = 3517, name = "Azrael (Normal)",                  x = 2415, y = 1510,  z = 3 },
    { group = "Quest",    id = 3517, name = "Azrael (Normal, room)",            x = 2333, y = 782,   z = 7 },
    { group = "Quest",    id = 3518, name = "Azrael (PvP)",                     x = 424,  y = 5183,  z = 3 },
    { group = "Quest",    id = 3518, name = "Azrael (PvP, room)",               x = 2333, y = 782,   z = 7 },
    { group = "Quest",    id = 3519, name = "Lucifer",                          x = 2415, y = 1510,  z = 3 },
    { group = "Quest",    id = 3519, name = "Lucifer (room)",                   x = 2333, y = 782,   z = 7 },

    -- Task Rooms: virtualRoom.playerPosition de
    -- server/data/scripts/task/1_bossroom_table.lua (BOSSES_ROOMS).
    -- bx/by/bz = bossPosition, ex/ey/ez = tpExit (so no tooltip).
    { group = "Task Rooms", name = "Mainland",                                x = 826,  y = 819,  z = 15, bx = 826,  by = 835,  bz = 15, ex = 938,  ey = 876,  ez = 15 },
    { group = "Task Rooms", name = "Nefrah",                                  x = 1215, y = 205,  z = 15, bx = 1214, by = 222,  bz = 15, ex = 1235, ey = 329,  ez = 15 },
    { group = "Task Rooms", name = "HighDesert",                              x = 2024, y = 692,  z = 15, bx = 2024, by = 708,  bz = 15, ex = 1960, ey = 733,  ez = 15 },
    { group = "Task Rooms", name = "HighDesert_SarumanCastle_normal",         x = 2206, y = 599,  z = 10, bx = 2224, by = 599,  bz = 10, ex = 2201, ey = 599,  ez = 10 },
    { group = "Task Rooms", name = "HighDesert_SarumanCastle_pvp",            x = 1135, y = 4347, z = 10, bx = 1153, by = 4347, bz = 10, ex = 1131, ey = 4347, ez = 10 },
    { group = "Task Rooms", name = "DarkIsland",                              x = 2830, y = 689,  z = 13, bx = 2841, by = 699,  bz = 13, ex = 2715, ey = 696,  ez = 14 },
    { group = "Task Rooms", name = "Icescar_part_one",                        x = 1778, y = 2403, z = 15, bx = 1758, by = 2409, bz = 15, ex = 1697, ey = 2432, ez = 14 },
    { group = "Task Rooms", name = "Icescar_part_two",                        x = 1906, y = 2400, z = 14, bx = 1887, by = 2410, bz = 14, ex = 1680, ey = 2432, ez = 14 },
    { group = "Task Rooms", name = "Wasteland",                               x = 3378, y = 2498, z = 5,  bx = 3378, by = 2481, bz = 5,  ex = 3309, ey = 2545, ez = 5 },
    { group = "Task Rooms", name = "Frostvein Sanctuary Abyssal Maw",         x = 1962, y = 2899, z = 2,  bx = 1959, by = 2877, bz = 2,  ex = 2358, ey = 3103, ez = 4 },
    { group = "Task Rooms", name = "Frostvein Sanctuary Glacial Juggernaut",  x = 1816, y = 3311, z = 9,  bx = 1817, by = 3336, bz = 9,  ex = 2358, ey = 3103, ez = 4 },
    { group = "Task Rooms", name = "Frostvein Sanctuary Celestial Harbinger", x = 1811, y = 3146, z = 8,  bx = 1811, by = 3128, bz = 8,  ex = 2358, ey = 3103, ez = 4 },
    { group = "Task Rooms", name = "Verdant Aether The Rootkraken",           x = 2421, y = 3438, z = 5,  bx = 2430, by = 3462, bz = 5,  ex = 2376, ey = 3132, ez = 4 },
    { group = "Task Rooms", name = "Verdant Aether Toxic Ravager",            x = 2500, y = 3050, z = 9,  bx = 2522, by = 3033, bz = 9,  ex = 2376, ey = 3132, ez = 4 },
    { group = "Task Rooms", name = "Verdant Aether Spirit of the Abyss",      x = 2842, y = 3232, z = 11, bx = 2842, by = 3200, bz = 11, ex = 2376, ey = 3132, ez = 4 },
    { group = "Task Rooms", name = "Luminaris Prism Shardlord",               x = 2741, y = 3481, z = 5,  bx = 2741, by = 3456, bz = 5,  ex = 2409, ey = 3105, ez = 4 },
    { group = "Task Rooms", name = "Luminaris Heavens Blade",                 x = 3405, y = 3336, z = 5,  bx = 3406, by = 3318, bz = 5,  ex = 2409, ey = 3105, ez = 4 },
    { group = "Task Rooms", name = "Candyland",                               x = 2584, y = 3724, z = 5,  bx = 2584, by = 3744, bz = 5,  ex = 2384, ey = 3074, ez = 4 },
    { group = "Task Rooms", name = "Graveyard",                               x = 3856, y = 3730, z = 7,  bx = 3857, by = 3753, bz = 7,  ex = 4030, ey = 3797, ez = 7 },
    { group = "Task Rooms", name = "Hospital",                                x = 3922, y = 3747, z = 7,  bx = 3934, by = 3735, bz = 7,  ex = 4019, ey = 3787, ez = 7 },
    { group = "Task Rooms", name = "Circus",                                  x = 3838, y = 3825, z = 7,  bx = 3845, by = 3844, bz = 7,  ex = 4016, ey = 3771, ez = 7 },
    { group = "Task Rooms", name = "Mansion",                                 x = 3891, y = 3826, z = 7,  bx = 3905, by = 3838, bz = 7,  ex = 4022, ey = 3757, ez = 7 },
    { group = "Task Rooms", name = "Niflheim",                                x = 3824, y = 3915, z = 7,  bx = 3848, by = 3922, bz = 7,  ex = 4038, ey = 3752, ez = 7 },
    { group = "Task Rooms", name = "The Pharaoh Blight",                      x = 3910, y = 3902, z = 7,  bx = 3935, by = 3913, bz = 7,  ex = 4054, ey = 3758, ez = 7 },
    { group = "Task Rooms", name = "Satan Citadel",                           x = 3847, y = 4017, z = 7,  bx = 3841, by = 3988, bz = 7,  ex = 4061, ey = 3771, ez = 7 },
    { group = "Task Rooms", name = "Test",                                    x = 661,  y = 936,  z = 7,  bx = 657,  by = 936,  bz = 7,  ex = 670,  ey = 936,  ez = 7 },
    -- "Luminaris Radiant Guardian" esta comentada na table do servidor.
}

local GROUP_LABELS = {
    ["Todos"]          = nil,
    ["Daily Bosses"]   = "Daily",
    ["Inferno"]        = "Inferno",
    ["Eternal Depths"] = "Depths",
    ["Quest Bosses"]   = "Quest",
    ["Task Rooms"]     = "Task Rooms",
}
local GROUP_ORDER = { "Todos", "Daily Bosses", "Inferno", "Eternal Depths", "Quest Bosses", "Task Rooms" }

-- ============================================================================
-- Estado
-- ============================================================================

local dlg = nil
local visible = {}   -- entradas atualmente mostradas na list (mesma ordem)

-- ============================================================================
-- Helpers
-- ============================================================================

local function formatPos(b)
    return string.format("%d, %d, %d", b.x, b.y, b.z)
end

local function formatLine(b)
    if b.id then
        return string.format("%s  |  %s  |  aid %d", b.name, formatPos(b), b.id)
    end
    return string.format("%s  |  %s", b.name, formatPos(b))
end

local function formatTooltip(b)
    local lines = { b.name }
    if b.id then
        lines[#lines + 1] = "Lever action id: " .. b.id
        lines[#lines + 1] = "centerPos: " .. formatPos(b)
    else
        lines[#lines + 1] = "playerPosition: " .. formatPos(b)
    end
    if b.bx then
        lines[#lines + 1] = string.format("bossPosition: %d, %d, %d", b.bx, b.by, b.bz)
    end
    if b.ex then
        lines[#lines + 1] = string.format("tpExit: %d, %d, %d", b.ex, b.ey, b.ez)
    end
    lines[#lines + 1] = "Grupo: " .. b.group
    return table.concat(lines, "\n")
end

local function matches(b, filter, group)
    if group and b.group ~= group then
        return false
    end
    if not filter or filter == "" then
        return true
    end
    local haystack = string.lower(b.name .. " " .. (b.id or "") .. " " .. formatPos(b) .. " " .. b.group)
    for word in string.gmatch(string.lower(filter), "%S+") do
        if not string.find(haystack, word, 1, true) then
            return false
        end
    end
    return true
end

local function buildItems(filter, group, sortAZ)
    visible = {}
    for _, b in ipairs(BOSSES) do
        if matches(b, filter, group) then
            visible[#visible + 1] = b
        end
    end

    if sortAZ then
        table.sort(visible, function(a, b)
            return string.lower(a.name) < string.lower(b.name)
        end)
    end

    local items = {}
    for _, b in ipairs(visible) do
        items[#items + 1] = {
            text = formatLine(b),
            tooltip = formatTooltip(b)
        }
    end

    if #items == 0 then
        items[1] = { text = "(nenhuma entrada corresponde ao filtro)", tooltip = "" }
    end
    return items
end

local function refreshList()
    if not dlg then return end
    local data = dlg.data
    local group = GROUP_LABELS[data.group or "Todos"]
    local items = buildItems(data.filter, group, data.sort_az)
    dlg:modify { boss_list = { items = items } }
    dlg:modify { lbl_status = { text = string.format("%d entrada(s) na lista.", #visible) } }
end

local function goTo(index)
    local b = visible[index]
    if not b then return end
    if not app.hasMap() then
        app.alert("Nenhum mapa aberto.")
        return
    end
    app.setCameraPosition(b.x, b.y, b.z)
    if dlg then
        dlg:modify { lbl_status = { text = string.format("%s -> %s", b.name, formatPos(b)) } }
    end
end

-- ============================================================================
-- UI
-- ============================================================================

dlg = Dialog {
    title = "Boss Teleport",
    width = 460,
    height = 560,
    resizable = true,
    dockable = true,
    onclose = function()
        dlg = nil
    end
}

dlg:box { orient = "horizontal", padding = 4, margin = 2, expand = false }
    dlg:input {
        id = "filter",
        label = "Buscar:",
        text = "",
        expand = true,
        onchange = function() refreshList() end
    }
dlg:endbox()
dlg:newrow()

dlg:box { orient = "horizontal", padding = 4, margin = 2, expand = false }
    dlg:combobox {
        id = "group",
        label = "Grupo:",
        options = GROUP_ORDER,
        option = "Todos",
        onchange = function() refreshList() end
    }
    dlg:check {
        id = "sort_az",
        text = "Ordenar A-Z",
        selected = false,
        onclick = function() refreshList() end
    }
dlg:endbox()
dlg:newrow()

dlg:label { text = "Clique numa entrada para ir ate a sala:" }
dlg:newrow()

dlg:list {
    id = "boss_list",
    height = 380,
    expand = true,
    items = buildItems("", nil, false),
    onchange = function(d)
        local idx = d.data.boss_list
        if idx and idx > 0 then goTo(idx) end
    end,
    ondoubleclick = function(d)
        local idx = d.data.boss_list
        if idx and idx > 0 then goTo(idx) end
    end
}
dlg:newrow()

dlg:box { orient = "horizontal", padding = 4, margin = 2, expand = false }
    dlg:button {
        text = "Ir",
        width = 60,
        onclick = function(d)
            local idx = d.data.boss_list
            if idx and idx > 0 then
                goTo(idx)
            else
                app.alert("Selecione uma entrada na lista.")
            end
        end
    }
    dlg:button {
        text = "Limpar filtro",
        width = 90,
        onclick = function(d)
            d:modify { filter = { text = "" } }
            refreshList()
        end
    }
dlg:endbox()
dlg:newrow()

dlg:label { id = "lbl_status", text = string.format("%d entrada(s) na lista.", #visible) }

dlg:show { wait = false }
