-- Find Invalid Items
-- Scans the map (or current selection) for items whose IDs are not present
-- in the loaded items database (items.otb / .dat). These are the items that
-- the server rejects with "Failed to create item" when it tries to load the
-- map after you've removed entries from items.xml / items.otb.
--
-- Workflow:
--   1. Pick scope: Whole map, Selection, or Rectangular region.
--   2. Press "Scan" to list every offending item (id + position + ground flag).
--   3. Click an entry and press "Go" to jump the camera there.
--   4. Press "Remove All" to delete them in one undoable transaction.
--      Ground items are removed only if "Include ground" is checked
--      (DANGEROUS — may leave tiles with no floor).

if not app then
    print("Error: RME Lua API not found.")
    return
end

if not app.hasMap() or not app.map then
    app.alert("No map is currently open.")
    return
end

if not Items or not Items.exists then
    app.alert("Items API not available in this build.")
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

-- Scope iterators ------------------------------------------------------------

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

-- Whole-map iterator: walk every tile that exists in the map.
-- Uses map.tiles when available, otherwise falls back to a full bounding-box
-- sweep via getTile (still cheap because empty cells return nil quickly).
local function wholeMapIter()
    if map.tiles then
        local i = 0
        return function()
            i = i + 1
            return map.tiles[i]
        end
    end

    local w  = map.width  or 65535
    local h  = map.height or 65535
    return rectIter(0, 0, 0, w - 1, h - 1, 15)
end

-- ---------------------------------------------------------------------------
-- Scan
-- ---------------------------------------------------------------------------

-- problem entry: { id, x, y, z, isGround, item, tile }
local function scan(tiles_iter, include_ground)
    local problems = {}
    local id_cache = {}  -- memoize Items.exists per id

    local function isInvalid(id)
        local cached = id_cache[id]
        if cached ~= nil then return cached end
        local valid = Items.exists(id) and true or false
        id_cache[id] = not valid
        return not valid
    end

    for tile in tiles_iter do
        if tile then
            local items = tile.items
            if items then
                for i = 1, #items do
                    local it = items[i]
                    if it and isInvalid(it.id) then
                        problems[#problems + 1] = {
                            id = it.id,
                            x = tile.x, y = tile.y, z = tile.z,
                            isGround = false,
                            item = it,
                            tile = tile,
                        }
                    end
                end
            end

            if include_ground and tile.hasGround then
                local g = tile.ground
                if g and isInvalid(g.id) then
                    problems[#problems + 1] = {
                        id = g.id,
                        x = tile.x, y = tile.y, z = tile.z,
                        isGround = true,
                        item = g,
                        tile = tile,
                    }
                end
            end
        end
    end

    return problems
end

-- ---------------------------------------------------------------------------
-- Defaults from current selection / camera
-- ---------------------------------------------------------------------------

