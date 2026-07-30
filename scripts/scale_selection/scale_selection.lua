-- Scale Selection
-- Resize the selected area by an integer factor.
-- Each original tile at relative (rx, ry) becomes an N x N block of clones
-- starting at (rx * N, ry * N), where N is the scale factor.
--
-- Origin (the tile that stays put) is chosen by the user:
--   * Top-left: keeps the NW corner of the selection fixed and grows SE
--   * Center:   centers the scaled shape on the original selection center
--
-- What is copied per tile:
--   - Ground item (id + action/unique IDs)
--   - All non-ground items (id, count, action/unique IDs)
--   - Creature (name, spawn time, direction)
--   - Spawn (radius)
--   - House id
--   - Map flags (PZ, NoPvp, NoLogout, etc.)
--
-- The whole operation is a single undo step.

if not app then
    print("Error: RME Lua API not found.")
    return
end

if not app.hasMap() or not app.map then
    app.alert("No map is currently open.")
    return
end

local map = app.map
local sel = app.selection

if not sel or sel.isEmpty then
    app.alert("Select an area first (RAW/normal selection), then run the script again.")
    return
end

-- ---------------------------------------------------------------------------
-- Snapshot the current selection
-- ---------------------------------------------------------------------------

local function snapshotTile(tile)
    local data = {
        x = tile.x,
        y = tile.y,
        z = tile.z,
        mapFlags = tile.mapFlags or 0,
        houseId = tile.houseId or 0,
        ground = nil,
        items = {},
        creature = nil,
        spawn = nil,
    }

    if tile.hasGround and tile.ground then
        local g = tile.ground
        data.ground = {
            id = g.id,
            actionId = g.actionId or 0,
            uniqueId = g.uniqueId or 0,
        }
    end

    local items = tile.items
    if items then
        for i = 1, #items do
            local it = items[i]
            if it then
                data.items[#data.items + 1] = {
                    id = it.id,
                    count = it.count or 1,
                    actionId = it.actionId or 0,
                    uniqueId = it.uniqueId or 0,
                }
            end
        end
    end

    if tile.hasCreature and tile.creature then
        local c = tile.creature
        data.creature = {
            name = c.name,
            spawnTime = c.spawnTime or 60,
            direction = c.direction or 0,
        }
    end

    if tile.hasSpawn and tile.spawn then
        data.spawn = { size = tile.spawn.size or 1 }
    end

    return data
end

