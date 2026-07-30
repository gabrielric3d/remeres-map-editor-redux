-- Clear Region
-- Wipe items, creatures, spawns and action/unique IDs from a target area.
--
-- Modes:
--   * Selection: iterates over currently selected tiles.
--   * Rectangle: iterates over every tile inside the cuboid defined by
--     (from_x, from_y, from_z) -> (to_x, to_y, to_z).
--
-- Per-tile toggles (independent):
--   * Remove items        - removes every non-ground item
--   * Remove ground       - removes the ground item (DANGEROUS, leaves no floor)
--   * Remove creatures    - removes any creature placed on the tile
--   * Remove spawns       - removes the spawn radius marker
--   * Clear Action IDs    - sets actionId = 0 on remaining items
--   * Clear Unique IDs    - sets uniqueId = 0 on remaining items
--
-- When "Remove items" is on, the AID/UID toggles are ignored for those items
-- (since they are deleted anyway).

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

local function clearItemIds(item, clear_aid, clear_uid)
    if not item then return 0 end
    local touched = 0
    if clear_aid and item.actionId and item.actionId ~= 0 then
        item.actionId = 0
        touched = touched + 1
    end
    if clear_uid and item.uniqueId and item.uniqueId ~= 0 then
        item.uniqueId = 0
        touched = touched + 1
    end
    return touched
end

-- Returns counts table: { items, ground, creatures, spawns, ids }
local function processTiles(tiles_iter, opts, preview)
    local counts = { items = 0, ground = 0, creatures = 0, spawns = 0, ids = 0 }

    -- Build a flat plan first so we can wrap mutations in a single transaction.
    local plan = {}  -- { {kind, tile, item?}, ... }

    for tile in tiles_iter do
        if tile then
            -- Non-ground items (iterate in reverse since order matters for removal)
            local items = tile.items
            if items then
                for i = #items, 1, -1 do
                    local it = items[i]
                    if it then
                        if opts.remove_items then
                            counts.items = counts.items + 1
                            if not preview then
                                plan[#plan + 1] = { kind = "remove_item", tile = tile, item = it }
                            end
                        elseif opts.clear_aid or opts.clear_uid then
                            local will_touch = 0
                            if opts.clear_aid and it.actionId and it.actionId ~= 0 then
                                will_touch = will_touch + 1
                            end
                            if opts.clear_uid and it.uniqueId and it.uniqueId ~= 0 then
                                will_touch = will_touch + 1
                            end
                            if will_touch > 0 then
                                counts.ids = counts.ids + will_touch
                                if not preview then
                                    plan[#plan + 1] = { kind = "clear_ids_item", item = it }
                                end
                            end
                        end
                    end
                end
            end

            -- Ground
            if tile.hasGround and tile.ground then
                if opts.remove_ground then
                    counts.ground = counts.ground + 1
                    if not preview then
                        plan[#plan + 1] = { kind = "remove_ground", tile = tile }
                    end
                elseif opts.clear_aid or opts.clear_uid then
                    local g = tile.ground
                    local will_touch = 0
                    if opts.clear_aid and g.actionId and g.actionId ~= 0 then
                        will_touch = will_touch + 1
                    end
                    if opts.clear_uid and g.uniqueId and g.uniqueId ~= 0 then
                        will_touch = will_touch + 1
                    end
                    if will_touch > 0 then
                        counts.ids = counts.ids + will_touch
                        if not preview then
                            plan[#plan + 1] = { kind = "clear_ids_item", item = g }
                        end
                    end
                end
            end

            -- Creature
            if opts.remove_creatures and tile.hasCreature then
                counts.creatures = counts.creatures + 1
                if not preview then
                    plan[#plan + 1] = { kind = "remove_creature", tile = tile }
                end
            end

            -- Spawn
            if opts.remove_spawns and tile.hasSpawn then
                counts.spawns = counts.spawns + 1
                if not preview then
                    plan[#plan + 1] = { kind = "remove_spawn", tile = tile }
                end
            end
        end
    end

    if not preview and #plan > 0 then
        app.transaction("Clear Region", function()
            for _, step in ipairs(plan) do
                if step.kind == "remove_item" then
                    step.tile:removeItem(step.item)
                elseif step.kind == "remove_ground" then
                    step.tile.ground = 0
                elseif step.kind == "remove_creature" then
                    step.tile:removeCreature()
                elseif step.kind == "remove_spawn" then
                    step.tile:removeSpawn()
                elseif step.kind == "clear_ids_item" then
                    clearItemIds(step.item, opts.clear_aid, opts.clear_uid)
                end
            end
        end)
    end

    return counts
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
    title = "Clear Region",
    width = 500,
    height = 520,
    resizable = false,
}

dlg:label {
    text = "Wipe items, creatures, spawns and action/unique IDs from the target area.",
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

-- What to clear
dlg:box { orient = "vertical", label = "What to Clear", padding = 8, margin = 4, expand = false }
    dlg:check { id = "remove_items",     text = "Remove all items (non-ground)", selected = true }
    dlg:newrow()
    dlg:check { id = "remove_creatures", text = "Remove creatures",              selected = true }
    dlg:newrow()
    dlg:check { id = "remove_spawns",    text = "Remove spawns",                 selected = true }
    dlg:newrow()
    dlg:check { id = "remove_ground",    text = "Remove ground (DANGEROUS - leaves tiles without a floor)", selected = false }
    dlg:newrow()
    dlg:check { id = "clear_aid",        text = "Clear Action IDs on remaining items", selected = true }
    dlg:newrow()
    dlg:check { id = "clear_uid",        text = "Clear Unique IDs on remaining items", selected = true }
dlg:endbox()

-- Status label
dlg:separator()
dlg:label { id = "lbl_status", text = "Ready." }
dlg:newrow()

-- Action buttons
local function runOperation(d, preview)
    local data = d.data

    local opts = {
        remove_items     = data.remove_items and true or false,
        remove_ground    = data.remove_ground and true or false,
        remove_creatures = data.remove_creatures and true or false,
        remove_spawns    = data.remove_spawns and true or false,
        clear_aid        = data.clear_aid and true or false,
        clear_uid        = data.clear_uid and true or false,
    }

    if not (opts.remove_items or opts.remove_ground or opts.remove_creatures
            or opts.remove_spawns or opts.clear_aid or opts.clear_uid) then
        app.alert("Nothing selected to clear. Tick at least one option.")
        return
    end

    local use_selection = data.mode_selection and true or false

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

    local counts = processTiles(iter, opts, preview)
    local verb = preview and "Would remove" or "Removed"
    local status = string.format(
        "%s %d item(s), %d ground, %d creature(s), %d spawn(s); cleared %d ID(s).",
        verb, counts.items, counts.ground, counts.creatures, counts.spawns, counts.ids
    )

    if not preview and app.refresh then app.refresh() end

    d:modify { lbl_status = { text = status } }
    print(status)
end

dlg:box { orient = "horizontal", padding = 4, margin = 4, expand = false }
    dlg:button {
        text = "Preview",
        onclick = function(d) runOperation(d, true) end
    }
    dlg:button {
        text = "Clear",
        onclick = function(d) runOperation(d, false) end
    }
    dlg:button {
        text = "Close",
        onclick = function(d) d:close() end
    }
dlg:endbox()

dlg:show { wait = false }

print("Clear Region: dialog opened.")
