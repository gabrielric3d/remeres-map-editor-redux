-- @Title: Pyramid Generator
-- @Author: BlackTalon
-- @Description: Generates multi-floor pyramids using the "pyramid template" tile IDs. Set the base size and the script stacks layers, shrinking by 2 tiles per floor.
-- @Version: 1.0
-- @Shortcut: Ctrl+Shift+P

-- ============================================================================
-- IDs from the "pyramid template" brush
-- ============================================================================
-- 5x5 template layout (reference):
--   1556 1403 1403 1403 1558
--   1399 1557 1402 1581 1401
--   1399 1398   .  1400 1401
--   1399 1579 1404 1578 1401
--   1554 1405 1405 1405 1552

local IDS = {
	-- Outer corners
	corner_nw = 1556,
	corner_ne = 1558,
	corner_sw = 1554,
	corner_se = 1552,

	-- Outer borders (poles)
	border_n  = 1403,
	border_s  = 1405,
	border_w  = 1399,
	border_e  = 1401,

	-- Inner corners (L-shaped ramps going up)
	inner_nw  = 1557,
	inner_ne  = 1581,
	inner_sw  = 1579,
	inner_se  = 1578,

	-- Inner borders (straight ramps going up)
	inner_n   = 1402,
	inner_s   = 1404,
	inner_w   = 1398,
	inner_e   = 1400,

	-- Tile used when the layer degenerates to 1x1 (peak)
	peak      = 1556,
}

-- ============================================================================
-- Persisted state
-- ============================================================================

local config = {
	size = 5,
	mode = "stacked",   -- "stacked" (multi-floor) or "flat" (base only)
	clearAbove = true,  -- clear tiles above before stacking
}

local store = app and app.storage and app.storage("pyramid_generator")
local saved = store and store:load()
if saved then
	for k, v in pairs(saved) do config[k] = v end
end

local function saveConfig()
	if store then store:save(config) end
end

-- ============================================================================
-- Helpers
-- ============================================================================

local function getAnchor()
	-- Prefer selection min corner; fall back to camera position.
	local sel = app and app.selection
	if sel and not sel.isEmpty and sel.bounds then
		return sel.bounds.min
	end
	local cam = app and app.getCameraPosition and app.getCameraPosition()
	if cam then
		return { x = cam.x, y = cam.y, z = cam.z }
	end
	return nil
end

local function placeGround(x, y, z, itemId)
	local tile = app.map:getOrCreateTile(x, y, z)
	if tile then
		tile.ground = itemId
	end
end

local function clearTile(x, y, z)
	local tile = app.map:getTile(x, y, z)
	if tile then
		tile.ground = 0
	end
end

-- ============================================================================
-- Generates the BASE layer (size N x N) at (ox, oy, z)
--   * Outer ring: corners + outer borders (regular ground)
--   * Inner ring (1 sqm inside the outer ring): ramps going up
--   * Anything further inward is left to the next (smaller) layer above
-- ============================================================================

local function generateBaseLayer(ox, oy, z, n)
	if n == 1 then
		placeGround(ox, oy, z, IDS.peak)
		return
	end

	if n == 2 then
		placeGround(ox,     oy,     z, IDS.corner_nw)
		placeGround(ox + 1, oy,     z, IDS.corner_ne)
		placeGround(ox,     oy + 1, z, IDS.corner_sw)
		placeGround(ox + 1, oy + 1, z, IDS.corner_se)
		return
	end

	local last = n - 1

	for ly = 0, last do
		for lx = 0, last do
			local id

			-- Outer ring
			if lx == 0 and ly == 0 then
				id = IDS.corner_nw
			elseif lx == last and ly == 0 then
				id = IDS.corner_ne
			elseif lx == 0 and ly == last then
				id = IDS.corner_sw
			elseif lx == last and ly == last then
				id = IDS.corner_se
			elseif ly == 0 then
				id = IDS.border_n
			elseif ly == last then
				id = IDS.border_s
			elseif lx == 0 then
				id = IDS.border_w
			elseif lx == last then
				id = IDS.border_e

			-- Inner ring (ramps; their upper half is drawn by the next layer)
			elseif lx == 1 and ly == 1 then
				id = IDS.inner_nw
			elseif lx == last - 1 and ly == 1 then
				id = IDS.inner_ne
			elseif lx == 1 and ly == last - 1 then
				id = IDS.inner_sw
			elseif lx == last - 1 and ly == last - 1 then
				id = IDS.inner_se
			elseif ly == 1 and lx > 1 and lx < last - 1 then
				id = IDS.inner_n
			elseif ly == last - 1 and lx > 1 and lx < last - 1 then
				id = IDS.inner_s
			elseif lx == 1 and ly > 1 and ly < last - 1 then
				id = IDS.inner_w
			elseif lx == last - 1 and ly > 1 and ly < last - 1 then
				id = IDS.inner_e
			end

			if id then
				placeGround(ox + lx, oy + ly, z, id)
			end
		end
	end
end

-- ============================================================================
-- Generates an UPPER layer (size N x N) at (ox, oy, z)
--   * Outer ring: lands exactly on top of the ramps from the layer below,
--     forming the upper half of those ramps automatically.
--   * Inner ring: ramps going up to the NEXT layer.
--   * Anything further inward is left empty (next layer covers it).
-- ============================================================================