local snapshot = {}
local tiles_collection = sel.tiles
for i = 1, #tiles_collection do
    local t = tiles_collection[i]
    if t then
        snapshot[#snapshot + 1] = snapshotTile(t)
    end
end

if #snapshot == 0 then
    app.alert("Selection has no tiles to scale.")
    return
end

local bounds = sel.bounds
if not bounds or not bounds.min or not bounds.max then
    app.alert("Selection has no valid bounds.")
    return
end
local sel_min = bounds.min
local sel_max = bounds.max
local sel_w = sel_max.x - sel_min.x + 1
local sel_h = sel_max.y - sel_min.y + 1

-- ---------------------------------------------------------------------------
-- Apply a snapshot entry to a target tile
-- ---------------------------------------------------------------------------

local function applySnapshot(target, src)
    -- Clear what's there first so we don't stack on top of existing content.
    target.ground = 0
    local existing = target.items
    if existing then
        for i = #existing, 1, -1 do
            local it = existing[i]
            if it then target:removeItem(it) end
        end
    end
    if target.hasCreature then target:removeCreature() end
    if target.hasSpawn then target:removeSpawn() end

    -- Apply snapshot
    if src.ground then
        target.ground = src.ground.id
        if target.ground then
            if src.ground.actionId ~= 0 then target.ground.actionId = src.ground.actionId end
            if src.ground.uniqueId ~= 0 then target.ground.uniqueId = src.ground.uniqueId end
        end
    end

    for i = 1, #src.items do
        local it_data = src.items[i]
        local new_item = target:addItem(it_data.id, it_data.count)
        if new_item then
            if it_data.actionId ~= 0 then new_item.actionId = it_data.actionId end
            if it_data.uniqueId ~= 0 then new_item.uniqueId = it_data.uniqueId end
        end
    end

    if src.creature then
        target:setCreature(src.creature.name, src.creature.spawnTime, src.creature.direction)
    end
    if src.spawn then
        target:setSpawn(src.spawn.size)
    end

    if src.mapFlags ~= 0 then target.mapFlags = src.mapFlags end
    if src.houseId ~= 0 then target.houseId = src.houseId end
end

-- ---------------------------------------------------------------------------
-- Core operation
-- ---------------------------------------------------------------------------

local function runScale(factor, origin_mode, do_borderize, do_wallize)
    factor = math.floor(factor)
    if factor < 2 then
        app.alert("Scale factor must be at least 2.")
        return
    end
    if factor > 8 then
        app.alert("Scale factor capped at 8 (would explode tile count).")
        return
    end

    -- Decide where the (0,0) of the scaled rectangle goes
    local out_origin_x, out_origin_y
    if origin_mode == "center" then
        local center_x = (sel_min.x + sel_max.x) / 2
        local center_y = (sel_min.y + sel_max.y) / 2
        local new_w = sel_w * factor
        local new_h = sel_h * factor
        out_origin_x = math.floor(center_x - new_w / 2 + 0.5)
        out_origin_y = math.floor(center_y - new_h / 2 + 0.5)
    else
        out_origin_x = sel_min.x
        out_origin_y = sel_min.y
    end

    local touched_tiles = {}

    app.transaction("Scale Selection x" .. factor, function()
        -- 1) Wipe the target rectangle (some tiles may overlap the original selection)
        local target_min_x = out_origin_x
        local target_min_y = out_origin_y
        local target_max_x = out_origin_x + sel_w * factor - 1
        local target_max_y = out_origin_y + sel_h * factor - 1
        for ty = target_min_y, target_max_y do
            for tx = target_min_x, target_max_x do
                local t = map:getTile(tx, ty, sel_min.z)
                if t then
                    -- Same wipe applySnapshot does, but we want the empty state ready
                    t.ground = 0
                    local items = t.items
                    if items then
                        for i = #items, 1, -1 do
                            local it = items[i]
                            if it then t:removeItem(it) end
                        end
                    end
                    if t.hasCreature then t:removeCreature() end
                    if t.hasSpawn then t:removeSpawn() end
                end
            end
        end

        -- 2) For each snapshotted tile, paint its N x N block in the new area
        for i = 1, #snapshot do
            local src = snapshot[i]
            local rel_x = src.x - sel_min.x
            local rel_y = src.y - sel_min.y
            for dy = 0, factor - 1 do
                for dx = 0, factor - 1 do
                    local tx = out_origin_x + rel_x * factor + dx
                    local ty = out_origin_y + rel_y * factor + dy
                    local target = map:getOrCreateTile(tx, ty, src.z)
                    if target then
                        applySnapshot(target, src)
                        touched_tiles[#touched_tiles + 1] = target
                    end
                end
            end
        end

        -- 3) Re-apply auto-borders / wall-magic on every painted tile
        if do_borderize or do_wallize then
            for i = 1, #touched_tiles do
                local t = touched_tiles[i]
                if do_borderize then t:borderize() end
                if do_wallize then t:wallize() end
            end
        end
    end)

    if app.refresh then app.refresh() end

    local status = string.format(
        "Scaled %d tile(s) by %dx -> %d painted tile(s).",
        #snapshot, factor, #touched_tiles
    )
    print(status)
    return status
end

-- ---------------------------------------------------------------------------
-- Dialog
-- ---------------------------------------------------------------------------

local dlg = Dialog {
    title = "Scale Selection",
    width = 380,
    height = 320,
    resizable = false,
}

dlg:label {
    text = string.format(
        "Selection: %d tile(s), bbox %dx%d at z=%d.",
        #snapshot, sel_w, sel_h, sel_min.z
    ),
}
dlg:newrow()

dlg:box { orient = "vertical", label = "Scale", padding = 8, margin = 4, expand = false }
    dlg:number { id = "factor", label = "Factor (N x N):", value = 2, min = 2, max = 8 }
    dlg:newrow()
dlg:endbox()

dlg:box { orient = "vertical", label = "Origin (anchor)", padding = 8, margin = 4, expand = false }
    dlg:radio { id = "origin_topleft", text = "Top-left (NW corner stays put, grows SE)", selected = true }
    dlg:newrow()
    dlg:radio { id = "origin_center", text = "Center (scaled shape stays centered)", selected = false }
    dlg:newrow()
dlg:endbox()

dlg:box { orient = "vertical", label = "After Scaling", padding = 8, margin = 4, expand = false }
    dlg:check { id = "do_borderize", text = "Re-apply ground auto-borders", selected = true }
    dlg:newrow()
    dlg:check { id = "do_wallize",   text = "Re-apply wall auto-magic",     selected = true }
    dlg:newrow()
dlg:endbox()

dlg:separator()
dlg:label { id = "lbl_status", text = "Ready." }
dlg:newrow()

dlg:box { orient = "horizontal", padding = 4, margin = 4, expand = false }
    dlg:button {
        text = "Scale",
        onclick = function(d)
            local data = d.data
            local origin = data.origin_center and "center" or "topleft"
            local status = runScale(
                tonumber(data.factor) or 2,
                origin,
                data.do_borderize and true or false,
                data.do_wallize and true or false
            )
            if status then
                d:modify { lbl_status = { text = status } }
            end
        end
    }
    dlg:button {
        text = "Close",
        onclick = function(d) d:close() end
    }
dlg:endbox()

dlg:show { wait = false }

print(string.format(
    "Scale Selection: dialog opened (%d tile(s) selected).",
    #snapshot
))