local defaults = {
    from_x = 0, from_y = 0, from_z = 7,
    to_x = 0,   to_y = 0,   to_z = 7,
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

local selection_has_tiles = (app.selection and not app.selection.isEmpty)

-- ---------------------------------------------------------------------------
-- State for the dialog
-- ---------------------------------------------------------------------------

local problems = {}

local function formatProblem(p)
    return string.format(
        "id=%-6d  @ (%d, %d, %d)%s",
        p.id, p.x, p.y, p.z,
        p.isGround and "   [ground]" or ""
    )
end

local function buildListItems()
    local items = {}
    for i, p in ipairs(problems) do
        items[i] = { text = formatProblem(p) }
    end
    return items
end

-- ---------------------------------------------------------------------------
-- Dialog
-- ---------------------------------------------------------------------------

local dlg = Dialog {
    title = "Find Invalid Items",
    width = 640,
    height = 540,
    resizable = true,
}

dlg:label {
    text = "Find items whose IDs are missing from items.otb / .dat (server 'Failed to create item')."
}
dlg:newrow()

-- Scope selector
dlg:box { orient = "vertical", label = "Scan Scope", padding = 8, margin = 4, expand = false }
    dlg:radio {
        id   = "mode_map",
        text = "Whole map",
        selected = not selection_has_tiles
    }
    dlg:newrow()
    dlg:radio {
        id   = "mode_selection",
        text = selection_has_tiles
            and string.format("Current selection (%d tiles)", app.selection.size)
            or  "Current selection (empty — select first)",
        selected = selection_has_tiles
    }
    dlg:newrow()
    dlg:radio {
        id   = "mode_rect",
        text = "Rectangular region",
        selected = false
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

-- Options
dlg:box { orient = "vertical", label = "Options", padding = 8, margin = 4, expand = false }
    dlg:check {
        id = "include_ground",
        text = "Include ground items (DANGEROUS — may leave tiles with no floor)",
        selected = true
    }
dlg:endbox()

dlg:separator()
dlg:label { id = "lbl_status", text = "Press Scan to start." }
dlg:newrow()

dlg:list {
    id = "problem_list",
    height = 280,
    expand = true,
    items = {},
}
dlg:newrow()

-- Actions --------------------------------------------------------------------

local function pickIter(data)
    if data.mode_selection then
        if not app.selection or app.selection.isEmpty then
            app.alert("Selection is empty. Select an area first or pick another scope.")
            return nil
        end
        return selectionIter()
    elseif data.mode_rect then
        local x1 = clampInt(data.from_x, 0, 65535)
        local y1 = clampInt(data.from_y, 0, 65535)
        local z1 = clampInt(data.from_z, 0, 15)
        local x2 = clampInt(data.to_x,   0, 65535)
        local y2 = clampInt(data.to_y,   0, 65535)
        local z2 = clampInt(data.to_z,   0, 15)
        return rectIter(x1, y1, z1, x2, y2, z2)
    else
        return wholeMapIter()
    end
end

local function doScan(d)
    local data = d.data
    local iter = pickIter(data)
    if not iter then return end

    local include_ground = data.include_ground and true or false

    d:modify { lbl_status = { text = "Scanning..." } }
    problems = scan(iter, include_ground)

    -- Group counts by id for the summary line.
    local counts = {}
    for _, p in ipairs(problems) do
        counts[p.id] = (counts[p.id] or 0) + 1
    end
    local distinct = 0
    for _ in pairs(counts) do distinct = distinct + 1 end

    local summary = string.format(
        "Found %d invalid item(s) across %d distinct ID(s).",
        #problems, distinct
    )
    d:modify {
        problem_list = { items = buildListItems() },
        lbl_status   = { text = summary },
    }
    print(summary)

    -- Print breakdown to console for copy/paste reports.
    if distinct > 0 then
        print("Invalid item IDs found:")
        local ids = {}
        for id in pairs(counts) do ids[#ids + 1] = id end
        table.sort(ids)
        for _, id in ipairs(ids) do
            print(string.format("  id=%d  count=%d", id, counts[id]))
        end
    end
end

local function doGo(d)
    local idx = d.data.problem_list
    if type(idx) ~= "number" or idx < 1 or idx > #problems then
        app.alert("Select an entry from the list first.")
        return
    end
    local p = problems[idx]
    if app.setCameraPosition then
        app.setCameraPosition(p.x, p.y, p.z)
    end
end

local function doRemoveAll(d)
    if #problems == 0 then
        app.alert("Nothing to remove. Run Scan first.")
        return
    end

    local removed = 0
    app.transaction("Remove invalid items", function()
        for _, p in ipairs(problems) do
            if p.tile and p.item then
                if p.tile:removeItem(p.item) then
                    removed = removed + 1
                end
            end
        end
    end)

    local msg = string.format("Removed %d / %d invalid item(s).", removed, #problems)
    problems = {}
    d:modify {
        problem_list = { items = {} },
        lbl_status   = { text = msg },
    }
    if app.refresh then app.refresh() end
    print(msg)
    app.alert(msg)
end

dlg:box { orient = "horizontal", padding = 4, margin = 4, expand = false }
    dlg:button { text = "Scan",        onclick = function(d) doScan(d) end }
    dlg:button { text = "Go",          onclick = function(d) doGo(d) end }
    dlg:button { text = "Remove All",  onclick = function(d) doRemoveAll(d) end }
    dlg:button { text = "Close",       onclick = function(d) d:close() end }
dlg:endbox()

dlg:show { wait = false }

print("Find Invalid Items: dialog opened.")
