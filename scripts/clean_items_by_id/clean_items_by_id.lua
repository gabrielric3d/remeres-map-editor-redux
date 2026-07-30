-- Clean Items by ID Range
-- Remove items from a selected area or a rectangular region (from_pos -> to_pos)
-- filtered by an item ID range (from_id -> to_id).
--
-- Modes:
--   * Selection: iterates over currently selected tiles.
--   * Rectangle: iterates over every tile inside the cuboid defined by
--     (from_x, from_y, from_z) -> (to_x, to_y, to_z).
--
-- Options:
--   * Include ground: also remove ground items when their id falls in range.
--   * Preview only : count matches without mutating the map.

if not app then
    print("Error: RME Lua API not found.")
    return
end

if not app.hasMap() or not app.map then
    app.alert("No map is currently open.")
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

-- Iterate tile items (both modes share this)
-- Returns matched_count, removed_count.
local function processTiles(tiles_iter, from_id, to_id, include_ground, preview)
    local matched, removed = 0, 0

    -- Build a flat list first so we can wrap mutations in a single transaction.
    local hits = {}  -- { {tile = t, item = it, isGround = bool}, ... }

    for tile in tiles_iter do
        if tile then
            -- Non-ground items (iterate in reverse since we may remove later)
            local items = tile.items
            if items then
                for i = #items, 1, -1 do
                    local it = items[i]
                    if it and it.id >= from_id and it.id <= to_id then
                        matched = matched + 1
                        if not preview then
                            hits[#hits + 1] = { tile = tile, item = it, isGround = false }
                        end
                    end
                end
            end

            -- Ground
            if include_ground and tile.hasGround then
                local g = tile.ground
                if g and g.id >= from_id and g.id <= to_id then
                    matched = matched + 1
                    if not preview then
                        hits[#hits + 1] = { tile = tile, item = g, isGround = true }
                    end
                end
            end
        end
    end

    if not preview and #hits > 0 then
        app.transaction(
            string.format("Clean items %d..%d", from_id, to_id),
            function()
                for _, h in ipairs(hits) do
                    if h.tile:removeItem(h.item) then
                        removed = removed + 1
                    end
                end
            end
        )
    end

    return matched, removed
end

-- Iterator over selected tiles
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

-- Iterator over a cuboid region
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
            -- advance
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
-- Seed defaults from current map / selection / camera
-- ---------------------------------------------------------------------------

local defaults = {
    from_x = 0, from_y = 0, from_z = 7,
    to_x = 0,   to_y = 0,   to_z = 7,
    from_id = 1, to_id = 100,
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
    title = "Clean Items by ID Range",
    width = 480,
    height = 420,
    resizable = false,
}

dlg:label {
    text = "Remove items whose ID is within [From ID, To ID] from the target area.",
}
dlg:newrow()

-- Mode selector
dlg:box { orient = "vertical", label = "Target Area", padding = 8, margin = 4, expand = false }
    dlg:radio {
        id = "mode_selection",
        text = selection_has_tiles
            and string.format("Use current selection (%d tiles)", app.selection.size)
            or  "Use current selection (empty — select first)",
        selected = selection_has_tiles
    }
    dlg:newrow()
    dlg:radio {
        id = "mode_rect",
        text = "Use rectangular region (from pos -> to pos)",
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

-- ID range
dlg:box { orient = "vertical", label = "Item ID Range", padding = 8, margin = 4, expand = false }
    dlg:box { orient = "horizontal", padding = 0, expand = false }
        dlg:number { id = "from_id", label = "From ID:", value = defaults.from_id, min = 0, max = 65535 }
        dlg:number { id = "to_id",   label = "To ID:",   value = defaults.to_id,   min = 0, max = 65535 }
    dlg:endbox()
dlg:endbox()

-- Options
dlg:box { orient = "vertical", label = "Options", padding = 8, margin = 4, expand = false }
    dlg:check {
        id = "include_ground",
        text = "Include ground items (DANGEROUS — may leave tiles with no floor)",
        selected = false
    }
dlg:endbox()

-- Status label
dlg:separator()
dlg:label { id = "lbl_status", text = "Ready." }
dlg:newrow()

-- Action buttons
local function runOperation(d, preview)
    local data = d.data

    local from_id = clampInt(data.from_id, 0, 65535)
    local to_id   = clampInt(data.to_id,   0, 65535)
    from_id, to_id = normalizeRange(from_id, to_id)

    local include_ground = data.include_ground and true or false
    local use_selection  = data.mode_selection and true or false

    local iter
    if use_selection then
        if not app.selection or app.selection.isEmpty then
            app.alert("Selection is empty. Select an area first or switch to rectangular mode.")
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

    local matched, removed = processTiles(iter, from_id, to_id, include_ground, preview)

    local status
    if preview then
        status = string.format(
            "Preview: %d item(s) would be removed in range %d..%d.",
            matched, from_id, to_id
        )
    else
        status = string.format(
            "Removed %d / %d matched item(s) in range %d..%d.",
            removed, matched, from_id, to_id
        )
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
        text = "Clean",
        onclick = function(d) runOperation(d, false) end
    }
    dlg:button {
        text = "Close",
        onclick = function(d) d:close() end
    }
dlg:endbox()

dlg:show { wait = false }

print("Clean Items by ID Range: dialog opened.")
