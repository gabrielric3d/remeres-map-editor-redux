-- Find Stacked Duplicate Items
-- Identify items that have the SAME ID stacked on the SAME SQM (tile).
--
-- Modes:
--   * Selection: iterates over currently selected tiles.
--   * Rectangle: iterates over every tile inside the cuboid defined by
--     (from_x, from_y, from_z) -> (to_x, to_y, to_z).
--
-- Options:
--   * Ignore stackable items : skip items flagged as stackable (e.g. coins,
--     where multiple of the same id on a tile can be intentional).
--   * Include ground         : also count the ground item when one of the
--     stacked items shares the ground's id.
--   * Remove extra copies    : keep the first occurrence of each duplicated
--     id on a tile and delete the rest (otherwise just report).
--
-- A "duplicate" = 2+ items with the same id on the same tile.

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

-- Scan tiles for stacked duplicates.
-- Returns:
--   report : array of { x, y, z, id, name, count, extra }
--   totals : { tiles, dup_groups, extra_items }
--   plan   : array of { tile, item } items to remove (only when not preview)
local function scanTiles(tiles_iter, opts, preview)
    local report = {}
    local totals = { tiles = 0, dup_groups = 0, extra_items = 0 }
    local plan = {}

    for tile in tiles_iter do
        if tile then
            -- Group items on this tile by id. Preserve first-seen order.
            local groups = {}     -- id -> { count, name, items = {...} }
            local order  = {}     -- ordered list of ids as first seen

            local function consider(it)
                if not it then return end
                if opts.ignore_stackable and it.isStackable then return end
                local id = it.id
                local g = groups[id]
                if not g then
                    g = { count = 0, name = it.name, items = {} }
                    groups[id] = g
                    order[#order + 1] = id
                end
                g.count = g.count + 1
                g.items[#g.items + 1] = it
            end

            -- Ground (optional)
            if opts.include_ground and tile.hasGround then
                consider(tile.ground)
            end

            -- Stacked (non-ground) items
            local items = tile.items
            if items then
                for i = 1, #items do
                    consider(items[i])
                end
            end

            -- Emit duplicates for this tile
            local tile_has_dup = false
            for _, id in ipairs(order) do
                local g = groups[id]
                if g.count >= 2 then
                    tile_has_dup = true
                    totals.dup_groups = totals.dup_groups + 1
                    local extra = g.count - 1
                    totals.extra_items = totals.extra_items + extra

                    report[#report + 1] = {
                        x = tile.x, y = tile.y, z = tile.z,
                        id = id, name = g.name,
                        count = g.count, extra = extra,
                    }

                    -- Plan removal of every copy except the first
                    if not preview and opts.remove_extras then
                        for k = 2, #g.items do
                            plan[#plan + 1] = { tile = tile, item = g.items[k] }
                        end
                    end
                end
            end

            if tile_has_dup then
                totals.tiles = totals.tiles + 1
            end
        end
    end

    return report, totals, plan
end

-- ---------------------------------------------------------------------------
-- Seed defaults from current selection / camera
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
            defaults.from_x = (cam.x or 1000) - 20
            defaults.from_y = (cam.y or 1000) - 20
            defaults.from_z = cam.z or 7
            defaults.to_x   = (cam.x or 1000) + 20
            defaults.to_y   = (cam.y or 1000) + 20
            defaults.to_z   = cam.z or 7
        end
    end
end

local selection_has_tiles = (app.selection and not app.selection.isEmpty)

-- ---------------------------------------------------------------------------
-- Dialog
-- ---------------------------------------------------------------------------

local dlg = Dialog {
    title = "Find Stacked Duplicate Items",
    width = 520,
    height = 460,
    resizable = false,
}

dlg:label {
    text = "Find items with the same ID stacked on the same SQM in the target area.",
}
dlg:newrow()

-- Mode selector
dlg:box { orient = "vertical", label = "Target Area", padding = 8, margin = 4, expand = false }
    dlg:radio {
        id = "mode_selection",
        text = selection_has_tiles
            and string.format("Use current selection (%d tiles)", app.selection.size)
            or  "Use current selection (empty - select first)",
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

-- Options
dlg:box { orient = "vertical", label = "Options", padding = 8, margin = 4, expand = false }
    dlg:check {
        id = "ignore_stackable",
        text = "Ignore stackable items (coins, etc. - multiples may be intentional)",
        selected = true
    }
    dlg:newrow()
    dlg:check {
        id = "include_ground",
        text = "Include ground item in the comparison",
        selected = false
    }
    dlg:newrow()
    dlg:check {
        id = "remove_extras",
        text = "Remove extra copies (keep 1 per id per tile) when running 'Apply'",
        selected = false
    }
dlg:endbox()

-- Status label
dlg:separator()
dlg:label { id = "lbl_status", text = "Ready." }
dlg:newrow()

-- ---------------------------------------------------------------------------
-- Run
-- ---------------------------------------------------------------------------

local function buildIterator(data)
    if data.mode_selection then
        if not app.selection or app.selection.isEmpty then
            app.alert("Selection is empty. Select an area first or switch to rectangular mode.")
            return nil
        end
        return selectionIter()
    end

    local x1 = clampInt(data.from_x, 0, 65535)
    local y1 = clampInt(data.from_y, 0, 65535)
    local z1 = clampInt(data.from_z, 0, 15)
    local x2 = clampInt(data.to_x,   0, 65535)
    local y2 = clampInt(data.to_y,   0, 65535)
    local z2 = clampInt(data.to_z,   0, 15)
    return rectIter(x1, y1, z1, x2, y2, z2)
end

local function printReport(report)
    if #report == 0 then
        print("Find Stacked Duplicates: no duplicates found.")
        return
    end
    print(string.format("Find Stacked Duplicates: %d duplicate group(s) found:", #report))
    local shown = 0
    for _, r in ipairs(report) do
        print(string.format(
            "  (%d, %d, %d)  id=%d  '%s'  x%d  (%d extra)",
            r.x, r.y, r.z, r.id, r.name or "?", r.count, r.extra
        ))
        shown = shown + 1
        if shown >= 500 then
            print(string.format("  ... (%d more not printed)", #report - shown))
            break
        end
    end
end

local function runOperation(d, preview)
    local data = d.data

    local opts = {
        ignore_stackable = data.ignore_stackable and true or false,
        include_ground   = data.include_ground   and true or false,
        remove_extras    = data.remove_extras    and true or false,
    }

    local iter = buildIterator(data)
    if not iter then return end

    local report, totals, plan = scanTiles(iter, opts, preview)

    printReport(report)

    local status
    if preview or not opts.remove_extras then
        status = string.format(
            "Found %d duplicate group(s) on %d tile(s); %d extra item(s). See script output.",
            totals.dup_groups, totals.tiles, totals.extra_items
        )
        if not preview and not opts.remove_extras then
            status = status .. " (enable 'Remove extra copies' to delete them)"
        end
    else
        local removed = 0
        if #plan > 0 then
            app.transaction("Remove stacked duplicates", function()
                for _, step in ipairs(plan) do
                    if step.tile:removeItem(step.item) then
                        removed = removed + 1
                    end
                end
            end)
            if app.refresh then app.refresh() end
        end
        status = string.format(
            "Removed %d / %d extra item(s) across %d duplicate group(s) on %d tile(s).",
            removed, totals.extra_items, totals.dup_groups, totals.tiles
        )
    end

    d:modify { lbl_status = { text = status } }
    print(status)
end

dlg:box { orient = "horizontal", padding = 4, margin = 4, expand = false }
    dlg:button {
        text = "Scan (report only)",
        onclick = function(d) runOperation(d, true) end
    }
    dlg:button {
        text = "Apply",
        onclick = function(d) runOperation(d, false) end
    }
    dlg:button {
        text = "Close",
        onclick = function(d) d:close() end
    }
dlg:endbox()

dlg:show { wait = false }

print("Find Stacked Duplicate Items: dialog opened.")