local function generateUpperLayer(ox, oy, z, n)
	-- For a 1x1 upper floor we just need the peak
	if n == 1 then
		placeGround(ox, oy, z, IDS.peak)
		return
	end

	if n == 2 then
		placeGround(ox,     oy,     z, IDS.corner_nw)
		placeGround(ox + 1, oy,     z, IDS.corner_ne)
		placeGround(ox,     oy + 1, z, IDS.corner_sw)
		placeGround(ox + 1, oy + 1, z, IDS.corner_se)
		return
	end

	local last = n - 1

	for ly = 0, last do
		for lx = 0, last do
			local id

			-- Outer ring (sits on top of ramps from the floor below)
			if lx == 0 and ly == 0 then
				id = IDS.corner_nw
			elseif lx == last and ly == 0 then
				id = IDS.corner_ne
			elseif lx == 0 and ly == last then
				id = IDS.corner_sw
			elseif lx == last and ly == last then
				id = IDS.corner_se
			elseif ly == 0 then
				id = IDS.border_n
			elseif ly == last then
				id = IDS.border_s
			elseif lx == 0 then
				id = IDS.border_w
			elseif lx == last then
				id = IDS.border_e

			-- Inner ring (ramps going up to the next floor)
			elseif n >= 5 and lx == 1 and ly == 1 then
				id = IDS.inner_nw
			elseif n >= 5 and lx == last - 1 and ly == 1 then
				id = IDS.inner_ne
			elseif n >= 5 and lx == 1 and ly == last - 1 then
				id = IDS.inner_sw
			elseif n >= 5 and lx == last - 1 and ly == last - 1 then
				id = IDS.inner_se
			elseif n >= 5 and ly == 1 and lx > 1 and lx < last - 1 then
				id = IDS.inner_n
			elseif n >= 5 and ly == last - 1 and lx > 1 and lx < last - 1 then
				id = IDS.inner_s
			elseif n >= 5 and lx == 1 and ly > 1 and ly < last - 1 then
				id = IDS.inner_w
			elseif n >= 5 and lx == last - 1 and ly > 1 and ly < last - 1 then
				id = IDS.inner_e
			end

			if id then
				placeGround(ox + lx, oy + ly, z, id)
			end
		end
	end
end

-- ============================================================================
-- Generates the full pyramid
-- ============================================================================
-- Layer k (0-indexed, base = 0):
--   * Layer size: size - 2*k  (shrinks 1 tile on EACH side per floor)
--   * Z position: anchor.z - k  (in Tibia, smaller Z = higher floor)
--   * XY offset:  +k  (each upper layer is 1 sqm inward on every side, so
--                      its outer ring lands on top of the ramps below)

local function generatePyramid(anchor, size, mode, clearAbove)
	if not app or not app.map then return end

	app.transaction("Generate Pyramid", function()
		local layers = math.floor((size + 1) / 2)

		-- Clear tiles on upper floors within the footprint to avoid leftovers
		if clearAbove then
			for k = 1, layers do
				local cz = anchor.z - k
				if cz < 0 then break end
				for dy = 0, size - 1 do
					for dx = 0, size - 1 do
						clearTile(anchor.x + dx, anchor.y + dy, cz)
					end
				end
			end
		end

		-- Generate each layer, shifted 1 sqm inward on every side per floor
		for k = 0, layers - 1 do
			local layerSize = size - 2 * k
			if layerSize < 1 then break end

			local lz = anchor.z - k
			if lz < 0 then break end

			local ox = anchor.x + k
			local oy = anchor.y + k

			if k == 0 then
				generateBaseLayer(ox, oy, lz, layerSize)
			else
				generateUpperLayer(ox, oy, lz, layerSize)
			end

			-- "flat" mode: base only
			if mode == "flat" then break end

			if k % 2 == 0 then app.yield() end
		end
	end)

	if app.refresh then app.refresh() end
end

-- ============================================================================
-- UI
-- ============================================================================

local function createDialog()
	local dlg = Dialog {
		title = "Pyramid Generator",
		width = 380,
		height = 320,
		resizable = false,
		onclose = function() saveConfig() end,
	}

	dlg:box { orient = "vertical", label = "Size" }
	dlg:label { text = "Base side (in tiles). Odd values end in a 1x1 peak." }
	dlg:number { id = "size", label = "Side:", value = config.size, min = 1, max = 99 }
	dlg:endbox()

	dlg:box { orient = "vertical", label = "Mode" }
	dlg:combobox {
		id = "mode",
		label = "Generation:",
		options = { "stacked", "flat" },
		option = config.mode,
	}
	dlg:label { text = "stacked = multi-floor pyramid | flat = base only" }
	dlg:check { id = "clear", text = "Clear tiles on upper floors before generating", selected = config.clearAbove }
	dlg:endbox()

	dlg:separator()

	dlg:label { text = "Anchor: NW corner of the selection, or camera if no selection." }
	dlg:label { text = "Each upper floor shrinks 1 sqm inward on every side." }

	dlg:separator()

	dlg:box { orient = "horizontal" }
	dlg:button { text = "Generate", onclick = function(d)
		if not app.hasMap or not app.hasMap() then
			app.alert("Open a map before generating the pyramid.")
			return
		end

		config.size = math.max(1, math.floor(d.data.size or 5))
		config.mode = d.data.mode or "stacked"
		config.clearAbove = d.data.clear and true or false
		saveConfig()

		local anchor = getAnchor()
		if not anchor then
			app.alert("Select an area or move the camera to the target location.")
			return
		end

		generatePyramid(anchor, config.size, config.mode, config.clearAbove)

		local layers = (config.mode == "flat") and 1 or math.floor((config.size + 1) / 2)
		app.alert(string.format("Pyramid generated: %dx%d base, %d layer(s).",
			config.size, config.size, layers))
	end }

	dlg:button { text = "Close", onclick = function(d) d:close() end }
	dlg:endbox()

	dlg:show { wait = false }
end

-- ============================================================================
-- Entry
-- ============================================================================

if not app then
	print("Error: RME Lua API not found.")
	return
end

createDialog()
