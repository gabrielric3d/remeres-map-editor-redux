-- Set Ground Action ID
-- Registers "Set Action ID on grounds..." in the map right-click menu.
-- Select tiles (rectangle or lasso), right-click, pick the entry and type the
-- action id: it is written on the ground of every selected tile. 0 removes it.
-- Items on top of the ground are not touched. The whole operation is one undo step.

if not app then
    print("Error: RME Lua API not found.")
    return
end

-- Autorun registers the entry once; running the script again by hand must not
-- add a second copy. A script reload recreates the Lua state and clears this.
if SET_GROUND_ACTION_ID_REGISTERED then
    return
end
SET_GROUND_ACTION_ID_REGISTERED = true

local last_action_id = 1000

local function selectedGrounds()
    local grounds = {}
    local sel = app.selection
    if not sel or sel.isEmpty then
        return grounds
    end
    local tiles = sel.tiles
    for i = 1, #tiles do
        local tile = tiles[i]
        if tile and tile.hasGround and tile.ground then
            grounds[#grounds + 1] = tile
        end
    end
    return grounds
end

local function askActionId(ground_count)
    local result = nil

    local dlg = Dialog { title = "Set Action ID on grounds" }
    dlg:label { text = string.format("%d selected tile(s) with ground.", ground_count) }
    dlg:newrow()
    dlg:number { id = "action_id", label = "Action ID (0 = remove):", value = last_action_id, min = 0, max = 65535 }
    dlg:newrow()
    dlg:button {
        text = "OK",
        onclick = function(d)
            result = math.floor(tonumber(d.data.action_id) or 0)
            d:close()
        end
    }
    dlg:button {
        text = "Cancel",
        onclick = function(d) d:close() end
    }
    dlg:show { wait = true }

    return result
end

local function applyActionId(grounds, aid)
    app.transaction("Set ground action id", function()
        for i = 1, #grounds do
            local tile = grounds[i]
            -- Changing an item's attribute does not register the tile for undo
            -- by itself; rewriting the map flags (unchanged) does.
            tile.mapFlags = tile.mapFlags
            tile.ground.actionId = aid
        end
    end)
end

app.addContextMenu("Set Action ID on grounds...", function()
    if not app.hasMap() or not app.map then
        return
    end

    local grounds = selectedGrounds()
    if #grounds == 0 then
        app.alert("No selected tile has a ground.")
        return
    end

    local aid = askActionId(#grounds)
    if not aid then
        return
    end
    if aid < 0 or aid > 65535 then
        app.alert("Action ID must be between 0 and 65535.")
        return
    end

    last_action_id = aid
    applyActionId(grounds, aid)

    if aid == 0 then
        print(string.format("Set Ground Action ID: removed the action id from %d ground(s).", #grounds))
    else
        print(string.format("Set Ground Action ID: action id %d written on %d ground(s).", aid, #grounds))
    end
end)
