-- Fix Spawn Creatures
-- Workflow:
--   1. User selects an area on the map.
--   2. Script scans every spawn whose center tile lies inside the selection.
--   3. For each such spawn, it walks its radius looking ONLY at creatures
--      whose tile is also inside the selection.
--   4. A creature is "bad" if its tile does not exist, has no ground, or is
--      blocking. Each creature is attributed to the first spawn that covers
--      it (option B - no duplicates on overlap).
--   5. The dialog lists all bad creatures. "Go" centers the camera on the
--      selected entry. "Fix All" relocates every bad creature to the nearest
--      valid tile inside the same spawn's radius, preserving name, spawnTime
--      and direction. Creatures with no valid destination are left in place
--      and reported.

if not app then
    print("Error: RME Lua API not found.")
    return
end

if not app.hasMap() or not app.map then
    app.alert("No map is currently open.")
    return
end

local selection = app.selection
if not selection or selection.isEmpty then
    app.alert("Select an area on the map first.")
    return
end

local map = app.map

-- Build a set of selected positions for O(1) membership tests.
local function posKey(x, y, z)
    return x .. ":" .. y .. ":" .. z
end

local selectedSet = {}
for _, tile in ipairs(selection.tiles) do
    selectedSet[posKey(tile.x, tile.y, tile.z)] = true
end

local function isSelected(x, y, z)
    return selectedSet[posKey(x, y, z)] == true
end

local function isInvalidTile(tile)
    if not tile then return true end
    if not tile.hasGround then return true end
    if tile.isBlocking then return true end
    return false
end

-- A destination tile must exist, have ground, not block, not already hold
-- a creature, and not be another spawn's center.
local function isValidDestination(tile)
    if not tile then return false end
    if not tile.hasGround then return false end
    if tile.isBlocking then return false end
    if tile.hasCreature then return false end
    if tile.hasSpawn then return false end
    return true
end

-- Cache spawn tiles (iterator is single-pass).
local spawnTiles = {}
for spawnTile in map.spawns do
    spawnTiles[#spawnTiles + 1] = spawnTile
end

local visited = {}
local problems = {}

for _, spawnTile in ipairs(spawnTiles) do
    local spawn = spawnTile.spawn
    if spawn and isSelected(spawnTile.x, spawnTile.y, spawnTile.z) then
        local radius = spawn.size or 0
        local cx, cy, cz = spawnTile.x, spawnTile.y, spawnTile.z

        for dx = -radius, radius do
            for dy = -radius, radius do
                local tx, ty, tz = cx + dx, cy + dy, cz
                local k = posKey(tx, ty, tz)
                if not visited[k] and isSelected(tx, ty, tz) then
                    local tile = map:getTile(tx, ty, tz)
                    if tile and tile.hasCreature then
                        visited[k] = true
                        if isInvalidTile(tile) then
                            local creature = tile.creature
                            problems[#problems + 1] = {
                                name = creature.name,
                                spawnTime = creature.spawnTime,
                                direction = creature.direction,
                                x = tx, y = ty, z = tz,
                                spawnX = cx, spawnY = cy, spawnZ = cz,
                                radius = radius,
                                fixed = false,
                                failed = false,
                            }
                        end
                    end
                end
            end
        end
    end
end

if #problems == 0 then
    app.alert("No spawn creatures on invalid tiles inside the selection.")
    return
end

-- Spiral search from the spawn center, expanding rings 0..radius.
-- Returns the first valid destination tile found, or nil.
local function findDestination(problem)
    local cx, cy, cz, r = problem.spawnX, problem.spawnY, problem.spawnZ, problem.radius
    for ring = 0, r do
        if ring == 0 then
            local t = map:getTile(cx, cy, cz)
            if isValidDestination(t) then return t end
        else
            for dx = -ring, ring do
                for dy = -ring, ring do
                    -- Only the ring boundary (max norm == ring).
                    if math.max(math.abs(dx), math.abs(dy)) == ring then
                        local t = map:getTile(cx + dx, cy + dy, cz)
                        if isValidDestination(t) then return t end
                    end
                end
            end
        end
    end
    return nil
end

local function formatProblem(p)
    local status
    if p.fixed then
        status = string.format(" -> moved to (%d, %d, %d)", p.newX, p.newY, p.newZ)
    elseif p.failed then
        status = "  [NO VALID DESTINATION]"
    else
        status = ""
    end
    return string.format(
        "%s  @ (%d, %d, %d)   [spawn: (%d, %d, %d) r=%d]%s",
        p.name, p.x, p.y, p.z, p.spawnX, p.spawnY, p.spawnZ, p.radius, status
    )
end

local function buildListItems()
    local items = {}
    for i, p in ipairs(problems) do
        items[i] = { text = formatProblem(p) }
    end
    return items
end

local dlg = Dialog {
    title = "Fix Spawn Creatures",
    width = 620,
    height = 460,
    resizable = true,
}

dlg:label {
    id = "lbl_header",
    text = string.format(
        "Found %d creature(s) on invalid tiles inside the selection.",
        #problems
    )
}
dlg:newrow()
dlg:label {
    text = "Go: center the camera on the selected entry. Fix All: relocate every creature."
}
dlg:newrow()

dlg:list {
    id = "problem_list",
    height = 280,
    expand = true,
    items = buildListItems(),
}
dlg:newrow()

dlg:box { orient = "horizontal", padding = 4, expand = false }
    dlg:button {
        text = "Go",
        onclick = function(d)
            local idx = d.data.problem_list
            if type(idx) ~= "number" or idx < 1 or idx > #problems then
                app.alert("Select a creature from the list first.")
                return
            end
            local p = problems[idx]
            app.setCameraPosition(p.x, p.y, p.z)
        end
    }
    dlg:button {
        text = "Fix All",
        onclick = function(d)
            local moved, failed = 0, 0
            app.transaction("Fix spawn creatures", function()
                for _, p in ipairs(problems) do
                    if not p.fixed and not p.failed then
                        local dest = findDestination(p)
                        if dest then
                            local src = map:getTile(p.x, p.y, p.z)
                            if src and src.hasCreature then
                                src:removeCreature()
                            end
                            dest:setCreature(p.name, p.spawnTime, p.direction)
                            p.fixed = true
                            p.newX, p.newY, p.newZ = dest.x, dest.y, dest.z
                            moved = moved + 1
                        else
                            p.failed = true
                            failed = failed + 1
                        end
                    end
                end
            end)

            d:modify {
                problem_list = { items = buildListItems() },
                lbl_header = {
                    text = string.format(
                        "Moved %d creature(s). %d without a valid destination.",
                        moved, failed
                    )
                }
            }

            app.alert(string.format(
                "Fix complete.\nMoved: %d\nNo valid destination: %d",
                moved, failed
            ))
        end
    }
    dlg:button {
        text = "Close",
        onclick = function(d) d:close() end
    }
dlg:endbox()

dlg:show { wait = false }

print(string.format("Fix Spawn Creatures: %d problem(s) found.", #problems))
