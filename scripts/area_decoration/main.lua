-- Area Decoration (Lua)
-- Port fiel do motor C++ de source/editor/area_decoration.cpp (FASE 1: motor +
-- presets JSON + importador XML + harness minimo de UI).
--
-- Divergencias deliberadas e documentadas em relacao ao C++:
--  * RNG: mt19937 proprio com mapeamentos fixos (uniform int por modulo, float
--    = next()/2^32, Fisher-Yates i=n..2). A ORDEM dos sorteios segue o C++,
--    mas as sequencias por seed nao sao identicas as do binario MSVC (as
--    distribuicoes da STL sao implementation-defined).
--  * sortRulesByPriority: std::sort e instavel; aqui o desempate e por indice
--    original (estavel e deterministico).
--  * Chaves de hash espacial/posicao usam strings "x:y(:z)" em vez de inteiros
--    de 64 bits (mesma identidade para coordenadas de mapa normais).
--  * gridSpacing e clampado a >=1 e gridJitter a >=0 (no C++ spacing<=0 trava
--    em loop infinito e jitter negativo e UB).
--  * Aritmetica em double (Lua) em vez de float32 (C++): applyFriendBias,
--    falloff e densidades divergem nos bits baixos (ja subsumido pelo RNG).
--  * Persistencia Fase 1: presets ficam em app.storage (JSON), keyed por nome;
--    o lado de ESCRITA de XML e o PresetManager em arquivos ficam para uma
--    fase posterior. importXmlPresets reconstroi o conjunto do zero (espelha o
--    clear() de loadPresets).
--  * app.yield() e chamado dentro de loops longos de geracao; assume-se que o
--    mapa nao e mutado durante a geracao (no C++ o loop e atomico).
--  * ItemGroup (recalculateWeights/selectRandom com cache) nao foi portado:
--    zero call sites no C++; o motor usa selectItemFromRule, que recalcula.
--
-- Comportamentos "estranhos" do C++ sao preservados de proposito (flagados com
-- [as-is]): validate confere regras desabilitadas; cluster rules ignoram
-- maxPlacements e friend bias; generateClusterRandom nao aplica SpacingConfig;
-- presenca de cluster rule desliga o filtro de blocked em collectTileData; o
-- lookahead de maxItemsTotal nao liga previewWasCapped; lastApplied inclui
-- itens que nunca chegaram ao mapa; density default de XML sem atributo = 0.3.

-- ============================================================================
-- Constantes / utilidades
-- ============================================================================

local UNLIMITED = -1
local FRIEND_PADDING = 2 -- kFriendPadding (modo mapa real)
local CLUSTER_ATTEMPTS = 20 -- tentativas por sub-centro extra
local SPATIAL_CELL = 8

local function posKey(x, y, z)
	return x .. ":" .. y .. ":" .. z
end

-- divisao truncada em direcao a zero (C++ int division)
local function idiv(a, b)
	local q = a / b
	if q >= 0 then
		return math.floor(q)
	end
	return math.ceil(q)
end

local function chebyshev(ax, ay, bx, by)
	return math.max(math.abs(ax - bx), math.abs(ay - by))
end

local function yield()
	if app and app.yield then
		app.yield()
	end
end

-- ============================================================================
-- mt19937 (32 bits) + mapeamentos fixos
-- ============================================================================

local MT = {}
MT.__index = MT

function MT.new(seed)
	local self = setmetatable({ mt = {}, index = 625 }, MT)
	local m = self.mt
	m[1] = seed & 0xFFFFFFFF
	for i = 2, 624 do
		m[i] = (1812433253 * (m[i - 1] ~ (m[i - 1] >> 30)) + (i - 1)) & 0xFFFFFFFF
	end
	return self
end

function MT:nextRaw()
	local m = self.mt
	if self.index > 624 then
		for i = 1, 624 do
			local y = (m[i] & 0x80000000) | (m[i % 624 + 1] & 0x7FFFFFFF)
			local v = m[(i + 396) % 624 + 1] ~ (y >> 1)
			if y & 1 == 1 then
				v = v ~ 0x9908B0DF
			end
			m[i] = v
		end
		self.index = 1
	end
	local y = m[self.index]
	self.index = self.index + 1
	y = y ~ (y >> 11)
	y = (y ~ ((y << 7) & 0x9D2C5680)) & 0xFFFFFFFF
	y = (y ~ ((y << 15) & 0xEFC60000)) & 0xFFFFFFFF
	return y ~ (y >> 18)
end

-- uniforme inclusivo [lo, hi]
function MT:int(lo, hi)
	local range = hi - lo + 1
	if range <= 0 then
		return lo
	end
	return lo + self:nextRaw() % range
end

-- uniforme [0, 1)
function MT:float()
	return self:nextRaw() / 4294967296.0
end

-- Fisher-Yates in place
function MT:shuffle(t)
	for i = #t, 2, -1 do
		local j = self:int(1, i)
		t[i], t[j] = t[j], t[i]
	end
end

-- ============================================================================
-- Construtores com defaults identicos aos structs C++ (area_decoration.h)
-- ============================================================================

local Defaults = {}

function Defaults.itemEntry(id, weight)
	return {
		itemId = id or 0,
		weight = weight or 100,
		isComposite = false,
		compositeTiles = {},
		isCluster = false,
		clusterCount = 3,
		clusterRadius = 3,
		clusterMinDistance = 2,
		hasCenterPoint = false,
		centerOffset = { x = 0, y = 0, z = 0 },
	}
end

function Defaults.makeComposite(tiles, weight)
	local e = Defaults.itemEntry(0, weight or 100)
	e.isComposite = true
	e.compositeTiles = tiles
	return e
end

function Defaults.makeCluster(tiles, weight, count, radius, minDistance)
	local e = Defaults.makeComposite(tiles, weight)
	e.isCluster = true
	e.clusterCount = count or 3
	e.clusterRadius = radius or 3
	e.clusterMinDistance = minDistance or 2
	return e
end

function Defaults.floorRule()
	return {
		name = "",
		floorId = 0,
		fromFloorId = 0,
		toFloorId = 0,
		items = {},
		borderItemId = 0,
		friendFloorId = 0,
		friendFromFloorId = 0,
		friendToFloorId = 0,
		friendChance = 0,
		friendStrength = 0,
		maxPlacements = UNLIMITED,
		density = 1.0,
		priority = 0,
		enabled = true,
		ruleMode = "single", -- "single" | "range" | "cluster"
		clusterTiles = {},
		hasCenterPoint = false,
		centerOffset = { x = 0, y = 0, z = 0 },
		instanceCount = 1,
		instanceMinDistance = 5,
		requireGround = true,
	}
end

function Defaults.preset()
	return {
		name = "",
		floorRules = {},
		spacing = { minDistance = 1, minSameItemDistance = 2, checkDiagonals = true },
		distribution = {
			mode = 0, -- 0=PureRandom 1=Clustered 2=GridBased
			clusterStrength = 0.5,
			clusterCount = 3,
			gridSpacingX = 3,
			gridSpacingY = 3,
			gridJitter = 1,
		},
		maxItemsTotal = UNLIMITED,
		skipBlockedTiles = true,
		defaultSeed = 0,
		area = Defaults.area(),
		hasArea = false,
	}
end

function Defaults.area()
	return {
		type = 0, -- 0=Rectangle 1=FloodFill 2=Selection
		rectMin = { x = 0, y = 0, z = 0 },
		rectMax = { x = 0, y = 0, z = 0 },
		floodOrigin = { x = 0, y = 0, z = 0 },
		floodTargetGround = 0,
		floodMaxRadius = 100,
	}
end

-- ============================================================================
-- Regras: matching, ordenacao e validacao (C++ 47-283)
-- ============================================================================

local function isFriendRange(rule)
	return rule.friendFromFloorId > 0 and rule.friendToFloorId > 0
end

local function hasFriendFloor(rule)
	return rule.friendFloorId > 0 or isFriendRange(rule)
end

local function matchesFloor(rule, groundId)
	if rule.ruleMode == "cluster" then
		return false
	end
	if rule.ruleMode == "range" then
		return groundId >= rule.fromFloorId and groundId <= rule.toFloorId
	end
	return groundId == rule.floorId
end

-- prioridade desc; desempate por indice original (divergencia documentada:
-- std::sort do C++ e instavel)
local function sortRulesByPriority(preset)
	local order = {}
	for i, r in ipairs(preset.floorRules) do
		order[r] = i
	end
	table.sort(preset.floorRules, function(a, b)
		if a.priority ~= b.priority then
			return a.priority > b.priority
		end
		return order[a] < order[b]
	end)
end

local function getMatchingRules(preset, groundId)
	local out = {}
	for _, rule in ipairs(preset.floorRules) do
		if rule.enabled and matchesFloor(rule, groundId) then
			out[#out + 1] = rule
		end
	end
	return out
end

local function clusterBounds(rule)
	local tiles = rule.clusterTiles
	if #tiles == 0 then
		return { x = 0, y = 0, z = 0 }, { x = 0, y = 0, z = 0 }
	end
	local mn = { x = tiles[1].offset.x, y = tiles[1].offset.y, z = tiles[1].offset.z }
	local mx = { x = mn.x, y = mn.y, z = mn.z }
	for _, t in ipairs(tiles) do
		mn.x = math.min(mn.x, t.offset.x)
		mn.y = math.min(mn.y, t.offset.y)
		mn.z = math.min(mn.z, t.offset.z)
		mx.x = math.max(mx.x, t.offset.x)
		mx.y = math.max(mx.y, t.offset.y)
		mx.z = math.max(mx.z, t.offset.z)
	end
	return mn, mx
end

local function representativeItemId(entry)
	if not entry.isComposite then
		return entry.itemId
	end
	for _, t in ipairs(entry.compositeTiles) do
		for _, id in ipairs(t.itemIds) do
			if id > 0 then
				return id
			end
		end
	end
	return 0
end

-- primeiro id > 0 de rule.clusterTiles (FloorRule::getClusterRepresentativeItemId)
local function clusterRepresentativeItemId(rule)
	for _, t in ipairs(rule.clusterTiles) do
		for _, id in ipairs(t.itemIds) do
			if id > 0 then
				return id
			end
		end
	end
	return 0
end

-- ids unicos > 0 dos clusterTiles; ordem de primeira ocorrencia (divergencia
-- documentada: o C++ devolve em ordem de hash-set, nao-deterministica)
local function clusterItemIds(rule)
	local out, seen = {}, {}
	for _, t in ipairs(rule.clusterTiles) do
		for _, id in ipairs(t.itemIds) do
			if id > 0 and not seen[id] then
				seen[id] = true
				out[#out + 1] = id
			end
		end
	end
	return out
end

-- [as-is] conta TODOS os slots, incluindo id 0 e duplicados
local function clusterTotalItemCount(rule)
	local count = 0
	for _, t in ipairs(rule.clusterTiles) do
		count = count + #t.itemIds
	end
	return count
end

-- primeira regra habilitada que casa o ground (DecorationPreset::findRule)
local function findRule(preset, groundId)
	for _, rule in ipairs(preset.floorRules) do
		if rule.enabled and matchesFloor(rule, groundId) then
			return rule
		end
	end
	return nil
end

-- fail-fast, mensagens identicas ao C++; [as-is] valida regras desabilitadas
local function validatePreset(preset)
	if #preset.floorRules == 0 then
		return false, "No floor rules defined"
	end
	for _, rule in ipairs(preset.floorRules) do
		if rule.ruleMode == "cluster" then
			if #rule.clusterTiles == 0 then
				return false, "Cluster rule '" .. rule.name .. "' has no cluster tiles"
			end
			local hasClusterItems = false
			for _, t in ipairs(rule.clusterTiles) do
				if #t.itemIds > 0 then
					hasClusterItems = true
					break
				end
			end
			if not hasClusterItems then
				return false, "Cluster rule '" .. rule.name .. "' has no items in cluster tiles"
			end
			if rule.hasCenterPoint then
				local mn, mx = clusterBounds(rule)
				local c = rule.centerOffset
				if c.x < mn.x or c.x > mx.x or c.y < mn.y or c.y > mx.y or c.z < mn.z or c.z > mx.z then
					return false, "Cluster rule '" .. rule.name .. "' centerOffset is outside cluster bounds"
				end
			end
			if rule.instanceCount <= 0 then
				return false, "Cluster rule '" .. rule.name .. "' instanceCount must be > 0"
			end
		elseif rule.ruleMode == "range" then
			if rule.fromFloorId > rule.toFloorId then
				return false, "Invalid floor range: fromFloorId > toFloorId"
			end
		else
			if rule.floorId == 0 then
				return false, "Floor rule has no floor ID specified"
			end
		end

		if rule.friendChance < 0 or rule.friendChance > 100 then
			return false, "Friend chance must be between 0 and 100"
		end
		if rule.friendStrength < 0 or rule.friendStrength > 100 then
			return false, "Friend strength must be between 0 and 100"
		end
		if isFriendRange(rule) and rule.friendFromFloorId > rule.friendToFloorId then
			return false, "Invalid friend floor range: fromFloorId > toFloorId"
		end
		if rule.ruleMode ~= "cluster" and #rule.items == 0 then
			return false, "Floor rule '" .. rule.name .. "' has no items"
		end

		for _, item in ipairs(rule.items) do
			if item.weight <= 0 then
				return false, "Item weight must be positive"
			end
			if item.isComposite then
				if #item.compositeTiles == 0 then
					return false, "Composite entry has no tiles"
				end
				local hasItems = false
				for _, t in ipairs(item.compositeTiles) do
					if #t.itemIds > 0 then
						hasItems = true
						break
					end
				end
				if not hasItems then
					return false, "Composite entry has no items"
				end
				if item.isCluster then
					if item.clusterCount <= 0 then
						return false, "Cluster entry has invalid count"
					end
					if item.clusterRadius < 0 then
						return false, "Cluster entry has invalid radius"
					end
					if item.clusterMinDistance < 0 then
						return false, "Cluster entry has invalid spacing"
					end
				end
			elseif item.itemId == 0 then
				return false, "Item entry has invalid item ID"
			end
		end
	end
	if preset.spacing.minDistance < 0 then
		return false, "minDistance cannot be negative"
	end
	return true
end

-- ============================================================================
-- Spatial hash (celulas 8x8, 2D, z ignorado — [as-is]) (C++ 477-518)
-- ============================================================================

local SpatialHash = {}
SpatialHash.__index = SpatialHash

function SpatialHash.new()
	return setmetatable({ cells = {} }, SpatialHash)
end

function SpatialHash:clear()
	self.cells = {}
end

function SpatialHash:insert(x, y, index)
	local key = idiv(x, SPATIAL_CELL) .. ":" .. idiv(y, SPATIAL_CELL)
	local cell = self.cells[key]
	if not cell then
		cell = {}
		self.cells[key] = cell
	end
	cell[#cell + 1] = { x = x, y = y, index = index }
end

-- chebyshev inclusivo, z ignorado; ordem: dy externo, dx interno, insercao
function SpatialHash:queryRadius(x, y, radius)
	local out = {}
	local cellRadius = idiv(radius, SPATIAL_CELL) + 1
	local cx, cy = idiv(x, SPATIAL_CELL), idiv(y, SPATIAL_CELL)
	for dy = -cellRadius, cellRadius do
		for dx = -cellRadius, cellRadius do
			local cell = self.cells[(cx + dx) .. ":" .. (cy + dy)]
			if cell then
				for _, e in ipairs(cell) do
					if chebyshev(e.x, e.y, x, y) <= radius then
						out[#out + 1] = e.index
					end
				end
			end
		end
	end
	return out
end

-- ============================================================================
-- Area -> lista de posicoes (C++ 349-475)
-- ============================================================================

-- mapAdapter: exists/groundId/isBlocking/hasStackedItems/stackIds (ver Engine)
local AreaOps = {}

function AreaOps.positions(area, mapAdapter)
	if area.type == 0 then
		return AreaOps.rectangle(area)
	elseif area.type == 1 then
		return AreaOps.floodFill(area, mapAdapter)
	elseif area.type == 2 then
		return AreaOps.selection()
	end
	return {}
end

-- normaliza por eixo; ordem: z externo, y, x interno
function AreaOps.rectangle(area)
	local minX = math.min(area.rectMin.x, area.rectMax.x)
	local maxX = math.max(area.rectMin.x, area.rectMax.x)
	local minY = math.min(area.rectMin.y, area.rectMax.y)
	local maxY = math.max(area.rectMin.y, area.rectMax.y)
	local minZ = math.min(area.rectMin.z, area.rectMax.z)
	local maxZ = math.max(area.rectMin.z, area.rectMax.z)
	local out = {}
	for z = minZ, maxZ do
		for y = minY, maxY do
			for x = minX, maxX do
				out[#out + 1] = { x = x, y = y, z = z }
			end
		end
	end
	return out
end

-- BFS 4-conectado (W,E,N,S), raio MANHATTAN inclusivo checado ANTES do
-- visited ([as-is]); vizinhos so a partir de tiles que casam o ground alvo
function AreaOps.floodFill(area, mapAdapter)
	local out = {}
	if not mapAdapter then
		return out
	end
	local ox, oy, oz = area.floodOrigin.x, area.floodOrigin.y, area.floodOrigin.z
	local target = area.floodTargetGround
	if target == 0 then
		target = mapAdapter:groundId(ox, oy, oz)
		if not target then
			return out
		end
	end
	local visited = {}
	local queue, head = { { ox, oy } }, 1
	while head <= #queue do
		local c = queue[head]
		head = head + 1
		local x, y = c[1], c[2]
		local dist = math.abs(x - ox) + math.abs(y - oy)
		if dist <= area.floodMaxRadius then
			local key = x .. ":" .. y
			if not visited[key] then
				visited[key] = true
				local gid = mapAdapter:groundId(x, y, oz)
				if gid and gid == target then
					out[#out + 1] = { x = x, y = y, z = oz }
					queue[#queue + 1] = { x - 1, y }
					queue[#queue + 1] = { x + 1, y }
					queue[#queue + 1] = { x, y - 1 }
					queue[#queue + 1] = { x, y + 1 }
				end
			end
		end
		if head % 512 == 0 then
			yield()
		end
	end
	return out
end

-- todos os tiles selecionados, sem filtro, na ordem da selecao ([as-is])
function AreaOps.selection()
	local out = {}
	if not (app and app.selection) then
		return out
	end
	for _, tile in ipairs(app.selection.tiles) do
		out[#out + 1] = { x = tile.x, y = tile.y, z = tile.z }
	end
	return out
end

-- [as-is] so Rectangle e implementado, com bounds CRUS (nao normalizados:
-- rect invertido => sempre false); FloodFill/Selection retornam sempre false
function AreaOps.contains(area, pos)
	if area.type ~= 0 then
		return false
	end
	return pos.x >= area.rectMin.x and pos.x <= area.rectMax.x
		and pos.y >= area.rectMin.y and pos.y <= area.rectMax.y
		and pos.z >= area.rectMin.z and pos.z <= area.rectMax.z
end

-- [as-is] Rectangle devolve os campos crus; FloodFill devolve caixa QUADRADA
-- (chebyshev) ao redor da origem apesar do flood ser limitado por manhattan;
-- Selection devolve zeros
function AreaOps.getBounds(area)
	if area.type == 0 then
		return area.rectMin, area.rectMax
	elseif area.type == 1 then
		local o = area.floodOrigin
		local r = area.floodMaxRadius
		return { x = o.x - r, y = o.y - r, z = o.z }, { x = o.x + r, y = o.y + r, z = o.z }
	end
	return { x = 0, y = 0, z = 0 }, { x = 0, y = 0, z = 0 }
end

-- ============================================================================
-- PreviewManager (integracao com renderizacao; overlay chega na Fase 2)
-- ============================================================================

local PreviewManager = {
	active = nil,
	previewOpacity = 0.7, -- alpha do ghost; setter NAO clampado ([as-is])
}

function PreviewManager.setActivePreview(preview)
	PreviewManager.active = preview
end

function PreviewManager.clearActivePreview()
	PreviewManager.active = nil -- so desanexa; nao limpa o estado
end

-- preview anexado mas invalido conta como "sem preview"
function PreviewManager.hasActivePreview()
	return PreviewManager.active ~= nil and PreviewManager.active.isValid
end

function PreviewManager.getPreviewItemsAt(x, y, z)
	if not PreviewManager.hasActivePreview() then
		return {}
	end
	local preview = PreviewManager.active
	local out = {}
	local bucket = preview.index[posKey(x, y, z)]
	if bucket then
		for _, idx in ipairs(bucket) do
			if idx <= #preview.items then
				out[#out + 1] = preview.items[idx]
			end
		end
	end
	return out
end

-- ============================================================================
-- Engine
-- ============================================================================

local Engine = {}
Engine.__index = Engine

local function newPreviewState()
	return {
		items = {},
		totalItemsPlaced = 0,
		itemCountById = {},
		placementsByRule = {},
		seed = 0,
		isValid = false,
		errorMessage = "",
		index = {}, -- posKey -> array de indices em items
		minPos = { x = 0, y = 0, z = 0 },
		maxPos = { x = 0, y = 0, z = 0 },
	}
end

-- mapAdapter nil = sem editor (apenas virtual). O adapter real envolve app.map.
function Engine.new(mapAdapter)
	return setmetatable({
		map = mapAdapter,
		preset = Defaults.preset(),
		area = Defaults.area(),
		preview = newPreviewState(),
		spatialHash = SpatialHash.new(),
		clusterCenters = {},
		friendCache = {},
		rng = MT.new(0),
		currentSeed = 0,
		virtualPreview = false,
		previewWasCapped = false,
		lastError = "",
		lastApplied = {},
	}, Engine)
end

-- copia profunda (o C++ copia por valor; sem isso, sortRulesByPriority
-- reordenaria o preset do CHAMADOR, ex. o preset persistido no storage)
local function deepCopy(v)
	if type(v) ~= "table" then
		return v
	end
	local out = {}
	for k, val in pairs(v) do
		out[k] = deepCopy(val)
	end
	return out
end

function Engine:setArea(area)
	self.area = deepCopy(area)
	self:clearPreview()
end

function Engine:setPreset(preset)
	self:clearPreview()
	self.preset = deepCopy(preset)
	sortRulesByPriority(self.preset)
end

function Engine:clearPreview()
	PreviewManager.clearActivePreview()
	self.preview = newPreviewState()
	self.spatialHash:clear()
	self.clusterCenters = {}
	self.friendCache = {}
	self.virtualPreview = false
	self.previewWasCapped = false
	-- [as-is] lastApplied/lastError/currentSeed NAO sao limpos aqui
end

-- itens do preview em uma posicao (PreviewState::hasItemAt/getItemsAt)
function Engine:hasItemAt(x, y, z)
	local bucket = self.preview.index[posKey(x, y, z)]
	return bucket ~= nil and #bucket > 0
end

function Engine:getItemsAt(x, y, z)
	local out = {}
	local bucket = self.preview.index[posKey(x, y, z)]
	if bucket then
		for _, idx in ipairs(bucket) do
			if idx <= #self.preview.items then
				out[#out + 1] = self.preview.items[idx]
			end
		end
	end
	return out
end

-- ---------------------------------------------------------------------------
-- collectTileData (C++ 989-1057)
-- ---------------------------------------------------------------------------

local function buildFriendRanges(preset, dedupe)
	local ranges, seen = {}, {}
	for _, rule in ipairs(preset.floorRules) do
		if rule.enabled and rule.friendChance > 0 and hasFriendFloor(rule) then
			local from = isFriendRange(rule) and rule.friendFromFloorId or rule.friendFloorId
			local to = isFriendRange(rule) and rule.friendToFloorId or rule.friendFloorId
			if from ~= 0 and to ~= 0 then
				if from > to then
					from, to = to, from
				end
				local key = from * 65536 + to
				if not dedupe or not seen[key] then
					seen[key] = true
					ranges[#ranges + 1] = { from = from, to = to, key = key }
				end
			end
		end
	end
	return ranges
end

function Engine:collectTileData()
	if not self.map then
		return nil
	end
	local friendRanges = buildFriendRanges(self.preset, false)
	local hasAnyClusterRules = false
	for _, rule in ipairs(self.preset.floorRules) do
		if rule.enabled and rule.ruleMode == "cluster" then
			hasAnyClusterRules = true
			break
		end
	end

	local tiles = {}
	local positions = AreaOps.positions(self.area, self.map)
	for i, pos in ipairs(positions) do
		if self.map:exists(pos.x, pos.y, pos.z) then
			local gid = self.map:groundId(pos.x, pos.y, pos.z)
			if not gid then
				-- [as-is] sem ground: incluido com sentinela 0 apenas se ha
				-- cluster rule habilitada
				if hasAnyClusterRules then
					tiles[#tiles + 1] = { pos = pos, groundId = 0 }
				end
			else
				local isFriendGround = false
				for _, r in ipairs(friendRanges) do
					if gid >= r.from and gid <= r.to then
						isFriendGround = true
						break
					end
				end
				-- [as-is] cluster rule presente desliga o filtro de blocked
				-- para TODOS os tiles
				local skip = self.preset.skipBlockedTiles
					and self.map:isBlocking(pos.x, pos.y, pos.z)
					and not isFriendGround
					and not hasAnyClusterRules
				if not skip then
					tiles[#tiles + 1] = { pos = pos, groundId = gid }
				end
			end
		end
		if i % 1024 == 0 then
			yield()
		end
	end
	return tiles
end

-- ---------------------------------------------------------------------------
-- Friend distance cache + bias (C++ 745-987)
-- ---------------------------------------------------------------------------

local BFS_OFFSETS = {
	{ -1, -1 }, { 0, -1 }, { 1, -1 },
	{ -1, 0 }, { 1, 0 },
	{ -1, 1 }, { 0, 1 }, { 1, 1 },
}

function Engine:buildFriendDistanceCache(tiles)
	self.friendCache = {}
	local ranges = buildFriendRanges(self.preset, true)
	if #ranges == 0 then
		return
	end

	-- bounds por z (sobre TODOS os tiles, sentinelas inclusive)
	local boundsByZ = {}
	for _, t in ipairs(tiles) do
		local b = boundsByZ[t.pos.z]
		if not b then
			boundsByZ[t.pos.z] = { minX = t.pos.x, maxX = t.pos.x, minY = t.pos.y, maxY = t.pos.y }
		else
			b.minX = math.min(b.minX, t.pos.x)
			b.maxX = math.max(b.maxX, t.pos.x)
			b.minY = math.min(b.minY, t.pos.y)
			b.maxY = math.max(b.maxY, t.pos.y)
		end
	end

	-- seeds por (key, z)
	local friendPositions = {} -- [key][z] = array de pos
	if self.map and not self.virtualPreview then
		-- modo mapa real: bounds GANHAM padding (o grid do BFS e o rect
		-- com padding) e o scan le o mapa
		for z, b in pairs(boundsByZ) do
			b.minX = b.minX - FRIEND_PADDING
			b.minY = b.minY - FRIEND_PADDING
			b.maxX = b.maxX + FRIEND_PADDING
			b.maxY = b.maxY + FRIEND_PADDING
			for y = b.minY, b.maxY do
				for x = b.minX, b.maxX do
					local gid = self.map:groundId(x, y, z)
					if gid then
						for _, r in ipairs(ranges) do
							if gid >= r.from and gid <= r.to then
								friendPositions[r.key] = friendPositions[r.key] or {}
								friendPositions[r.key][z] = friendPositions[r.key][z] or {}
								local list = friendPositions[r.key][z]
								list[#list + 1] = { x = x, y = y, z = z }
							end
						end
					end
				end
				yield()
			end
		end
	else
		-- modo virtual: sem padding; seeds derivadas dos proprios tiles
		for _, t in ipairs(tiles) do
			for _, r in ipairs(ranges) do
				if t.groundId >= r.from and t.groundId <= r.to then
					friendPositions[r.key] = friendPositions[r.key] or {}
					friendPositions[r.key][t.pos.z] = friendPositions[r.key][t.pos.z] or {}
					local list = friendPositions[r.key][t.pos.z]
					list[#list + 1] = t.pos
				end
			end
		end
	end

	for _, r in ipairs(ranges) do
		local perZ = friendPositions[r.key]
		if perZ then
			for z, positions in pairs(perZ) do
				local b = boundsByZ[z]
				if b then
					local width = b.maxX - b.minX + 1
					local height = b.maxY - b.minY + 1
					if width > 0 and height > 0 then
						local dist = {}
						for i = 1, width * height do
							dist[i] = -1
						end
						local layer = { minX = b.minX, minY = b.minY, width = width, height = height, dist = dist }
						local queue, head = {}, 1
						for _, p in ipairs(positions) do
							local lx, ly = p.x - b.minX, p.y - b.minY
							if lx >= 0 and ly >= 0 and lx < width and ly < height then
								local idx = ly * width + lx + 1
								if dist[idx] ~= 0 then
									dist[idx] = 0
									queue[#queue + 1] = { p.x, p.y }
								end
							end
						end
						-- BFS 8-conectado; distancia atribuida no ENQUEUE
						while head <= #queue do
							local c = queue[head]
							head = head + 1
							local base = dist[(c[2] - b.minY) * width + (c[1] - b.minX) + 1]
							for _, o in ipairs(BFS_OFFSETS) do
								local nx, ny = c[1] + o[1], c[2] + o[2]
								if nx >= b.minX and nx <= b.maxX and ny >= b.minY and ny <= b.maxY then
									local nIdx = (ny - b.minY) * width + (nx - b.minX) + 1
									if dist[nIdx] == -1 then
										dist[nIdx] = base + 1
										queue[#queue + 1] = { nx, ny }
									end
								end
							end
							if head % 2048 == 0 then
								yield()
							end
						end
						self.friendCache[r.key] = self.friendCache[r.key] or {}
						self.friendCache[r.key][z] = layer
					end
				end
			end
		end
	end
end

function Engine:getFriendDistance(key, pos)
	local perZ = self.friendCache[key]
	if not perZ then
		return -1
	end
	local layer = perZ[pos.z]
	if not layer then
		return -1
	end
	if pos.x < layer.minX or pos.x >= layer.minX + layer.width
		or pos.y < layer.minY or pos.y >= layer.minY + layer.height then
		return -1
	end
	return layer.dist[(pos.y - layer.minY) * layer.width + (pos.x - layer.minX) + 1] or -1
end

-- densidade * ((1 - c) + c * proximity^e); nunca AUMENTA acima da base
function Engine:applyFriendBias(rule, pos, baseDensity)
	if not rule or rule.friendChance <= 0 or not hasFriendFloor(rule) then
		return baseDensity
	end
	local from = isFriendRange(rule) and rule.friendFromFloorId or rule.friendFloorId
	local to = isFriendRange(rule) and rule.friendToFloorId or rule.friendFloorId
	if from == 0 or to == 0 then
		return baseDensity
	end
	if from > to then
		from, to = to, from
	end
	local distance = self:getFriendDistance(from * 65536 + to, pos)
	if distance < 0 then
		return baseDensity
	end
	local fc = rule.friendChance / 100.0
	if fc < 0 then
		fc = 0
	elseif fc > 1 then
		fc = 1
	end
	local proximity = 1.0 / (1.0 + distance)
	if rule.friendStrength > 0 then
		proximity = proximity ^ (1.0 + rule.friendStrength / 20.0)
	end
	return baseDensity * (1.0 - fc) + (baseDensity * proximity) * fc
end

-- ---------------------------------------------------------------------------
-- Spacing / validacao de tile (C++ 1059-1108)
-- ---------------------------------------------------------------------------

function Engine:checkSpacing(x, y, z, itemId)
	local spacing = self.preset.spacing
	local maxRadius = math.max(spacing.minDistance, spacing.minSameItemDistance)
	local nearby = self.spatialHash:queryRadius(x, y, maxRadius)
	for _, idx in ipairs(nearby) do
		local existing = self.preview.items[idx]
		if existing and existing.z == z then
			local dx = math.abs(x - existing.x)
			local dy = math.abs(y - existing.y)
			local distance
			if spacing.checkDiagonals then
				distance = math.max(dx, dy)
			else
				distance = dx + dy
			end
			if distance < spacing.minDistance then
				return false
			end
			if itemId == existing.itemId and distance < spacing.minSameItemDistance then
				return false
			end
		end
	end
	return true
end

-- [as-is] itemId nao e usado; so existencia do tile + blocking
function Engine:validateTilePlacement(x, y, z)
	if self.virtualPreview then
		return true
	end
	if not self.map then
		return false
	end
	if not self.map:exists(x, y, z) then
		return false
	end
	if self.preset.skipBlockedTiles and self.map:isBlocking(x, y, z) then
		return false
	end
	return true
end

-- ---------------------------------------------------------------------------
-- buildPlacementItems / spacing / commit / selecao (C++ 1110-1303)
-- ---------------------------------------------------------------------------

-- retorna array de PreviewItems ou nil (falha); parciais sao descartados
function Engine:buildPlacementItems(basePos, entry, rule)
	local out = {}
	local placedPositions = {}

	local function addBorder(pos)
		if rule and rule.borderItemId > 0 then
			out[#out + 1] = { x = pos.x, y = pos.y, z = pos.z, itemId = rule.borderItemId, rule = rule }
		end
	end

	-- item simples: SEM requireGround e SEM checagem de tile ocupado ([as-is])
	if not entry.isComposite then
		if entry.itemId == 0 then
			return nil
		end
		if not self:validateTilePlacement(basePos.x, basePos.y, basePos.z) then
			return nil
		end
		out[#out + 1] = { x = basePos.x, y = basePos.y, z = basePos.z, itemId = entry.itemId, rule = rule }
		addBorder(basePos)
		return out
	end

	if #entry.compositeTiles == 0 then
		return nil
	end

	local function appendCompositeAt(origin)
		for _, ct in ipairs(entry.compositeTiles) do
			if #ct.itemIds > 0 then
				local px = origin.x + ct.offset.x
				local py = origin.y + ct.offset.y
				local pz = origin.z + ct.offset.z
				local skipTile = false
				-- requireGround: pula SO este tile do composite
				if rule and rule.requireGround and not self.virtualPreview and self.map then
					if not self.map:groundId(px, py, pz) then
						skipTile = true
					end
				end
				if not skipTile then
					-- tile com itens empilhados REJEITA o composite inteiro
					if not self.virtualPreview and self.map then
						if self.map:exists(px, py, pz) and self.map:hasStackedItems(px, py, pz) then
							return false
						end
					end
					local validateId = 0
					for _, id in ipairs(ct.itemIds) do
						if id > 0 then
							validateId = id
							break
						end
					end
					if validateId ~= 0 then
						if not self:validateTilePlacement(px, py, pz) then
							return false
						end
						local added = false
						for _, id in ipairs(ct.itemIds) do
							if id > 0 then
								out[#out + 1] = { x = px, y = py, z = pz, itemId = id, rule = rule }
								added = true
							end
						end
						if added then
							placedPositions[#placedPositions + 1] = { x = px, y = py, z = pz }
						end
					end
				end
			end
		end
		return true
	end

	if not entry.isCluster then
		if not appendCompositeAt(basePos) then
			return nil
		end
		for _, pos in ipairs(placedPositions) do
			addBorder(pos)
		end
		if #out == 0 then
			return nil
		end
		return out
	end

	-- cluster por entrada: 1o centro sempre (0,0,0); extras sorteados
	local count = math.max(1, entry.clusterCount)
	local radius = math.max(0, entry.clusterRadius)
	local minDist = math.max(0, entry.clusterMinDistance)
	local centers = { { 0, 0 } }
	for _ = 2, count do
		local placed = false
		for _ = 1, CLUSTER_ATTEMPTS do
			local dx = self.rng:int(-radius, radius)
			local dy = self.rng:int(-radius, radius)
			if not (dx == 0 and dy == 0) then
				local tooClose = false
				for _, c in ipairs(centers) do
					if chebyshev(c[1], c[2], dx, dy) < minDist then
						tooClose = true
						break
					end
				end
				if not tooClose then
					centers[#centers + 1] = { dx, dy }
					placed = true
					break
				end
			end
		end
		if not placed then
			break -- [as-is] aborta TODOS os centros restantes
		end
	end
	for _, c in ipairs(centers) do
		local origin = { x = basePos.x + c[1], y = basePos.y + c[2], z = basePos.z }
		if not appendCompositeAt(origin) then
			return nil
		end
	end
	for _, pos in ipairs(placedPositions) do
		addBorder(pos)
	end
	if #out == 0 then
		return nil
	end
	return out
end

function Engine:checkSpacingForPlacement(items)
	for _, it in ipairs(items) do
		if not self:checkSpacing(it.x, it.y, it.z, it.itemId) then
			return false
		end
	end
	return true
end

function Engine:commitPlacement(items)
	local preview = self.preview
	for _, it in ipairs(items) do
		preview.items[#preview.items + 1] = it
		self.spatialHash:insert(it.x, it.y, #preview.items)
		preview.totalItemsPlaced = preview.totalItemsPlaced + 1
		preview.itemCountById[it.itemId] = (preview.itemCountById[it.itemId] or 0) + 1
	end
	if #items > 0 then
		local rule = items[1].rule
		preview.placementsByRule[rule] = (preview.placementsByRule[rule] or 0) + 1
	end
end

-- um sorteio uniforme [0, totalWeight-1]; cumulativo com roll < cumulative
function Engine:selectItemFromRule(rule)
	if not rule or #rule.items == 0 then
		return nil
	end
	local total = 0
	for _, item in ipairs(rule.items) do
		total = total + item.weight
	end
	if total <= 0 then
		return nil -- sem sorteio (muda a sequencia downstream, como no C++)
	end
	local roll = self.rng:int(0, total - 1)
	local cumulative = 0
	for _, item in ipairs(rule.items) do
		cumulative = cumulative + item.weight
		if roll < cumulative then
			return item
		end
	end
	return rule.items[#rule.items]
end

function Engine:checkClusterCenterSpacing(pos, minDistance)
	if minDistance <= 0 then
		return true
	end
	for _, c in ipairs(self.clusterCenters) do
		if c.z == pos.z and chebyshev(c.x, c.y, pos.x, pos.y) < minDistance then
			return false
		end
	end
	return true
end

-- ---------------------------------------------------------------------------
-- Pipeline por regra (compartilhado pelas 3 distribuicoes) (C++ spec)
-- ---------------------------------------------------------------------------

-- retorna "capped" para abortar o gerador inteiro
function Engine:tryPlaceAt(pos, groundId, rulePlacements, densityFn)
	local matching = getMatchingRules(self.preset, groundId)
	for _, rule in ipairs(matching) do
		-- (1) cap global (re-checado por regra)
		if self.preset.maxItemsTotal >= 0 and self.preview.totalItemsPlaced >= self.preset.maxItemsTotal then
			self.previewWasCapped = true
			return "capped"
		end
		-- (2) cap por regra (conta COMMITS, nao itens)
		local placed = rulePlacements[rule] or 0
		if not (rule.maxPlacements >= 0 and placed >= rule.maxPlacements) then
			-- (3)+(4) densidade e gate. O C++ escreve como SKIP (draw >
			-- density => continue); com density NaN o C++ NAO pula —
			-- espelhamos a condicao negada para manter essa semantica
			local adjusted = densityFn(rule)
			if not (self.rng:float() > adjusted) then
				-- (5) escolha ponderada
				local selected = self:selectItemFromRule(rule)
				if selected then
					-- (6) spacing de ancora p/ cluster ENTRIES
					local ok = true
					if selected.isCluster then
						local r = math.max(0, selected.clusterRadius)
						local d = math.max(0, selected.clusterMinDistance)
						ok = self:checkClusterCenterSpacing(pos, r + d)
					end
					if ok then
						-- (7) build (consome RNG mesmo se falhar depois)
						local items = self:buildPlacementItems(pos, selected, rule)
						if items then
							-- (8) lookahead: estrito >, NAO liga o flag [as-is]
							if not (self.preset.maxItemsTotal >= 0
								and self.preview.totalItemsPlaced + #items > self.preset.maxItemsTotal) then
								-- (9) spacing
								if self:checkSpacingForPlacement(items) then
									-- (10) commit
									self:commitPlacement(items)
									if selected.isCluster then
										self.clusterCenters[#self.clusterCenters + 1] =
											{ x = pos.x, y = pos.y, z = pos.z }
									end
									rulePlacements[rule] = placed + 1
								end
							end
						end
					end
				end
			end
		end
		-- sem break: varias regras podem colocar no MESMO tile
	end
	return nil
end

-- ---------------------------------------------------------------------------
-- Distribuicoes (C++ 1305-1625)
-- ---------------------------------------------------------------------------

function Engine:generatePureRandom(tiles)
	local rulePlacements = {}
	local indices = {}
	for i = 1, #tiles do
		indices[i] = i
	end
	self.rng:shuffle(indices)
	for n, idx in ipairs(indices) do
		if self.preset.maxItemsTotal >= 0 and self.preview.totalItemsPlaced >= self.preset.maxItemsTotal then
			self.previewWasCapped = true
			break
		end
		local t = tiles[idx]
		local result = self:tryPlaceAt(t.pos, t.groundId, rulePlacements, function(rule)
			return self:applyFriendBias(rule, t.pos, rule.density)
		end)
		if result == "capped" then
			return
		end
		if n % 256 == 0 then
			yield()
		end
	end
end

function Engine:generateClustered(tiles)
	if #tiles == 0 then
		return
	end
	local rulePlacements = {}
	local cfg = self.preset.distribution
	local centers = {}
	local numClusters = math.min(cfg.clusterCount, #tiles)
	for _ = 1, numClusters do
		centers[#centers + 1] = tiles[self.rng:int(1, #tiles)].pos
	end
	-- score = distancia euclidiana ate o centro mais proximo (z ignorado).
	-- Sentinela FINITA (FLT_MAX do C++): com zero centros e strength 0,
	-- exp(-FLT_MAX*0) = 1 (densidade cheia); math.huge daria NaN e nada
	-- seria colocado — o oposto do C++.
	local FLT_MAX = 3.402823466e38
	local scores = {}
	for i, t in ipairs(tiles) do
		local minDist = FLT_MAX
		for _, c in ipairs(centers) do
			local dx = t.pos.x - c.x
			local dy = t.pos.y - c.y
			local d = math.sqrt(dx * dx + dy * dy)
			if d < minDist then
				minDist = d
			end
		end
		scores[#scores + 1] = { d = minDist, i = i }
	end
	table.sort(scores, function(a, b)
		if a.d ~= b.d then
			return a.d < b.d
		end
		return a.i < b.i
	end)
	for n, s in ipairs(scores) do
		if self.preset.maxItemsTotal >= 0 and self.preview.totalItemsPlaced >= self.preset.maxItemsTotal then
			self.previewWasCapped = true
			break
		end
		local t = tiles[s.i]
		local result = self:tryPlaceAt(t.pos, t.groundId, rulePlacements, function(rule)
			local adjusted = rule.density
			-- [as-is] friend bias aplicado SOBRE o falloff (so neste modo)
			if s.d > 0 then
				adjusted = adjusted * math.exp(-s.d * cfg.clusterStrength * 0.1)
			end
			return self:applyFriendBias(rule, t.pos, adjusted)
		end)
		if result == "capped" then
			return
		end
		if n % 256 == 0 then
			yield()
		end
	end
end

function Engine:generateGridBased(tiles)
	if #tiles == 0 then
		return
	end
	local rulePlacements = {}
	local cfg = self.preset.distribution
	-- divergencia documentada: clamp para evitar loop infinito (C++ trava
	-- com spacing <= 0) e jitter negativo (UB no C++)
	local spacingX = math.max(1, cfg.gridSpacingX)
	local spacingY = math.max(1, cfg.gridSpacingY)
	local jitter = math.max(0, cfg.gridJitter)

	local tileMap = {}
	local minX, maxX = tiles[1].pos.x, tiles[1].pos.x
	local minY, maxY = tiles[1].pos.y, tiles[1].pos.y
	local z = tiles[1].pos.z -- [as-is] so o andar do 1o tile e sondado
	for _, t in ipairs(tiles) do
		tileMap[posKey(t.pos.x, t.pos.y, t.pos.z)] = t
		minX = math.min(minX, t.pos.x)
		maxX = math.max(maxX, t.pos.x)
		minY = math.min(minY, t.pos.y)
		maxY = math.max(maxY, t.pos.y)
	end

	local gx = minX
	while gx <= maxX do
		local gy = minY
		while gy <= maxY do
			if self.preset.maxItemsTotal >= 0 and self.preview.totalItemsPlaced >= self.preset.maxItemsTotal then
				self.previewWasCapped = true
				return
			end
			-- jitter SEMPRE consumido (X depois Y), mesmo em probe perdido
			local px = gx + self.rng:int(-jitter, jitter)
			local py = gy + self.rng:int(-jitter, jitter)
			local t = tileMap[posKey(px, py, z)]
			if t then
				local result = self:tryPlaceAt(t.pos, t.groundId, rulePlacements, function(rule)
					return self:applyFriendBias(rule, t.pos, rule.density)
				end)
				if result == "capped" then
					return
				end
			end
			gy = gy + spacingY
		end
		gx = gx + spacingX
		yield()
	end
end

-- ---------------------------------------------------------------------------
-- Cluster rules (RuleMode cluster) (C++ 1596-1922)
-- ---------------------------------------------------------------------------

-- pattern como sub-multiset da pilha do tile (ground + itens)
function Engine:tileMatchesClusterPattern(x, y, z, patternIds)
	if not self.map:exists(x, y, z) then
		return false
	end
	local remaining = self.map:stackIds(x, y, z)
	for _, pid in ipairs(patternIds) do
		if pid > 0 then
			local found = nil
			for i, id in ipairs(remaining) do
				if id == pid then
					found = i
					break
				end
			end
			if not found then
				return false
			end
			table.remove(remaining, found)
		end
	end
	return true
end

-- candidatos: posicoes da area onde o footprint inteiro cabe na area e (em
-- modo real) o pattern existe no mapa
function Engine:collectClusterCandidates(tiles, rule, anchorOff)
	local valid = {}
	for _, t in ipairs(tiles) do
		valid[posKey(t.pos.x, t.pos.y, t.pos.z)] = true
	end
	local candidates = {}
	for n, t in ipairs(tiles) do
		local allMatch = true
		for _, ct in ipairs(rule.clusterTiles) do
			local ax = t.pos.x + (ct.offset.x - anchorOff.x)
			local ay = t.pos.y + (ct.offset.y - anchorOff.y)
			local az = t.pos.z + (ct.offset.z - anchorOff.z)
			if not valid[posKey(ax, ay, az)] then
				allMatch = false
				break
			end
			if self.map and not self.virtualPreview then
				if #ct.itemIds > 0 and not self:tileMatchesClusterPattern(ax, ay, az, ct.itemIds) then
					allMatch = false
					break
				end
			end
		end
		if allMatch then
			candidates[#candidates + 1] = t.pos
		end
		if n % 256 == 0 then
			yield()
		end
	end
	return candidates
end

local function instanceTooClose(centers, pos, minDistance)
	if minDistance <= 0 then
		return false
	end
	for _, c in ipairs(centers) do
		if c.z == pos.z and chebyshev(c.x, c.y, pos.x, pos.y) < minDistance then
			return true
		end
	end
	return false
end

function Engine:generateClusterCentered(tiles, rule)
	if #rule.clusterTiles == 0 or #tiles == 0 or #rule.items == 0 then
		return
	end
	if not rule.hasCenterPoint then
		return
	end
	if not self.map and not self.virtualPreview then
		return
	end

	local candidates = self:collectClusterCandidates(tiles, rule, rule.centerOffset)
	if #candidates == 0 then
		return
	end
	self.rng:shuffle(candidates)

	local instanceCenters = {}
	local instancesPlaced = 0
	for _, centerPos in ipairs(candidates) do
		if instancesPlaced >= rule.instanceCount then
			break
		end
		if self.preset.maxItemsTotal >= 0 and self.preview.totalItemsPlaced >= self.preset.maxItemsTotal then
			self.previewWasCapped = true
			break
		end
		if not instanceTooClose(instanceCenters, centerPos, rule.instanceMinDistance) then
			local selected = self:selectItemFromRule(rule)
			if selected then
				local basePos = { x = centerPos.x, y = centerPos.y, z = centerPos.z }
				if selected.isComposite and selected.hasCenterPoint then
					basePos.x = basePos.x - selected.centerOffset.x
					basePos.y = basePos.y - selected.centerOffset.y
					basePos.z = basePos.z - selected.centerOffset.z
				end
				local valid = true
				if not self.virtualPreview then
					valid = self:validateTilePlacement(basePos.x, basePos.y, basePos.z)
				end
				if valid then
					local items = self:buildPlacementItems(basePos, selected, rule)
					if items and #items > 0 then
						if not (self.preset.maxItemsTotal >= 0
							and self.preview.totalItemsPlaced + #items > self.preset.maxItemsTotal) then
							if self:checkSpacingForPlacement(items) then
								self:commitPlacement(items)
								instanceCenters[#instanceCenters + 1] = centerPos
								instancesPlaced = instancesPlaced + 1
							end
						end
					end
				end
			end
		end
	end
	-- [as-is] modo centered ignora rule.density e rule.maxPlacements
end

function Engine:generateClusterRandom(tiles, rule)
	if #rule.clusterTiles == 0 or #tiles == 0 or #rule.items == 0 then
		return
	end
	if not self.map and not self.virtualPreview then
		return
	end

	local anchorOff = rule.clusterTiles[1].offset
	local candidates = self:collectClusterCandidates(tiles, rule, anchorOff)
	if #candidates == 0 then
		return
	end
	self.rng:shuffle(candidates)

	local instanceAnchors = {}
	local instancesPlaced = 0
	for _, anchorPos in ipairs(candidates) do
		if instancesPlaced >= rule.instanceCount then
			break
		end
		if self.preset.maxItemsTotal >= 0 and self.preview.totalItemsPlaced >= self.preset.maxItemsTotal then
			self.previewWasCapped = true
			break
		end
		if not instanceTooClose(instanceAnchors, anchorPos, rule.instanceMinDistance) then
			local placementItems = {}
			for _, ct in ipairs(rule.clusterTiles) do
				local ax = anchorPos.x + (ct.offset.x - anchorOff.x)
				local ay = anchorPos.y + (ct.offset.y - anchorOff.y)
				local az = anchorPos.z + (ct.offset.z - anchorOff.z)
				-- densidade POR TILE do template, sorteada antes da selecao
				-- (condicao de skip negada: preserva semantica de NaN)
				if not (self.rng:float() > rule.density) then
					local selected = self:selectItemFromRule(rule)
					if selected then
						local basePos = { x = ax, y = ay, z = az }
						if selected.isComposite and selected.hasCenterPoint then
							basePos.x = basePos.x - selected.centerOffset.x
							basePos.y = basePos.y - selected.centerOffset.y
							basePos.z = basePos.z - selected.centerOffset.z
						end
						local valid = true
						if not self.virtualPreview then
							valid = self:validateTilePlacement(basePos.x, basePos.y, basePos.z)
						end
						if valid then
							local items = self:buildPlacementItems(basePos, selected, rule)
							if items then
								for _, it in ipairs(items) do
									placementItems[#placementItems + 1] = it
								end
							end
						end
					end
				end
			end
			if #placementItems > 0 then
				if not (self.preset.maxItemsTotal >= 0
					and self.preview.totalItemsPlaced + #placementItems > self.preset.maxItemsTotal) then
					-- [as-is] modo random NAO checa SpacingConfig
					self:commitPlacement(placementItems)
					instanceAnchors[#instanceAnchors + 1] = anchorPos
					instancesPlaced = instancesPlaced + 1
				end
			end
		end
	end
end

-- ---------------------------------------------------------------------------
-- Orquestracao: generatePreview / virtual / reroll / apply / remove
-- (C++ 549-745, 1924-2074)
-- ---------------------------------------------------------------------------

local function rebuildPreviewIndex(preview)
	preview.index = {}
	for i, it in ipairs(preview.items) do
		local key = posKey(it.x, it.y, it.z)
		local bucket = preview.index[key]
		if not bucket then
			bucket = {}
			preview.index[key] = bucket
		end
		bucket[#bucket + 1] = i
	end
	if #preview.items > 0 then
		local first = preview.items[1]
		local mn = { x = first.x, y = first.y, z = first.z }
		local mx = { x = first.x, y = first.y, z = first.z }
		for _, it in ipairs(preview.items) do
			mn.x = math.min(mn.x, it.x)
			mn.y = math.min(mn.y, it.y)
			mn.z = math.min(mn.z, it.z)
			mx.x = math.max(mx.x, it.x)
			mx.y = math.max(mx.y, it.y)
			mx.z = math.max(mx.z, it.z)
		end
		preview.minPos, preview.maxPos = mn, mx
	end
end

-- equivalente de std::random_device (entropia nao-deterministica)
local function entropySeed()
	local seed = (os.time() * 7919 + math.floor((os.clock() * 1000) % 65536)) % 4294967296
	if seed == 0 then
		seed = 1
	end
	return seed
end

local function resolveSeed(self, seed)
	if seed == 0 then
		if self.preset.defaultSeed ~= 0 then
			seed = self.preset.defaultSeed
		else
			seed = entropySeed()
		end
	end
	return seed
end

local function runGeneration(self, tiles)
	self.spatialHash:clear()
	self:buildFriendDistanceCache(tiles)

	-- [as-is] switch sem default: mode fora de 0..2 nao roda distribuicao
	-- nenhuma (XML nao valida o campo)
	local mode = self.preset.distribution.mode
	if mode == 0 then
		self:generatePureRandom(tiles)
	elseif mode == 1 then
		self:generateClustered(tiles)
	elseif mode == 2 then
		self:generateGridBased(tiles)
	end

	-- passada separada de cluster rules, na ordem (pos-sort) das regras
	for _, rule in ipairs(self.preset.floorRules) do
		if rule.enabled and rule.ruleMode == "cluster" then
			if rule.hasCenterPoint then
				self:generateClusterCentered(tiles, rule)
			else
				self:generateClusterRandom(tiles, rule)
			end
		end
	end

	rebuildPreviewIndex(self.preview)
	self.preview.isValid = true
end

function Engine:generatePreview(seed)
	self:clearPreview()
	seed = resolveSeed(self, seed or 0)
	self.currentSeed = seed
	self.rng = MT.new(seed % 4294967296)
	self.preview.seed = seed

	local ok, err = validatePreset(self.preset)
	if not ok then
		self.lastError = "Invalid preset: " .. err
		self.preview.errorMessage = self.lastError
		return false
	end
	local tiles = self:collectTileData()
	if not tiles then
		self.lastError = "Failed to collect tile data from area"
		self.preview.errorMessage = self.lastError
		return false
	end
	if #tiles == 0 then
		self.lastError = "No valid tiles found in selected area"
		self.preview.errorMessage = self.lastError
		return false
	end

	runGeneration(self, tiles)
	-- registra para renderizacao (o virtual NAO registra — [as-is])
	PreviewManager.setActivePreview(self.preview)
	return true
end

function Engine:generatePreviewVirtual(width, height, groundId, seed)
	self:clearPreview()
	self.virtualPreview = true
	seed = resolveSeed(self, seed or 0)
	self.currentSeed = seed
	self.rng = MT.new(seed % 4294967296)
	self.preview.seed = seed

	local ok, err = validatePreset(self.preset)
	if not ok then
		self.lastError = "Invalid preset: " .. err
		self.preview.errorMessage = self.lastError
		return false
	end
	-- [as-is] checagem de parametros vem DEPOIS da validacao do preset
	if width <= 0 or height <= 0 or groundId == 0 then
		self.lastError = "Invalid virtual preview configuration"
		self.preview.errorMessage = self.lastError
		return false
	end

	local tiles = {}
	for y = 0, height - 1 do
		for x = 0, width - 1 do
			tiles[#tiles + 1] = { pos = { x = x, y = y, z = 0 }, groundId = groundId }
		end
	end

	runGeneration(self, tiles)
	return true
end

-- [as-is] reroll IGNORA preset.defaultSeed (sorteia entropia e passa direto)
function Engine:rerollPreview()
	return self:generatePreview(entropySeed())
end

-- aplica o preview no mapa real numa unica transacao (undo de 1 passo)
function Engine:applyPreview()
	if not self.preview.isValid or #self.preview.items == 0 then
		self.lastError = "No valid preview to apply"
		return false
	end
	if not self.map then
		self.lastError = "No editor available"
		return false
	end

	-- agrupa por posicao preservando a ordem interna dos itens
	local groups, orderKeys = {}, {}
	for _, it in ipairs(self.preview.items) do
		local key = posKey(it.x, it.y, it.z)
		local g = groups[key]
		if not g then
			g = {}
			groups[key] = g
			orderKeys[#orderKeys + 1] = key
		end
		g[#g + 1] = it
	end

	local applied = 0
	app.transaction("Area Decoration", function()
		for _, key in ipairs(orderKeys) do
			local g = groups[key]
			local tile = app.map:getTile(g[1].x, g[1].y, g[1].z)
			-- tiles inexistentes sao pulados em silencio ([as-is])
			if tile then
				for _, it in ipairs(g) do
					local okAdd = pcall(function()
						tile:addItem(it.itemId)
					end)
					if okAdd then
						applied = applied + 1
					end
				end
			end
		end
	end)

	if applied == 0 then
		self.lastError = "No changes were applied"
		return false
	end

	-- [as-is] snapshot inclui TODOS os itens do preview (mesmo os pulados)
	self.lastApplied = {}
	for _, it in ipairs(self.preview.items) do
		self.lastApplied[#self.lastApplied + 1] = { x = it.x, y = it.y, z = it.z, itemId = it.itemId }
	end
	self:clearPreview()
	return true
end

function Engine:removeLastApplied()
	if not self.map then
		self.lastError = "No editor available"
		return false
	end
	if #self.lastApplied == 0 then
		self.lastError = "No applied items to remove"
		return false
	end

	-- buckets por posicao com contagem por id
	local buckets, order = {}, {}
	for _, it in ipairs(self.lastApplied) do
		local key = posKey(it.x, it.y, it.z)
		local b = buckets[key]
		if not b then
			b = { x = it.x, y = it.y, z = it.z, counts = {} }
			buckets[key] = b
			order[#order + 1] = key
		end
		b.counts[it.itemId] = (b.counts[it.itemId] or 0) + 1
	end

	local anyChange = false
	app.transaction("Area Decoration (remover)", function()
		for _, key in ipairs(order) do
			local b = buckets[key]
			local tile = app.map:getTile(b.x, b.y, b.z)
			if tile then
				for id, count in pairs(b.counts) do
					-- [as-is] o GROUND e removido primeiro se o id casar
					if count > 0 and tile.ground and tile.ground.id == id then
						tile.ground = nil
						count = count - 1
						anyChange = true
					end
					-- remove os N de cima para baixo
					local stack = tile.items
					for i = #stack, 1, -1 do
						if count <= 0 then
							break
						end
						if stack[i].id == id then
							tile:removeItem(stack[i])
							count = count - 1
							anyChange = true
						end
					end
				end
			end
		end
	end)

	if not anyChange then
		-- [as-is] lastApplied NAO e limpo neste caminho de falha
		self.lastError = "No items from last apply were found to remove"
		return false
	end
	self.lastApplied = {}
	return true
end

-- ============================================================================
-- Adapter do mapa real (envolve app.map)
-- ============================================================================

local RealMap = {}
RealMap.__index = RealMap

function RealMap.new()
	return setmetatable({}, RealMap)
end

function RealMap:exists(x, y, z)
	return app.map:getTile(x, y, z) ~= nil
end

function RealMap:groundId(x, y, z)
	local tile = app.map:getTile(x, y, z)
	if tile and tile.ground then
		return tile.ground.id
	end
	return nil
end

function RealMap:isBlocking(x, y, z)
	local tile = app.map:getTile(x, y, z)
	return tile ~= nil and tile.isBlocking
end

function RealMap:hasStackedItems(x, y, z)
	local tile = app.map:getTile(x, y, z)
	return tile ~= nil and tile.itemCount > 0
end

-- pilha completa: ground primeiro (se existir), depois itens na ordem
function RealMap:stackIds(x, y, z)
	local tile = app.map:getTile(x, y, z)
	local out = {}
	if not tile then
		return out
	end
	if tile.ground then
		out[#out + 1] = tile.ground.id
	end
	for _, item in ipairs(tile.items) do
		out[#out + 1] = item.id
	end
	return out
end

-- ============================================================================
-- Importador XML (formato pugixml de DecorationPreset) (C++ 2080-2869)
-- ============================================================================

local Xml = {}

local ENTITIES = { amp = "&", lt = "<", gt = ">", quot = '"', apos = "'" }

local function unescape(s)
	return (s:gsub("&(#?x?%w+);", function(e)
		if ENTITIES[e] then
			return ENTITIES[e]
		end
		local dec = e:match("^#(%d+)$")
		if dec then
			return string.char(tonumber(dec) % 256)
		end
		local hex = e:match("^#x(%x+)$")
		if hex then
			return string.char(tonumber(hex, 16) % 256)
		end
		return "&" .. e .. ";"
	end))
end

-- fim da tag respeitando aspas (pugixml aceita '>' dentro de valores)
local function findTagEnd(xml, s)
	local i = s + 1
	local n = #xml
	local quote = nil
	while i <= n do
		local c = xml:sub(i, i)
		if quote then
			if c == quote then
				quote = nil
			end
		elseif c == '"' or c == "'" then
			quote = c
		elseif c == ">" then
			return i
		end
		i = i + 1
	end
	return nil
end

-- scanner com pilha; retorna a raiz {name, attrs, children} ou nil em XML
-- malformado (tag de fechamento errada / documento truncado), espelhando a
-- falha de parse do pugixml
function Xml.parse(xml)
	local doc = { name = "#doc", attrs = {}, children = {} }
	local stack = { doc }
	local pos = 1
	local n = #xml
	while pos <= n do
		local s = xml:find("<", pos, true)
		if not s then
			break
		end
		local nxt = xml:sub(s + 1, s + 1)
		if nxt == "?" then
			local _, e = xml:find("?>", s, true)
			if not e then
				return nil
			end
			pos = e + 1
		elseif xml:sub(s + 1, s + 3) == "!--" then
			local _, e = xml:find("-->", s, true)
			if not e then
				return nil
			end
			pos = e + 1
		elseif nxt == "/" then
			local e = findTagEnd(xml, s)
			if not e then
				return nil
			end
			local name = xml:sub(s + 2, e - 1):match("^([%w_]+)")
			if #stack <= 1 or stack[#stack].name ~= name then
				return nil -- fechamento sem par / nome errado
			end
			table.remove(stack)
			pos = e + 1
		else
			local e = findTagEnd(xml, s)
			if not e then
				return nil
			end
			local tag = xml:sub(s + 1, e - 1)
			local selfClose = tag:sub(-1) == "/"
			if selfClose then
				tag = tag:sub(1, -2)
			end
			local name = tag:match("^([%w_]+)")
			if name then
				local node = { name = name, attrs = {}, children = {} }
				for k, v in tag:gmatch('([%w_]+)%s*=%s*"([^"]*)"') do
					node.attrs[k] = unescape(v)
				end
				-- pugixml tambem aceita aspas simples
				for k, v in tag:gmatch("([%w_]+)%s*=%s*'([^']*)'") do
					if node.attrs[k] == nil then
						node.attrs[k] = unescape(v)
					end
				end
				local top = stack[#stack]
				top.children[#top.children + 1] = node
				if not selfClose then
					stack[#stack + 1] = node
				end
			end
			pos = e + 1
		end
	end
	if #stack ~= 1 then
		return nil -- documento truncado (tags abertas no EOF)
	end
	return doc
end

local function childByName(node, name)
	for _, c in ipairs(node.children) do
		if c.name == name then
			return c
		end
	end
	return nil
end

-- semantica pugixml: atributo AUSENTE -> default; presente mas nao-numerico
-- -> 0. strtol: prefixo inteiro decimal, PARA no '.' (truncamento em direcao
-- a zero; "0x10" -> 0, "1e3" -> 1)
local function strtolPrefix(v)
	local m = v:match("^%s*([-+]?%d+)")
	if m then
		return tonumber(m)
	end
	return 0
end

-- strtod: aceita ponto flutuante/expoente ("0.5abc" -> 0.5)
local function strtodPrefix(v)
	local num = tonumber(v)
	if num then
		return num
	end
	local m = v:match("^%s*([-+]?%d*%.?%d+[eE][-+]?%d+)") or v:match("^%s*([-+]?%d*%.?%d+)")
	if m then
		return tonumber(m) or 0
	end
	return 0
end

local function attrInt(node, name, default)
	local v = node.attrs[name]
	if v == nil then
		return default
	end
	return strtolPrefix(v)
end

local function attrFloat(node, name, default)
	local v = node.attrs[name]
	if v == nil then
		return default
	end
	return strtodPrefix(v)
end

-- strtoul (negativos dao a volta em 2^32; overflow SATURA em 2^32-1) +
-- truncamento uint16
local function attrU16(node, name, default)
	local v = node.attrs[name]
	if v == nil then
		return default
	end
	local num = strtolPrefix(v)
	if num < 0 then
		num = num % 4294967296
	end
	if num >= 4294967296 then
		num = 4294967295
	end
	return num % 65536
end

-- default_seed: uint64 decimal; so seed % 2^32 e consumido — acumula digito a
-- digito mod 2^32 (evita perda de precisao em double acima de 2^53)
local function parseSeed(s)
	local digits = s:match("^%s*(%d+)")
	if not digits then
		return 0
	end
	local acc = 0
	for d in digits:gmatch("%d") do
		acc = (acc * 10 + tonumber(d)) % 4294967296
	end
	return acc
end

-- pugixml as_bool: primeiro caractere em {1,t,T,y,Y}
local function attrBool(node, name, default)
	local v = node.attrs[name]
	if v == nil then
		return default
	end
	local c = v:sub(1, 1)
	return c == "1" or c == "t" or c == "T" or c == "y" or c == "Y"
end

local function attrStr(node, name, default)
	local v = node.attrs[name]
	if v == nil then
		return default
	end
	return v
end

-- <tile>/<cluster_tile>: mantidos apenas com >=1 id valido (>0)
local function parseTiles(node, tileName)
	local tiles = {}
	for _, c in ipairs(node.children) do
		if c.name == tileName then
			local ids = {}
			for _, ic in ipairs(c.children) do
				if ic.name == "item" then
					local id = attrU16(ic, "id", 0)
					if id > 0 then
						ids[#ids + 1] = id
					end
				end
			end
			if #ids > 0 then
				tiles[#tiles + 1] = {
					offset = { x = attrInt(c, "x", 0), y = attrInt(c, "y", 0), z = attrInt(c, "z", 0) },
					itemIds = ids,
				}
			end
		end
	end
	return tiles
end

function Xml.presetFromString(xml)
	local doc = Xml.parse(xml)
	if not doc then
		return nil, "XML malformado"
	end
	local root = childByName(doc, "decoration_preset")
	if not root then
		return nil, "elemento <decoration_preset> ausente"
	end

	local preset = Defaults.preset()
	preset.name = attrStr(root, "name", "Unnamed Preset")

	local node = childByName(root, "spacing")
	if node then
		preset.spacing.minDistance = attrInt(node, "min_distance", 1)
		preset.spacing.minSameItemDistance = attrInt(node, "same_item_distance", 2)
		preset.spacing.checkDiagonals = attrBool(node, "check_diagonals", true)
	end

	node = childByName(root, "distribution")
	if node then
		preset.distribution.mode = attrInt(node, "mode", 0)
		preset.distribution.clusterStrength = attrFloat(node, "cluster_strength", 0.5)
		preset.distribution.clusterCount = attrInt(node, "cluster_count", 3)
		preset.distribution.gridSpacingX = attrInt(node, "grid_spacing_x", 3)
		preset.distribution.gridSpacingY = attrInt(node, "grid_spacing_y", 3)
		preset.distribution.gridJitter = attrInt(node, "grid_jitter", 1)
	end

	node = childByName(root, "settings")
	if node then
		preset.maxItemsTotal = attrInt(node, "max_items_total", -1)
		preset.skipBlockedTiles = attrBool(node, "skip_blocked", true)
		preset.defaultSeed = parseSeed(attrStr(node, "default_seed", "0"))
	end

	preset.hasArea = false
	node = childByName(root, "area")
	if node then
		preset.hasArea = true
		local a = preset.area
		a.type = attrInt(node, "type", 0)
		a.rectMin = { x = attrInt(node, "rect_min_x", 0), y = attrInt(node, "rect_min_y", 0), z = attrInt(node, "rect_min_z", 0) }
		a.rectMax = { x = attrInt(node, "rect_max_x", 0), y = attrInt(node, "rect_max_y", 0), z = attrInt(node, "rect_max_z", 0) }
		a.floodOrigin = { x = attrInt(node, "flood_origin_x", 0), y = attrInt(node, "flood_origin_y", 0), z = attrInt(node, "flood_origin_z", 0) }
		a.floodTargetGround = attrU16(node, "flood_target_ground", 0)
		a.floodMaxRadius = attrInt(node, "flood_max_radius", 100)
	end

	preset.floorRules = {}
	local rulesNode = childByName(root, "floor_rules")
	if rulesNode then
		for _, rn in ipairs(rulesNode.children) do
			if rn.name == "rule" then
				local rule = Defaults.floorRule()
				rule.name = attrStr(rn, "name", "Rule")
				rule.floorId = attrU16(rn, "floor_id", 0)
				rule.fromFloorId = attrU16(rn, "from_floor_id", 0)
				rule.toFloorId = attrU16(rn, "to_floor_id", 0)
				-- [as-is] default de density no XML e 0.3 (struct usa 1.0)
				rule.density = attrFloat(rn, "density", 0.3)
				rule.maxPlacements = attrInt(rn, "max_placements", -1)
				rule.priority = attrInt(rn, "priority", 0)
				rule.enabled = attrBool(rn, "enabled", true)
				rule.borderItemId = attrU16(rn, "border_item_id", 0)

				-- rule_mode: "cluster"/"range" exatos; senao inferencia legada
				local mode = attrStr(rn, "rule_mode", "")
				if mode == "cluster" then
					rule.ruleMode = "cluster"
				elseif mode == "range" then
					rule.ruleMode = "range"
				elseif rule.fromFloorId > 0 and rule.toFloorId > 0 then
					rule.ruleMode = "range"
				else
					rule.ruleMode = "single"
				end

				if rule.ruleMode == "cluster" then
					rule.hasCenterPoint = attrBool(rn, "has_center", false)
					rule.centerOffset = {
						x = attrInt(rn, "center_x", 0),
						y = attrInt(rn, "center_y", 0),
						z = attrInt(rn, "center_z", 0),
					}
					rule.instanceCount = attrInt(rn, "instance_count", 1)
					rule.instanceMinDistance = attrInt(rn, "instance_min_distance", 5)
					rule.requireGround = attrBool(rn, "require_ground", true)
					rule.clusterTiles = parseTiles(rn, "cluster_tile")
				end

				-- friend: range (ambos > 0) tem precedencia e zera o single
				local ffrom = attrU16(rn, "friend_from_floor_id", 0)
				local fto = attrU16(rn, "friend_to_floor_id", 0)
				if ffrom > 0 and fto > 0 then
					rule.friendFloorId = 0
					rule.friendFromFloorId = ffrom
					rule.friendToFloorId = fto
				else
					rule.friendFloorId = attrU16(rn, "friend_floor_id", 0)
					rule.friendFromFloorId = 0
					rule.friendToFloorId = 0
				end
				rule.friendChance = attrInt(rn, "friend_chance", 0)
				rule.friendStrength = attrInt(rn, "friend_strength", 0)

				-- <items>: sequencia heterogenea, ordem do documento
				local itemsNode = childByName(rn, "items")
				if itemsNode then
					for _, en in ipairs(itemsNode.children) do
						if en.name == "item" then
							local id = attrU16(en, "id", 0)
							if id > 0 then
								rule.items[#rule.items + 1] = Defaults.itemEntry(id, attrInt(en, "weight", 100))
							end
						elseif en.name == "composite" or en.name == "cluster" then
							-- weight <= 0 cai no legado "chance" (default 100)
							local weight = attrInt(en, "weight", 0)
							if weight <= 0 then
								weight = attrInt(en, "chance", 100)
							end
							local ctiles = parseTiles(en, "tile")
							if #ctiles > 0 then
								if en.name == "cluster" then
									rule.items[#rule.items + 1] = Defaults.makeCluster(
										ctiles, weight,
										attrInt(en, "count", 3),
										attrInt(en, "radius", 3),
										attrInt(en, "min_distance", 2))
								else
									rule.items[#rule.items + 1] = Defaults.makeComposite(ctiles, weight)
								end
							end
						end
					end
				end

				preset.floorRules[#preset.floorRules + 1] = rule
			end
		end
	end

	return preset
end

-- ============================================================================
-- Serializador XML (lado de escrita; espelha toXmlString/saveToFile do C++)
-- ============================================================================

local function xmlEscape(s)
	return (tostring(s):gsub("&", "&amp;"):gsub("<", "&lt;"):gsub(">", "&gt;"):gsub('"', "&quot;"))
end

local function numStr(v)
	if v == math.floor(v) then
		return ("%d"):format(v)
	end
	return ("%.9g"):format(v)
end

local function boolStr(b)
	return b and "true" or "false"
end

function Xml.presetToString(preset)
	local out = {}
	local function w(s)
		out[#out + 1] = s
	end
	w('<?xml version="1.0"?>\n')
	w(('<decoration_preset name="%s" version="1.0">\n'):format(xmlEscape(preset.name)))

	local sp = preset.spacing
	w(('\t<spacing min_distance="%d" same_item_distance="%d" check_diagonals="%s" />\n')
		:format(sp.minDistance, sp.minSameItemDistance, boolStr(sp.checkDiagonals)))

	local di = preset.distribution
	w(('\t<distribution mode="%d" cluster_strength="%s" cluster_count="%d" grid_spacing_x="%d" grid_spacing_y="%d" grid_jitter="%d" />\n')
		:format(di.mode, numStr(di.clusterStrength), di.clusterCount, di.gridSpacingX, di.gridSpacingY, di.gridJitter))

	w(('\t<settings max_items_total="%d" skip_blocked="%s" default_seed="%s" />\n')
		:format(preset.maxItemsTotal, boolStr(preset.skipBlockedTiles), numStr(preset.defaultSeed)))

	if preset.hasArea then
		local a = preset.area
		w(('\t<area type="%d" rect_min_x="%d" rect_min_y="%d" rect_min_z="%d" rect_max_x="%d" rect_max_y="%d" rect_max_z="%d" flood_origin_x="%d" flood_origin_y="%d" flood_origin_z="%d" flood_target_ground="%d" flood_max_radius="%d" />\n')
			:format(a.type, a.rectMin.x, a.rectMin.y, a.rectMin.z, a.rectMax.x, a.rectMax.y, a.rectMax.z,
				a.floodOrigin.x, a.floodOrigin.y, a.floodOrigin.z, a.floodTargetGround, a.floodMaxRadius))
	end

	w('\t<floor_rules>\n')
	for _, rule in ipairs(preset.floorRules) do
		local attrs = ('name="%s"'):format(xmlEscape(rule.name))
		-- rule_mode escrito so para cluster/range (compat com leitores antigos)
		if rule.ruleMode == "cluster" then
			attrs = attrs .. ' rule_mode="cluster"'
		elseif rule.ruleMode == "range" then
			attrs = attrs .. ' rule_mode="range"'
		end
		attrs = attrs
			.. (' floor_id="%d" from_floor_id="%d" to_floor_id="%d" density="%s" max_placements="%d" priority="%d" enabled="%s" border_item_id="%d"')
				:format(rule.floorId, rule.fromFloorId, rule.toFloorId, numStr(rule.density),
					rule.maxPlacements, rule.priority, boolStr(rule.enabled), rule.borderItemId)
		if rule.ruleMode == "cluster" then
			attrs = attrs
				.. (' has_center="%s" center_x="%d" center_y="%d" center_z="%d" instance_count="%d" instance_min_distance="%d" require_ground="%s"')
					:format(boolStr(rule.hasCenterPoint), rule.centerOffset.x, rule.centerOffset.y,
						rule.centerOffset.z, rule.instanceCount, rule.instanceMinDistance, boolStr(rule.requireGround))
		end
		-- friend mutuamente exclusivo na escrita, como o C++
		if isFriendRange(rule) then
			attrs = attrs .. (' friend_floor_id="0" friend_from_floor_id="%d" friend_to_floor_id="%d"')
				:format(rule.friendFromFloorId, rule.friendToFloorId)
		else
			attrs = attrs .. (' friend_floor_id="%d" friend_from_floor_id="0" friend_to_floor_id="0"')
				:format(rule.friendFloorId)
		end
		attrs = attrs .. (' friend_chance="%d" friend_strength="%d"'):format(rule.friendChance, rule.friendStrength)
		w('\t\t<rule ' .. attrs .. '>\n')

		if rule.ruleMode == "cluster" then
			for _, ct in ipairs(rule.clusterTiles) do
				local valid = {}
				for _, id in ipairs(ct.itemIds) do
					if id > 0 then
						valid[#valid + 1] = id
					end
				end
				if #valid > 0 then
					w(('\t\t\t<cluster_tile x="%d" y="%d" z="%d">\n'):format(ct.offset.x, ct.offset.y, ct.offset.z))
					for _, id in ipairs(valid) do
						w(('\t\t\t\t<item id="%d" />\n'):format(id))
					end
					w('\t\t\t</cluster_tile>\n')
				end
			end
		end

		w('\t\t\t<items>\n')
		for _, e in ipairs(rule.items) do
			if not e.isComposite then
				if e.itemId > 0 then
					w(('\t\t\t\t<item id="%d" weight="%d" />\n'):format(e.itemId, e.weight))
				end
			else
				local tiles = {}
				for _, ct in ipairs(e.compositeTiles) do
					local valid = {}
					for _, id in ipairs(ct.itemIds) do
						if id > 0 then
							valid[#valid + 1] = id
						end
					end
					if #valid > 0 then
						tiles[#tiles + 1] = { offset = ct.offset, ids = valid }
					end
				end
				if #tiles > 0 then
					local tag = e.isCluster and "cluster" or "composite"
					local extra = ""
					if e.isCluster then
						extra = (' count="%d" radius="%d" min_distance="%d"')
							:format(e.clusterCount, e.clusterRadius, e.clusterMinDistance)
					end
					w(('\t\t\t\t<%s weight="%d"%s>\n'):format(tag, e.weight, extra))
					for _, t in ipairs(tiles) do
						w(('\t\t\t\t\t<tile x="%d" y="%d" z="%d">\n'):format(t.offset.x, t.offset.y, t.offset.z))
						for _, id in ipairs(t.ids) do
							w(('\t\t\t\t\t\t<item id="%d" />\n'):format(id))
						end
						w('\t\t\t\t\t</tile>\n')
					end
					w(('\t\t\t\t</%s>\n'):format(tag))
				end
			end
		end
		w('\t\t\t</items>\n')
		w('\t\t</rule>\n')
	end
	w('\t</floor_rules>\n')
	w('</decoration_preset>\n')
	return table.concat(out)
end

-- ============================================================================
-- Rule-from-selection: montagem pura (testavel offline). `all` = lista de
-- { itemId, layer (0=ground 1=borda 2=fundo 3=regular), rel = {x,y,z} }
-- na ordem de extracao da selecao. roles: layer -> 0 (cluster) | 1 (itens).
-- ============================================================================

local function assembleRuleFromItems(all, opts)
	local rule = Defaults.floorRule()
	rule.ruleMode = "cluster"
	rule.name = opts.name
	rule.instanceCount = opts.instanceCount
	rule.instanceMinDistance = opts.minDistance
	rule.density = opts.densityPct / 100.0

	-- tiles do cluster: camadas role 0 na ordem fixa (0,1,2,3);
	-- agrupados por (x,y), z DESCARTADO [as-is do C++]
	local clusterMap, buckets = {}, {}
	for layer = 0, 3 do
		if opts.roles[layer] == 0 then
			for _, e in ipairs(all) do
				if e.layer == layer then
					local key = e.rel.x .. ":" .. e.rel.y
					local b = clusterMap[key]
					if not b then
						b = { x = e.rel.x, y = e.rel.y, ids = {} }
						clusterMap[key] = b
						buckets[#buckets + 1] = b
					end
					b.ids[#b.ids + 1] = e.itemId
				end
			end
		end
	end
	-- ordem do std::map do C++: (x asc, y asc)
	table.sort(buckets, function(a, b)
		if a.x ~= b.x then
			return a.x < b.x
		end
		return a.y < b.y
	end)
	for _, b in ipairs(buckets) do
		rule.clusterTiles[#rule.clusterTiles + 1] = { offset = { x = b.x, y = b.y, z = 0 }, itemIds = b.ids }
	end

	-- lista de itens: contagem por id nas camadas role 1; id asc; peso = n*100
	local counts, idList = {}, {}
	for layer = 0, 3 do
		if opts.roles[layer] == 1 then
			for _, e in ipairs(all) do
				if e.layer == layer then
					if not counts[e.itemId] then
						idList[#idList + 1] = e.itemId
					end
					counts[e.itemId] = (counts[e.itemId] or 0) + 1
				end
			end
		end
	end
	table.sort(idList)
	for _, id in ipairs(idList) do
		rule.items[#rule.items + 1] = Defaults.itemEntry(id, counts[id] * 100)
	end
	return rule
end

-- ============================================================================
-- Modulo exportado (para testes offline e fases 2/3)
-- ============================================================================

local AreaDecoration = {
	assembleRuleFromItems = assembleRuleFromItems,
	Defaults = Defaults,
	Engine = Engine,
	RealMap = RealMap,
	Xml = Xml,
	MT = MT,
	AreaOps = AreaOps,
	PreviewManager = PreviewManager,
	validatePreset = validatePreset,
	sortRulesByPriority = sortRulesByPriority,
	getMatchingRules = getMatchingRules,
	findRule = findRule,
	matchesFloor = matchesFloor,
	representativeItemId = representativeItemId,
	clusterRepresentativeItemId = clusterRepresentativeItemId,
	clusterItemIds = clusterItemIds,
	clusterTotalItemCount = clusterTotalItemCount,
}

-- em testes offline (sem o editor), apenas exporta o modulo
if not (app and app.hasMap) then
	return AreaDecoration
end

-- ============================================================================
-- Presets: storage JSON + importacao dos XML de data/presets/decoration
-- ============================================================================

local STORE = app.storage("area_decoration")
local db = STORE:load() or { presets = {} }
if type(db.presets) ~= "table" then
	db.presets = {}
end

local function saveDb()
	STORE:save(db)
end

local function presetNames()
	local names = {}
	for n in pairs(db.presets) do
		names[#names + 1] = n
	end
	table.sort(names)
	return names
end

-- requer app.readFile/app.listFiles (build recente)
local function importXmlPresets()
	if not app.listFiles or not app.readFile then
		return nil, "Importacao requer rebuild do editor (app.readFile/listFiles)."
	end
	local files = app.listFiles("presets/decoration")
	if not files then
		return nil, "Pasta data/presets/decoration nao encontrada."
	end
	-- espelha PresetManager::loadPresets: limpa e reconstroi do zero
	local fresh = {}
	local imported, failed = 0, 0
	for _, f in ipairs(files) do
		if f:lower():match("%.xml$") then
			local content = app.readFile("presets/decoration/" .. f)
			if content then
				local preset = Xml.presetFromString(content)
				if preset then
					fresh[preset.name] = preset
					imported = imported + 1
				else
					failed = failed + 1
				end
			else
				failed = failed + 1
			end
		end
	end
	db.presets = fresh
	saveDb()
	return imported, failed
end

-- ============================================================================
-- Fase 2: overlay de preview fantasma no mapa
-- ============================================================================

local OVERLAY_ID = "area_decoration_preview"
local spriteCache = {}

local function spriteFor(itemId)
	local img = spriteCache[itemId]
	if img == nil then
		local s = Image.fromItemSprite(itemId)
		if s and s.valid and s.isFromSprite then
			img = s
		else
			img = false
		end
		spriteCache[itemId] = img
	end
	return img
end

-- icones de UI dos assets do editor (assets/svg/...), com tint claro para o
-- tema escuro; requer rebuild (Image.fromAsset) — sem ele, ha fallback por
-- sprite de item nos chamadores
local assetIconCache = {}
local function assetIcon(p)
	local img = assetIconCache[p]
	if img == nil then
		img = false
		if Image.fromAsset then
			local ok, a = pcall(function()
				return Image.fromAsset(p, 16, 224, 224, 224)
			end)
			if ok and a and a.valid then
				img = a
			end
		end
		assetIconCache[p] = img
	end
	if img then
		return img
	end
	return nil
end

app.mapView:addOverlay(OVERLAY_ID, {
	ondraw = function(ctx)
		if not PreviewManager.hasActivePreview() then
			return
		end
		local preview = PreviewManager.active
		local v = ctx.view
		local opacity = PreviewManager.previewOpacity
		for _, it in ipairs(preview.items) do
			if it.z == v.z and it.x >= v.x1 and it.x <= v.x2 and it.y >= v.y1 and it.y <= v.y2 then
				local img = spriteFor(it.itemId)
				if img then
					ctx.image({ image = img, x = it.x, y = it.y, z = it.z, opacity = opacity })
				end
			end
		end
	end,
	onhover = function(info)
		if not PreviewManager.hasActivePreview() or not info.pos then
			return nil
		end
		local items = PreviewManager.getPreviewItemsAt(info.pos.x, info.pos.y, info.pos.z)
		if #items == 0 then
			return nil
		end
		return {
			tooltip = { text = "Preview: " .. #items .. " item(s)", color = { 120, 220, 120 } },
			highlight = { color = { 120, 220, 120, 70 }, filled = true },
		}
	end,
})
pcall(function()
	app.mapView:registerShow("Area Decoration Preview", OVERLAY_ID, { enabled = true })
end)

-- O drawer de overlay cacheia os comandos por (view, selecao, historico,
-- revisao). Gerar/limpar preview nao muda nada disso, entao forcamos o bump
-- da revisao via setEnabled (incondicional no engine) + redraw.
local function refreshPreviewOverlay()
	app.mapView:setEnabled(OVERLAY_ID, true)
	app.refresh()
end


-- ============================================================================
-- Fase 3: dialogo completo
-- ============================================================================

local engine = Engine.new(RealMap.new())
local MAX_AUTO_BATCHES = 20 -- kMaxAutoBatches do dialogo C++

local MODE_OPTIONS = { "PureRandom (espalhado)", "Clustered (agrupado)", "GridBased (grade)" }
local AREA_OPTIONS = { "Retangulo", "Flood Fill", "Selecao atual" }

-- preset de trabalho da UI (copia editavel; Salvar grava no storage)
local working = Defaults.preset()
local workingName = ""

local function itemName(id)
	local ok, info = pcall(function()
		return Items.getInfo(id)
	end)
	if ok and info and info.name and info.name ~= "" then
		return info.name
	end
	return "item " .. id
end

local function describeEntry(entry)
	if not entry.isComposite then
		return ("%d %s (peso %d)"):format(entry.itemId, itemName(entry.itemId), entry.weight)
	end
	local kind = entry.isCluster and "cluster" or "composite"
	return ("%s: %d tile(s) (peso %d)"):format(kind, #entry.compositeTiles, entry.weight)
end

local function ruleSummary(i, rule)
	local mode
	if rule.ruleMode == "cluster" then
		mode = "cluster x" .. rule.instanceCount
	elseif rule.ruleMode == "range" then
		mode = rule.fromFloorId .. "-" .. rule.toFloorId
	else
		mode = "piso " .. rule.floorId
	end
	local onOff = rule.enabled and "[on] " or "[off]"
	return ("%s %d. %s | %s | %d item(s) | %d%% | prio %d"):format(
		onOff, i, rule.name, mode, #rule.items, math.floor(rule.density * 100 + 0.5), rule.priority)
end

local function ruleListItems()
	local items = {}
	for i, rule in ipairs(working.floorRules) do
		local it = { text = ruleSummary(i, rule) }
		local repId = 0
		if rule.ruleMode == "cluster" then
			repId = clusterRepresentativeItemId(rule)
		elseif rule.items[1] then
			repId = representativeItemId(rule.items[1])
		end
		if repId > 0 then
			local img = spriteFor(repId)
			if img then
				it.image = img
			end
		end
		items[#items + 1] = it
	end
	return items
end

-- ---------------------------------------------------------------------------
-- Extracao da selecao (rule-from-selection / captura de template)
-- camadas: 0=ground 1=borda 2=fundo(alwaysOnBottom) 3=regular
-- ---------------------------------------------------------------------------

local function extractSelectionItems()
	if not app.selection or app.selection.size == 0 then
		return nil, "Selecione uma area no mapa primeiro."
	end
	local tiles = app.selection.tiles
	local minX, minY, minZ = math.huge, math.huge, math.huge
	local maxX, maxY, maxZ = -math.huge, -math.huge, -math.huge
	for _, t in ipairs(tiles) do
		minX = math.min(minX, t.x)
		maxX = math.max(maxX, t.x)
		minY = math.min(minY, t.y)
		maxY = math.max(maxY, t.y)
		minZ = math.min(minZ, t.z)
		maxZ = math.max(maxZ, t.z)
	end
	-- centro = divisao inteira de (min+max)/2, como o C++
	local cx = math.floor((minX + maxX) / 2)
	local cy = math.floor((minY + maxY) / 2)
	local cz = math.floor((minZ + maxZ) / 2)

	local all = {}
	for _, t in ipairs(tiles) do
		local rel = { x = t.x - cx, y = t.y - cy, z = t.z - cz }
		if t.ground and t.ground.id > 0 then
			all[#all + 1] = { itemId = t.ground.id, layer = 0, rel = rel }
		end
		for _, item in ipairs(t.items) do
			if item.id > 0 then
				local layer
				if item.isBorder then
					layer = 1
				else
					-- isAlwaysOnBottom requer rebuild; fallback por zOrder
					local bottom = item.isAlwaysOnBottom
					if bottom == nil then
						bottom = (item.zOrder == 1 or item.zOrder == 2)
					end
					layer = bottom and 2 or 3
				end
				all[#all + 1] = { itemId = item.id, layer = layer, rel = rel }
			end
		end
	end
	return all
end

-- roles: tabela layer(0..3) -> 0 (tiles do cluster) | 1 (lista de itens)
local function buildRuleFromSelection(opts)
	local all, err = extractSelectionItems()
	if not all then
		return nil, err
	end
	return assembleRuleFromItems(all, opts)
end

-- captura TODA a selecao (todas as camadas) como template de cluster tiles
local function captureClusterTemplate()
	local all, err = extractSelectionItems()
	if not all then
		return nil, err
	end
	local clusterMap, buckets = {}, {}
	for _, e in ipairs(all) do
		local key = e.rel.x .. ":" .. e.rel.y
		local b = clusterMap[key]
		if not b then
			b = { x = e.rel.x, y = e.rel.y, ids = {} }
			clusterMap[key] = b
			buckets[#buckets + 1] = b
		end
		b.ids[#b.ids + 1] = e.itemId
	end
	table.sort(buckets, function(a, b)
		if a.x ~= b.x then
			return a.x < b.x
		end
		return a.y < b.y
	end)
	local tiles = {}
	for _, b in ipairs(buckets) do
		tiles[#tiles + 1] = { offset = { x = b.x, y = b.y, z = 0 }, itemIds = b.ids }
	end
	return tiles
end

-- ---------------------------------------------------------------------------
-- Editor de regra (layout espelhado do "Edit Floor Rule" do C++: 3 colunas)
-- ---------------------------------------------------------------------------

-- cache global de doodad brushes (nome + lookId), carregado sob demanda
local doodadCache = nil

local function loadDoodads()
	if doodadCache then
		return doodadCache
	end
	doodadCache = {}
	local ok, names = pcall(function()
		return Brushes.getNames()
	end)
	if ok and names then
		for _, name in ipairs(names) do
			local b = Brushes.get(name)
			if b and b.type == "doodad" then
				doodadCache[#doodadCache + 1] = { name = name, lookId = b.lookId or 0 }
			end
		end
		table.sort(doodadCache, function(a, b)
			return a.name:lower() < b.name:lower()
		end)
	end
	return doodadCache
end

local function brushIcon(lookId)
	if not lookId or lookId == 0 then
		return nil
	end
	local img = spriteFor(lookId)
	if img then
		return img
	end
	local ok, s = pcall(function()
		return Image.fromSprite(lookId)
	end)
	if ok and s and s.valid then
		return s
	end
	return nil
end

-- replica AddItemsFromDoodad do C++: singles com chance (<=0 -> peso default)
-- e composites com chance CUMULATIVA (peso = chance - anterior)
local function addItemsFromDoodad(rule, brushName, defaultWeight)
	local b = Brushes.get(brushName)
	if not b then
		return nil, "Brush nao encontrado."
	end
	if not b.getDoodadItems then
		return nil, "Doodad Browser requer rebuild do editor (getDoodadItems)."
	end
	local data = b:getDoodadItems()
	if not data then
		return nil, "Brush nao e um doodad."
	end
	local function hasSingle(id)
		for _, e in ipairs(rule.items) do
			if not e.isComposite and e.itemId == id then
				return true
			end
		end
		return false
	end
	local singles, comps = 0, 0
	for _, alt in ipairs(data) do
		for _, s in ipairs(alt.singles) do
			if s.id ~= 0 and not hasSingle(s.id) then
				local w = s.chance
				if w <= 0 then
					w = defaultWeight
				end
				rule.items[#rule.items + 1] = Defaults.itemEntry(s.id, w)
				singles = singles + 1
			end
		end
		local prevChance = 0
		for _, c in ipairs(alt.composites) do
			if #c.tiles == 0 then
				prevChance = c.chance
			else
				local w = c.chance - prevChance
				if w <= 0 then
					w = defaultWeight
				end
				prevChance = c.chance
				local tiles = {}
				for _, t in ipairs(c.tiles) do
					if #t.ids > 0 then
						tiles[#tiles + 1] = { offset = { x = t.x, y = t.y, z = t.z }, itemIds = t.ids }
					end
				end
				if #tiles > 0 then
					rule.items[#rule.items + 1] = Defaults.makeComposite(tiles, w)
					comps = comps + 1
				end
			end
		end
	end
	return singles, comps
end

local DOODAD_LIST_CAP = 200

local openRuleEditor

openRuleEditor = function(original, onCommit)
	local rule = deepCopy(original)
	local ed
	ed = Dialog({ title = "Edit Floor Rule", resizable = true })

	local function status(text)
		ed:modify({ edstatus = { text = text } })
		ed:repaint()
	end

	local edBusy = false
	local function guarded(fn)
		return function()
			if edBusy then
				return
			end
			edBusy = true
			local ok, err = pcall(fn)
			edBusy = false
			if not ok then
				status("Erro: " .. tostring(err))
			end
		end
	end

	local function edIcon(p)
		local img = assetIcon(p)
		if img then
			ed:image({ image = img, valign = "center" })
		end
	end

	-- ------------- estado derivado dos radios (lido fresco de ed.data) -------
	local function currentMode(d)
		if d.rRadioCluster then
			return "cluster"
		elseif d.rRadioRange then
			return "range"
		end
		return "single"
	end

	local function updateModeEnables()
		local mode = currentMode(ed.data)
		ed:modify({
			rfloor = { enabled = mode == "single" },
			rfrom = { enabled = mode == "range" },
			rto = { enabled = mode == "range" },
			ricount = { enabled = mode == "cluster" },
			rimind = { enabled = mode == "cluster" },
			rreqground = { enabled = mode == "cluster" },
			rhascenter = { enabled = mode == "cluster" },
		})
		ed:repaint()
	end

	local function updateFriendEnables()
		local range = ed.data.rfRadioRange and true or false
		ed:modify({
			rfsingle = { enabled = not range },
			rffrom = { enabled = range },
			rfto = { enabled = range },
		})
		ed:repaint()
	end

	-- previews de sprite dos pisos
	local function updateFloorPreviews()
		local d = ed.data
		local mode = currentMode(d)
		local fromId, toId = 0, 0
		if mode == "single" then
			fromId = math.floor(d.rfloor or 0)
		elseif mode == "range" then
			fromId = math.floor(d.rfrom or 0)
			toId = math.floor(d.rto or 0)
		end
		ed:modify({
			fpFrom = { itemid = fromId, width = 32, height = 32, smooth = false },
			fpTo = { itemid = toId, width = 32, height = 32, smooth = false },
		})
		ed:repaint()
	end

	local function updateFriendPreviews()
		local d = ed.data
		local fromId, toId = 0, 0
		if d.rfRadioRange then
			fromId = math.floor(d.rffrom or 0)
			toId = math.floor(d.rfto or 0)
		else
			fromId = math.floor(d.rfsingle or 0)
		end
		ed:modify({
			fpfFrom = { itemid = fromId, width = 32, height = 32, smooth = false },
			fpfTo = { itemid = toId, width = 32, height = 32, smooth = false },
		})
		ed:repaint()
	end

	-- copia os valores dos controles para a copia da regra
	local function capture()
		local d = ed.data
		rule.name = d.rname or rule.name
		rule.density = math.floor(d.rdensity or 100) / 100.0
		rule.maxPlacements = math.floor(d.rmaxp or -1)
		rule.priority = math.floor(d.rprio or 0)
		rule.borderItemId = math.floor(d.rborder or 0)
		rule.friendChance = math.floor(d.rfchance or 0)
		rule.friendStrength = math.floor(d.rfstrength or 0)
		if d.rfRadioRange then
			rule.friendFloorId = 0
			rule.friendFromFloorId = math.floor(d.rffrom or 0)
			rule.friendToFloorId = math.floor(d.rfto or 0)
		else
			rule.friendFloorId = math.floor(d.rfsingle or 0)
			rule.friendFromFloorId = 0
			rule.friendToFloorId = 0
		end
		rule.ruleMode = currentMode(d)
		if rule.ruleMode == "single" then
			rule.floorId = math.floor(d.rfloor or 0)
			rule.fromFloorId = 0
			rule.toFloorId = 0
		elseif rule.ruleMode == "range" then
			rule.floorId = 0
			rule.fromFloorId = math.floor(d.rfrom or 0)
			rule.toFloorId = math.floor(d.rto or 0)
		else
			rule.floorId = 0
			rule.fromFloorId = 0
			rule.toFloorId = 0
			rule.instanceCount = math.floor(d.ricount or 1)
			rule.instanceMinDistance = math.floor(d.rimind or 5)
			rule.requireGround = d.rreqground and true or false
			rule.hasCenterPoint = d.rhascenter and true or false
		end
	end

	local function entryItems()
		local items = {}
		for _, e in ipairs(rule.items) do
			local it = { text = describeEntry(e) }
			local repId = representativeItemId(e)
			if repId > 0 then
				local img = spriteFor(repId)
				if img then
					it.image = img
				end
			end
			items[#items + 1] = it
		end
		return items
	end

	local function refreshEntries()
		ed:modify({ redItems = { items = entryItems() } })
		ed:repaint()
	end

	local function clusterInfoText()
		return ("Template: %d tile(s), %d item(s)"):format(#rule.clusterTiles, clusterTotalItemCount(rule))
	end

	-- doodad browser
	local doodadFilter = {}
	local function doodadListItems(query)
		local all = loadDoodads()
		doodadFilter = {}
		query = (query or ""):lower()
		local items = {}
		for _, d in ipairs(all) do
			if query == "" or d.name:lower():find(query, 1, true) then
				doodadFilter[#doodadFilter + 1] = d
				if #doodadFilter <= DOODAD_LIST_CAP then
					local it = { text = d.name }
					local img = brushIcon(d.lookId)
					if img then
						it.image = img
					end
					items[#items + 1] = it
				end
			end
		end
		return items, #doodadFilter, #all
	end

	local function refreshDoodads()
		local items, matched, total = doodadListItems(ed.data.dSearch)
		local info = ("%d de %d doodads"):format(matched, total)
		if matched > DOODAD_LIST_CAP then
			info = info .. (" (mostrando %d - refine a busca)"):format(DOODAD_LIST_CAP)
		end
		ed:modify({
			dList = { items = items },
			dInfo = { text = info },
		})
		ed:repaint()
	end

	local mode = rule.ruleMode
	local friendRange = isFriendRange(rule)

	-- ======================== 3 COLUNAS ========================
	ed:wrap({})

	-- -------- COLUNA ESQUERDA --------
	ed:box({ orient = "vertical" })

	ed:box({ orient = "vertical", label = "Rule Name" })
	ed:input({ id = "rname", text = rule.name })
	ed:endbox()

	ed:box({ orient = "vertical", label = "Floor Selection" })
	ed:wrap({})
	edIcon("svg/solid/layer-group.svg")
	ed:label({ text = "Preview:" })
	ed:image({ id = "fpFrom", itemid = (mode == "range") and rule.fromFloorId or rule.floorId, width = 32, height = 32, smooth = false })
	ed:image({ id = "fpTo", itemid = (mode == "range") and rule.toFloorId or 0, width = 32, height = 32, smooth = false })
	ed:endwrap()
	ed:wrap({})
	ed:radio({
		id = "rRadioSingle",
		text = "Single Floor ID",
		group = true,
		selected = mode == "single",
		onclick = function()
			updateModeEnables()
			updateFloorPreviews()
		end,
	})
	ed:number({
		id = "rfloor",
		value = rule.floorId,
		min = 0,
		max = 65535,
		enabled = mode == "single",
		onchange = updateFloorPreviews,
	})
	ed:endwrap()
	ed:wrap({})
	ed:radio({
		id = "rRadioRange",
		text = "Floor Range",
		selected = mode == "range",
		onclick = function()
			updateModeEnables()
			updateFloorPreviews()
		end,
	})
	ed:number({ id = "rfrom", label = "From:", value = rule.fromFloorId, min = 0, max = 65535, enabled = mode == "range", onchange = updateFloorPreviews })
	ed:number({ id = "rto", label = "To:", value = rule.toFloorId, min = 0, max = 65535, enabled = mode == "range", onchange = updateFloorPreviews })
	ed:endwrap()
	ed:wrap({})
	ed:radio({
		id = "rRadioCluster",
		text = "Cluster",
		selected = mode == "cluster",
		onclick = function()
			updateModeEnables()
			updateFloorPreviews()
		end,
	})
	ed:endwrap()
	ed:endbox()

	ed:box({ orient = "vertical", label = "Cluster (modo Cluster)" })
	ed:wrap({})
	edIcon("svg/solid/object-group.svg")
	ed:number({ id = "ricount", label = "Instances:", value = rule.instanceCount, min = 1, max = 100, enabled = mode == "cluster" })
	ed:number({ id = "rimind", label = "Min Dist:", value = rule.instanceMinDistance, min = 0, max = 100, enabled = mode == "cluster" })
	ed:endwrap()
	ed:wrap({})
	ed:check({ id = "rreqground", text = "Require Ground", selected = rule.requireGround, enabled = mode == "cluster" })
	ed:check({ id = "rhascenter", text = "Has Center Point", selected = rule.hasCenterPoint, enabled = mode == "cluster" })
	ed:endwrap()
	ed:label({ id = "rclusterinfo", text = clusterInfoText() })
	ed:wrap({})
	ed:button({
		text = "Capture Template From Selection",
		tooltip = "Usa a selecao atual (todas as camadas) como template do cluster.",
		onclick = guarded(function()
			local tiles, err = captureClusterTemplate()
			if not tiles then
				status(err)
				return
			end
			rule.clusterTiles = tiles
			rule.centerOffset = { x = 0, y = 0, z = 0 }
			ed:modify({ rclusterinfo = { text = clusterInfoText() } })
			status("Template capturado: " .. #tiles .. " tile(s).")
		end),
	})
	ed:endwrap()
	ed:endbox()

	ed:box({ orient = "vertical", label = "Settings" })
	ed:wrap({})
	edIcon("svg/solid/sliders.svg")
	ed:number({ id = "rdensity", label = "Density (%):", value = math.floor(rule.density * 100), min = 1, max = 100 })
	ed:endwrap()
	ed:wrap({})
	ed:number({ id = "rmaxp", label = "Max Placements:", value = rule.maxPlacements, min = -1, max = 10000 })
	ed:endwrap()
	ed:wrap({})
	ed:number({ id = "rprio", label = "Priority:", value = rule.priority, min = 0, max = 100 })
	ed:endwrap()
	ed:label({ text = "(-1 = unlimited placements)" })
	ed:endbox()

	ed:box({ orient = "vertical", label = "Border Item (placed on top)" })
	ed:wrap({})
	edIcon("svg/solid/border-all.svg")
	ed:item({
		id = "rborder",
		itemid = rule.borderItemId,
		tooltip = "Item colocado POR CIMA de cada decoracao desta regra. Clique para escolher.",
	})
	ed:endwrap()
	ed:label({ text = "(0 = no border item)" })
	ed:endbox()

	ed:box({ orient = "vertical", label = "Friend Floor (bias placement)" })
	ed:wrap({})
	edIcon("svg/solid/magnet.svg")
	ed:label({ text = "Preview:" })
	ed:image({ id = "fpfFrom", itemid = friendRange and rule.friendFromFloorId or rule.friendFloorId, width = 32, height = 32, smooth = false })
	ed:image({ id = "fpfTo", itemid = friendRange and rule.friendToFloorId or 0, width = 32, height = 32, smooth = false })
	ed:endwrap()
	ed:wrap({})
	ed:radio({
		id = "rfRadioSingle",
		text = "Single Friend Floor ID",
		group = true,
		selected = not friendRange,
		onclick = function()
			updateFriendEnables()
			updateFriendPreviews()
		end,
	})
	ed:number({ id = "rfsingle", value = rule.friendFloorId, min = 0, max = 65535, enabled = not friendRange, onchange = updateFriendPreviews })
	ed:endwrap()
	ed:wrap({})
	ed:radio({
		id = "rfRadioRange",
		text = "Friend Floor Range",
		selected = friendRange,
		onclick = function()
			updateFriendEnables()
			updateFriendPreviews()
		end,
	})
	ed:number({ id = "rffrom", label = "From:", value = rule.friendFromFloorId, min = 0, max = 65535, enabled = friendRange, onchange = updateFriendPreviews })
	ed:number({ id = "rfto", label = "To:", value = rule.friendToFloorId, min = 0, max = 65535, enabled = friendRange, onchange = updateFriendPreviews })
	ed:endwrap()
	ed:wrap({})
	ed:number({
		id = "rfchance",
		label = "Friend Chance (%):",
		value = rule.friendChance,
		min = 0,
		max = 100,
		tooltip = "0 desliga. Quanto maior, mais a densidade se concentra perto do friend floor.",
	})
	ed:number({
		id = "rfstrength",
		label = "Strength:",
		value = rule.friendStrength,
		min = 0,
		max = 100,
		tooltip = "Quao rapido a densidade cai com a distancia.",
	})
	ed:endwrap()
	ed:endbox()

	ed:endbox() -- fim coluna esquerda

	-- -------- COLUNA DO MEIO --------
	ed:box({ orient = "vertical" })

	ed:box({ orient = "vertical", label = "Items", expand = true })
	ed:wrap({ expand = true })
	ed:list({
		id = "redItems",
		items = entryItems(),
		icon_size = 32,
		width = 330,
		min_height = 180,
		expand = true,
	})
	ed:endwrap()
	ed:endbox()

	ed:box({ orient = "vertical", label = "Add Single Item" })
	ed:wrap({})
	edIcon("svg/solid/plus.svg")
	ed:item({ id = "rnewItem", label = "Item:", itemid = 0, tooltip = "Clique para escolher o item." })
	ed:number({ id = "rnewWeight", label = "Weight:", value = 100, min = 1, max = 1000 })
	ed:button({
		text = "Add",
		onclick = guarded(function()
			local id = math.floor(ed.data.rnewItem or 0)
			if id == 0 then
				status("Escolha um item primeiro.")
				return
			end
			rule.items[#rule.items + 1] = Defaults.itemEntry(id, math.floor(ed.data.rnewWeight or 100))
			refreshEntries()
		end),
	})
	ed:endwrap()
	ed:wrap({})
	ed:number({ id = "rrangeFrom", label = "From ID:", value = 0, min = 0, max = 65535 })
	ed:number({ id = "rrangeTo", label = "To ID:", value = 0, min = 0, max = 65535 })
	ed:button({
		text = "Add Range",
		tooltip = "Adiciona todos os ids da faixa (max 1000) com o peso atual.",
		onclick = guarded(function()
			local from = math.floor(ed.data.rrangeFrom or 0)
			local to = math.floor(ed.data.rrangeTo or 0)
			if from <= 0 or to <= 0 then
				status("Informe os dois ids da faixa.")
				return
			end
			if from > to then
				from, to = to, from
			end
			if to - from + 1 > 1000 then
				status("Faixa grande demais (max 1000 ids).")
				return
			end
			local weight = math.floor(ed.data.rnewWeight or 100)
			for id = from, to do
				rule.items[#rule.items + 1] = Defaults.itemEntry(id, weight)
			end
			refreshEntries()
		end),
	})
	ed:endwrap()
	ed:wrap({})
	ed:button({
		text = "Edit Weight",
		tooltip = "Aplica o peso do campo Weight ao item selecionado na lista.",
		onclick = guarded(function()
			local idx = ed.data.redItems
			if not idx or not rule.items[idx] then
				status("Selecione um item na lista.")
				return
			end
			rule.items[idx].weight = math.floor(ed.data.rnewWeight or 100)
			refreshEntries()
		end),
	})
	ed:button({
		text = "Remove",
		onclick = guarded(function()
			local idx = ed.data.redItems
			if not idx or not rule.items[idx] then
				status("Selecione um item na lista.")
				return
			end
			table.remove(rule.items, idx)
			refreshEntries()
		end),
	})
	ed:button({
		text = "Clear All",
		onclick = guarded(function()
			rule.items = {}
			refreshEntries()
		end),
	})
	ed:endwrap()
	ed:endbox()

	ed:box({ orient = "vertical", label = "Cluster From Selection" })
	ed:wrap({})
	edIcon("svg/solid/object-ungroup.svg")
	ed:number({ id = "cfsCount", label = "Count:", value = 1, min = 1, max = 100 })
	ed:number({ id = "cfsRadius", label = "Radius:", value = 3, min = 0, max = 50 })
	ed:number({ id = "cfsMinDist", label = "Min Dist:", value = 2, min = 0, max = 50 })
	ed:endwrap()
	ed:wrap({})
	ed:button({
		text = "Add Cluster From Selection",
		tooltip = "Cria uma ENTRADA cluster (item ponderado) a partir da selecao atual.",
		onclick = guarded(function()
			local tiles, err = captureClusterTemplate()
			if not tiles then
				status(err)
				return
			end
			rule.items[#rule.items + 1] = Defaults.makeCluster(
				tiles,
				math.floor(ed.data.rnewWeight or 100),
				math.floor(ed.data.cfsCount or 1),
				math.floor(ed.data.cfsRadius or 3),
				math.floor(ed.data.cfsMinDist or 2))
			refreshEntries()
			status("Entrada cluster adicionada (" .. #tiles .. " tiles).")
		end),
	})
	ed:button({
		text = "Replace Selected Cluster",
		tooltip = "Substitui os tiles da entrada cluster/composite selecionada pela selecao atual.",
		onclick = guarded(function()
			local idx = ed.data.redItems
			local entry = idx and rule.items[idx]
			if not entry or not entry.isComposite then
				status("Selecione uma entrada composite/cluster na lista.")
				return
			end
			local tiles, err = captureClusterTemplate()
			if not tiles then
				status(err)
				return
			end
			entry.compositeTiles = tiles
			refreshEntries()
			status("Tiles da entrada substituidos (" .. #tiles .. ").")
		end),
	})
	ed:endwrap()
	ed:endbox()

	ed:endbox() -- fim coluna do meio

	-- -------- COLUNA DIREITA --------
	ed:box({ orient = "vertical" })
	ed:box({ orient = "vertical", label = "Doodad Browser (duplo clique p/ adicionar)", expand = true })
	ed:wrap({})
	edIcon("svg/solid/tree.svg")
	ed:input({ id = "dSearch", text = "" })
	ed:button({
		text = "Filtrar",
		onclick = guarded(function()
			refreshDoodads()
		end),
	})
	ed:endwrap()
	local initialDoodads, initialMatched, initialTotal = doodadListItems("")
	ed:wrap({ expand = true })
	ed:list({
		id = "dList",
		items = initialDoodads,
		icon_size = 32,
		width = 280,
		min_height = 320,
		expand = true,
		ondoubleclick = guarded(function()
			local idx = ed.data.dList
			local entry = idx and doodadFilter[idx]
			if not entry then
				return
			end
			local singles, comps = addItemsFromDoodad(rule, entry.name, math.floor(ed.data.rnewWeight or 100))
			if not singles then
				status(comps)
				return
			end
			refreshEntries()
			if singles + comps > 0 then
				status(("Adicionados %d singles e %d composites de '%s'."):format(singles, comps, entry.name))
			else
				status("Nada novo para adicionar deste doodad (itens ja na lista).")
			end
		end),
	})
	ed:endwrap()
	ed:label({ id = "dInfo", text = ("%d de %d doodads"):format(math.min(initialMatched, DOODAD_LIST_CAP), initialTotal) })
	ed:endbox()
	ed:endbox() -- fim coluna direita

	ed:endwrap() -- fim das 3 colunas

	ed:wrap({})
	ed:button({
		text = "OK",
		onclick = guarded(function()
			capture()
			-- validacoes do dialogo C++ (TransferDataFromWindow)
			if rule.ruleMode == "cluster" then
				if #rule.clusterTiles == 0 then
					status("Regra cluster precisa de um template (use 'Capture Template From Selection').")
					return
				end
			elseif rule.ruleMode == "range" then
				if rule.fromFloorId > rule.toFloorId then
					status("Faixa invalida: 'From' maior que 'To'.")
					return
				end
			else
				if rule.floorId == 0 then
					status("Informe o Floor ID.")
					return
				end
			end
			if rule.ruleMode ~= "cluster" and #rule.items == 0 then
				status("A regra precisa de pelo menos um item.")
				return
			end
			if rule.friendFromFloorId > 0 and rule.friendToFloorId > 0
				and rule.friendFromFloorId > rule.friendToFloorId then
				status("Faixa de friend floor invalida.")
				return
			end
			ed:close()
			onCommit(rule)
		end),
	})
	ed:button({
		text = "Cancel",
		onclick = function()
			ed:close()
		end,
	})
	ed:endwrap()
	ed:label({ id = "edstatus", text = "" })

	ed:show({ wait = false })
end

-- ---------------------------------------------------------------------------
-- Dialogo "Regra da selecao"
-- ---------------------------------------------------------------------------

local ROLE_OPTIONS = { "Tiles do cluster", "Lista de itens" }

local function openRuleFromSelectionDialog(onCommit)
	local rs
	rs = Dialog({ title = "Gerar regra da selecao", resizable = true })

	local function status(text)
		rs:modify({ rsstatus = { text = text } })
		rs:repaint()
	end

	rs:label({ text = "Cria uma regra Cluster a partir da selecao atual do mapa." })
	rs:wrap({})
	rs:input({ id = "rsname", label = "Nome", text = "Selection Rule" })
	rs:endwrap()
	rs:wrap({})
	rs:number({ id = "rscount", label = "Instancias", value = 1, min = 1, max = 100 })
	rs:number({ id = "rsmind", label = "Dist. minima", value = 5, min = 1, max = 50 })
	rs:number({ id = "rsdens", label = "Densidade (%)", value = 100, min = 1, max = 100 })
	rs:endwrap()
	-- papeis por camada (defaults do C++: ground/bordas -> cluster; resto -> itens)
	rs:box({ orient = "vertical", label = "Papel de cada camada da selecao" })
	rs:wrap({})
	rs:combobox({ id = "rsrole0", label = "Ground", options = ROLE_OPTIONS, option = ROLE_OPTIONS[1] })
	rs:combobox({ id = "rsrole1", label = "Bordas", options = ROLE_OPTIONS, option = ROLE_OPTIONS[1] })
	rs:endwrap()
	rs:wrap({})
	rs:combobox({ id = "rsrole2", label = "Fundo", options = ROLE_OPTIONS, option = ROLE_OPTIONS[2] })
	rs:combobox({ id = "rsrole3", label = "Itens", options = ROLE_OPTIONS, option = ROLE_OPTIONS[2] })
	rs:endwrap()
	rs:endbox()
	rs:wrap({})
	rs:button({
		text = "Criar regra",
		onclick = function()
			local trimmed = (rs.data.rsname or ""):gsub("%s+$", "") -- trim a direita, como o C++
			if trimmed == "" then
				status("Informe um nome.")
				return
			end
			local function role(key)
				return (rs.data[key] == ROLE_OPTIONS[1]) and 0 or 1
			end
			local rule, err = buildRuleFromSelection({
				name = rs.data.rsname, -- [as-is] C++ guarda o nome SEM trim
				instanceCount = math.floor(rs.data.rscount or 1),
				minDistance = math.floor(rs.data.rsmind or 5),
				densityPct = math.floor(rs.data.rsdens or 100),
				roles = { [0] = role("rsrole0"), [1] = role("rsrole1"), [2] = role("rsrole2"), [3] = role("rsrole3") },
			})
			if not rule then
				status(err)
				return
			end
			if #rule.clusterTiles == 0 and #rule.items == 0 then
				status("A selecao nao produziu tiles nem itens.")
				return
			end
			rs:close()
			onCommit(rule)
		end,
	})
	rs:button({
		text = "Cancelar",
		onclick = function()
			rs:close()
		end,
	})
	rs:endwrap()
	rs:label({ id = "rsstatus", text = "" })
	rs:show({ wait = false })
end

-- ---------------------------------------------------------------------------
-- Dialogo principal (layout espelhado do dialogo C++)
-- ---------------------------------------------------------------------------

local buildDialog

-- keepWorking = true: rebuild de layout (troca de tipo de area) mantendo o
-- preset de trabalho atual em vez de recarregar do storage
buildDialog = function(selectedName, keepWorking)
	local names = presetNames()
	local options = { "(None - Custom)" }
	for _, n in ipairs(names) do
		options[#options + 1] = n
	end
	local current = selectedName or options[1]

	if keepWorking then
		-- mantem o working atual
	elseif selectedName and db.presets[selectedName] then
		working = deepCopy(db.presets[selectedName])
		workingName = selectedName
	else
		working = Defaults.preset()
		workingName = ""
	end

	local dlg
	dlg = Dialog({ title = "Area Decoration", resizable = true })

	local function setStatus(text)
		dlg:modify({ status = { text = text } })
		dlg:repaint()
		yield()
	end

	local busy = false
	local function guarded(fn)
		return function()
			if busy then
				return
			end
			busy = true
			local ok, err = pcall(fn)
			busy = false
			if not ok then
				setStatus("Erro: " .. tostring(err))
			end
		end
	end

	local function groupIcon(assetPath, fallbackItemId)
		local img = assetIcon(assetPath)
		if img then
			dlg:image({ image = img, valign = "center" })
		elseif fallbackItemId then
			dlg:image({ itemid = fallbackItemId, width = 20, height = 20, smooth = false, valign = "center" })
		end
	end

	local function refreshRules()
		dlg:modify({ rulesList = { items = ruleListItems() } })
		dlg:repaint()
	end

	-- le os controles para dentro do working (BuildPresetFromUI). Campos de
	-- area que nao existem no layout atual (tipo oculto) mantem o valor.
	local function buildWorkingFromUI()
		local d = dlg.data
		working.spacing.minDistance = math.floor(d.cMinDist or working.spacing.minDistance)
		working.spacing.minSameItemDistance = math.floor(d.cSameDist or working.spacing.minSameItemDistance)
		working.spacing.checkDiagonals = d.cDiagonals and true or false
		for i, label in ipairs(MODE_OPTIONS) do
			if d.cMode == label then
				working.distribution.mode = i - 1
			end
		end
		working.distribution.clusterStrength = math.floor(d.cStrength or 50) / 100.0
		working.distribution.clusterCount = math.floor(d.cCount or working.distribution.clusterCount)
		working.distribution.gridSpacingX = math.floor(d.cGridX or working.distribution.gridSpacingX)
		working.distribution.gridSpacingY = math.floor(d.cGridY or working.distribution.gridSpacingY)
		working.distribution.gridJitter = math.floor(d.cJitter or working.distribution.gridJitter)
		working.maxItemsTotal = math.floor(d.cMaxItems or working.maxItemsTotal)
		working.skipBlockedTiles = d.cSkipBlocked and true or false
		working.defaultSeed = 0

		local a = working.area
		for i, label in ipairs(AREA_OPTIONS) do
			if d.aType == label then
				a.type = i - 1
			end
		end
		if d.aX1 ~= nil then
			a.rectMin = { x = math.floor(d.aX1), y = math.floor(d.aY1 or 0), z = math.floor(d.aZ1 or 7) }
			a.rectMax = { x = math.floor(d.aX2 or 0), y = math.floor(d.aY2 or 0), z = math.floor(d.aZ2 or 7) }
		end
		if d.aFX ~= nil then
			a.floodOrigin = { x = math.floor(d.aFX), y = math.floor(d.aFY or 0), z = math.floor(d.aFZ or 7) }
			a.floodTargetGround = math.floor(d.aFTarget or 0)
			a.floodMaxRadius = math.floor(d.aFRadius or 100)
		end
		working.hasArea = true
		return working
	end

	-- ------------------------------------------------------------------
	-- Preset Configuration
	-- ------------------------------------------------------------------
	dlg:box({ orient = "vertical", label = "Preset Configuration" })
	dlg:wrap({})
	groupIcon("svg/solid/box-archive.svg", 1988)
	dlg:combobox({
		id = "preset",
		label = "Load Preset:",
		options = options,
		option = current,
		onchange = function()
			local name = dlg.data.preset
			dlg:close()
			if name == "(None - Custom)" then
				buildDialog(nil)
			else
				buildDialog(name)
			end
		end,
	})
	dlg:button({
		text = "Refresh",
		tooltip = "Recarrega a lista de presets do storage.",
		onclick = guarded(function()
			dlg:close()
			buildDialog(workingName ~= "" and workingName or nil)
		end),
	})
	dlg:button({
		text = "Delete",
		onclick = guarded(function()
			local name = dlg.data.preset
			if name == "(None - Custom)" or not db.presets[name] then
				setStatus("Selecione um preset salvo para excluir.")
				return
			end
			local result = app.alert({
				title = "Excluir preset",
				text = "Excluir o preset '" .. name .. "'?",
				buttons = { "Sim", "Nao" },
			})
			if result == 1 then
				db.presets[name] = nil
				saveDb()
				dlg:close()
				buildDialog()
			end
		end),
	})
	dlg:button({
		text = "Import...",
		tooltip = "Importa os XML de data/presets/decoration (substitui o conjunto importado).",
		onclick = guarded(function()
			local imported, failed = importXmlPresets()
			if not imported then
				setStatus(failed)
				return
			end
			dlg:close()
			buildDialog()
		end),
	})
	dlg:endwrap()
	dlg:wrap({})
	dlg:input({ id = "saveName", label = "Save As:", text = workingName })
	dlg:button({
		text = "Save",
		tooltip = "Grava o preset atual (regras + configuracao + area) no storage.",
		onclick = guarded(function()
			local name = (dlg.data.saveName or ""):gsub("^%s+", ""):gsub("%s+$", "")
			if name == "" then
				setStatus("Informe um nome para salvar.")
				return
			end
			buildWorkingFromUI()
			working.name = name
			db.presets[name] = deepCopy(working)
			saveDb()
			workingName = name
			setStatus("Preset '" .. name .. "' salvo.")
		end),
	})
	dlg:button({
		text = "Export...",
		tooltip = "Gera o XML do preset e copia para o clipboard (cole num .xml em data/presets/decoration).",
		onclick = guarded(function()
			buildWorkingFromUI()
			local name = (dlg.data.saveName or ""):gsub("^%s+", ""):gsub("%s+$", "")
			working.name = (name ~= "" and name) or (workingName ~= "" and workingName) or "Unnamed Preset"
			app.setClipboard(Xml.presetToString(working))
			setStatus("XML do preset '" .. working.name .. "' copiado para o clipboard.")
		end),
	})
	dlg:endwrap()
	dlg:endbox()

	-- ------------------------------------------------------------------
	-- Abas (ordem do C++: Area, Floor Rules, Settings, Seed)
	-- ------------------------------------------------------------------
	dlg:tab({ id = "tabArea", text = "Area" })
	dlg:box({ orient = "vertical", label = "Area Type" })
	dlg:wrap({})
	groupIcon("svg/solid/map.svg", 4526)
	dlg:combobox({
		id = "aType",
		options = AREA_OPTIONS,
		option = AREA_OPTIONS[math.min(math.max((working.area.type or 0) + 1, 1), 3)],
		tooltip = "Retangulo: coordenadas fixas. Flood Fill: expande a partir de um ponto. Selecao: usa a selecao viva do mapa.",
		onchange = function()
			buildWorkingFromUI()
			dlg:close()
			buildDialog(selectedName, true)
		end,
	})
	dlg:endwrap()
	dlg:endbox()

	if working.area.type == 1 then
		dlg:box({ orient = "vertical", label = "Flood Fill" })
		dlg:wrap({})
		dlg:number({ id = "aFX", label = "Origem X:", value = working.area.floodOrigin.x, min = 0, max = 65535 })
		dlg:number({ id = "aFY", label = "Y:", value = working.area.floodOrigin.y, min = 0, max = 65535 })
		dlg:number({ id = "aFZ", label = "Z:", value = working.area.floodOrigin.z, min = 0, max = 15 })
		dlg:endwrap()
		dlg:wrap({})
		dlg:number({
			id = "aFTarget",
			label = "Ground alvo (0 = o da origem):",
			value = working.area.floodTargetGround,
			min = 0,
			max = 65535,
		})
		dlg:number({ id = "aFRadius", label = "Raio max.:", value = working.area.floodMaxRadius, min = 1, max = 1000 })
		dlg:endwrap()
		dlg:endbox()
	elseif working.area.type == 0 then
		dlg:box({ orient = "vertical", label = "Rectangle Coordinates" })
		dlg:wrap({})
		dlg:number({ id = "aX1", label = "X1:", value = working.area.rectMin.x, min = 0, max = 65535 })
		dlg:number({ id = "aY1", label = "Y1:", value = working.area.rectMin.y, min = 0, max = 65535 })
		dlg:endwrap()
		dlg:wrap({})
		dlg:number({ id = "aX2", label = "X2:", value = working.area.rectMax.x, min = 0, max = 65535 })
		dlg:number({ id = "aY2", label = "Y2:", value = working.area.rectMax.y, min = 0, max = 65535 })
		dlg:endwrap()
		dlg:wrap({})
		dlg:number({ id = "aZ1", label = "Z1:", value = working.area.rectMin.z, min = 0, max = 15 })
		dlg:number({ id = "aZ2", label = "Z2:", value = working.area.rectMax.z, min = 0, max = 15 })
		dlg:endwrap()
		dlg:label({
			id = "zFloors",
			text = ("Z Floors: %d (Z %d)"):format(
				math.abs(working.area.rectMax.z - working.area.rectMin.z) + 1, working.area.rectMin.z),
		})
		dlg:wrap({})
		dlg:button({
			text = "Use Current Selection",
			tooltip = "Preenche as coordenadas com os limites da selecao atual.",
			onclick = guarded(function()
				if not app.selection or app.selection.size == 0 then
					setStatus("Selecione uma area no mapa primeiro.")
					return
				end
				local b = app.selection.bounds
				dlg:modify({
					aX1 = { value = b.min.x },
					aY1 = { value = b.min.y },
					aZ1 = { value = b.min.z },
					aX2 = { value = b.max.x },
					aY2 = { value = b.max.y },
					aZ2 = { value = b.max.z },
					zFloors = { text = ("Z Floors: %d (Z %d)"):format(b.max.z - b.min.z + 1, b.min.z) },
					areaInfo = { text = ("Area: %d tile(s) selecionado(s)"):format(app.selection.size) },
				})
				dlg:repaint()
			end),
		})
		dlg:endwrap()
		dlg:endbox()
	else
		dlg:box({ orient = "vertical", label = "Current Selection" })
		dlg:label({ text = "A area sera a selecao VIVA do mapa no momento de gerar o preview." })
		dlg:endbox()
	end
	dlg:label({ id = "areaInfo", text = "No area defined" })

	dlg:tab({ id = "tabRules", text = "Floor Rules" })
	dlg:list({
		id = "rulesList",
		items = ruleListItems(),
		icon_size = 32,
		height = 220,
		max_height = 220,
		expand = false,
	})
	dlg:label({
		text = "Nota: varias regras no mesmo piso sao aplicadas por prioridade. Para empilhar no mesmo tile, use Min Distance 0.",
	})
	dlg:wrap({})
	dlg:button({
		text = "Add Rule",
		onclick = guarded(function()
			local rule = Defaults.floorRule()
			rule.name = "New Rule"
			openRuleEditor(rule, function(committed)
				working.floorRules[#working.floorRules + 1] = committed
				refreshRules()
			end)
		end),
	})
	dlg:button({
		text = "Edit",
		onclick = guarded(function()
			local idx = dlg.data.rulesList
			if not idx or not working.floorRules[idx] then
				setStatus("Selecione uma regra.")
				return
			end
			openRuleEditor(working.floorRules[idx], function(committed)
				working.floorRules[idx] = committed
				refreshRules()
			end)
		end),
	})
	dlg:button({
		text = "Remove",
		onclick = guarded(function()
			local idx = dlg.data.rulesList
			if not idx or not working.floorRules[idx] then
				setStatus("Selecione uma regra.")
				return
			end
			table.remove(working.floorRules, idx)
			refreshRules()
		end),
	})
	dlg:button({
		text = "Duplicate",
		onclick = guarded(function()
			local idx = dlg.data.rulesList
			if not idx or not working.floorRules[idx] then
				setStatus("Selecione uma regra.")
				return
			end
			local copy = deepCopy(working.floorRules[idx])
			copy.name = copy.name .. " (copia)"
			working.floorRules[#working.floorRules + 1] = copy
			refreshRules()
		end),
	})
	dlg:button({
		text = "On/Off",
		tooltip = "Liga/desliga a regra selecionada.",
		onclick = guarded(function()
			local idx = dlg.data.rulesList
			local rule = idx and working.floorRules[idx]
			if not rule then
				setStatus("Selecione uma regra.")
				return
			end
			rule.enabled = not rule.enabled
			refreshRules()
		end),
	})
	dlg:button({
		text = "From Selection...",
		tooltip = "Cria uma regra Cluster a partir da area selecionada no mapa.",
		onclick = guarded(function()
			openRuleFromSelectionDialog(function(rule)
				working.floorRules[#working.floorRules + 1] = rule
				refreshRules()
				setStatus("Regra '" .. rule.name .. "' criada da selecao.")
			end)
		end),
	})
	dlg:endwrap()

	dlg:tab({ id = "tabSettings", text = "Settings" })
	local distMode = working.distribution.mode or 0
	dlg:box({ orient = "vertical", label = "Spacing" })
	dlg:wrap({})
	groupIcon("svg/solid/arrows-left-right.svg", 2554)
	dlg:number({
		id = "cMinDist",
		label = "Min Distance:",
		value = working.spacing.minDistance,
		min = 0,
		max = 20,
		tooltip = "Distancia minima entre quaisquer itens colocados.",
	})
	dlg:number({
		id = "cSameDist",
		label = "Same Item Distance:",
		value = working.spacing.minSameItemDistance,
		min = 0,
		max = 20,
		tooltip = "Distancia minima entre itens IGUAIS.",
	})
	dlg:check({ id = "cDiagonals", text = "Check Diagonals", selected = working.spacing.checkDiagonals })
	dlg:endwrap()
	dlg:endbox()
	dlg:box({ orient = "vertical", label = "Distribution" })
	dlg:wrap({})
	groupIcon("svg/solid/shuffle.svg", 2785)
	dlg:combobox({
		id = "cMode",
		label = "Mode:",
		options = MODE_OPTIONS,
		option = MODE_OPTIONS[distMode + 1] or MODE_OPTIONS[1],
		onchange = function()
			local m = 0
			for i, l in ipairs(MODE_OPTIONS) do
				if dlg.data.cMode == l then
					m = i - 1
				end
			end
			dlg:modify({
				cStrength = { enabled = (m == 1) },
				cCount = { enabled = (m == 1) },
				cGridX = { enabled = (m == 2) },
				cGridY = { enabled = (m == 2) },
				cJitter = { enabled = (m == 2) },
			})
			dlg:repaint()
		end,
	})
	dlg:endwrap()
	dlg:wrap({})
	dlg:slider({
		id = "cStrength",
		label = "Cluster Strength:",
		value = math.floor((working.distribution.clusterStrength or 0.5) * 100),
		min = 0,
		max = 100,
		enabled = (distMode == 1),
	})
	dlg:number({
		id = "cCount",
		label = "Cluster Count:",
		value = working.distribution.clusterCount,
		min = 1,
		max = 20,
		enabled = (distMode == 1),
	})
	dlg:endwrap()
	dlg:wrap({})
	dlg:number({
		id = "cGridX",
		label = "Grid Spacing X:",
		value = working.distribution.gridSpacingX,
		min = 1,
		max = 20,
		enabled = (distMode == 2),
	})
	dlg:number({
		id = "cGridY",
		label = "Grid Spacing Y:",
		value = working.distribution.gridSpacingY,
		min = 1,
		max = 20,
		enabled = (distMode == 2),
	})
	dlg:number({
		id = "cJitter",
		label = "Grid Jitter:",
		value = working.distribution.gridJitter,
		min = 0,
		max = 5,
		enabled = (distMode == 2),
	})
	dlg:endwrap()
	dlg:endbox()
	dlg:box({ orient = "vertical", label = "Limits" })
	dlg:wrap({})
	groupIcon("svg/solid/gauge.svg", 2553)
	dlg:number({
		id = "cMaxItems",
		label = "Max Items Total:",
		value = working.maxItemsTotal,
		min = -1,
		max = 10000,
		tooltip = "-1 = sem limite. Se cortar a geracao, o Apply aplica em lotes.",
	})
	dlg:check({ id = "cSkipBlocked", text = "Skip Blocked Tiles", selected = working.skipBlockedTiles })
	dlg:endwrap()
	dlg:endbox()

	dlg:tab({ id = "tabSeed", text = "Seed" })
	dlg:box({ orient = "vertical", label = "Seed" })
	dlg:wrap({})
	groupIcon("svg/solid/dice.svg", 2148)
	dlg:check({
		id = "useSeed",
		text = "Use Specific Seed",
		selected = false,
		tooltip = "Marcado: usa a seed abaixo (resultado reproduzivel). Desmarcado: seed aleatoria a cada preview.",
	})
	dlg:number({ id = "seedVal", label = "Seed:", value = 0, min = 0, max = 999999999 })
	dlg:endwrap()
	dlg:label({ text = "Mesma seed + mesmo preset + mesma area = mesmo resultado." })
	dlg:endbox()
	dlg:endtabs()

	-- ------------------------------------------------------------------
	-- Preview (sempre visivel, como no C++)
	-- ------------------------------------------------------------------
	local function previewStatus(prefix)
		local msg = prefix .. engine.preview.totalItemsPlaced
			.. " itens (seed " .. engine.currentSeed .. ")"
		if engine.previewWasCapped then
			msg = msg .. " [limite atingido]"
		end
		return msg
	end

	local function currentSeed()
		if dlg.data.useSeed then
			return math.floor(dlg.data.seedVal or 0)
		end
		return 0
	end

	local function setupEngine()
		if #working.floorRules == 0 then
			setStatus("Adicione pelo menos uma regra primeiro (aba Floor Rules).")
			return false
		end
		buildWorkingFromUI()
		engine:setPreset(working)
		engine:setArea(working.area)
		return true
	end

	-- caminho compartilhado de Apply to Map / Reroll and Apply (auto-batch)
	local function doApply()
		local totalApplied, batches = 0, 0
		while batches < MAX_AUTO_BATCHES do
			local wasCapped = engine.previewWasCapped
			local batchCount = engine.preview.totalItemsPlaced
			if not engine:applyPreview() then
				setStatus(engine.lastError)
				refreshPreviewOverlay()
				return
			end
			totalApplied = totalApplied + batchCount
			batches = batches + 1
			if not wasCapped or batches >= MAX_AUTO_BATCHES then
				break
			end
			if not engine:generatePreview(0) then
				break
			end
		end
		refreshPreviewOverlay()
		if batches > 1 then
			setStatus("Aplicado em " .. batches .. " lotes (" .. totalApplied .. " itens). Ctrl+Z desfaz cada lote.")
		else
			setStatus("Aplicado: " .. totalApplied .. " itens. Ctrl+Z desfaz.")
		end
	end

	dlg:box({ orient = "vertical", label = "Preview" })
	dlg:wrap({})
	groupIcon("svg/solid/eye.svg", 2160)
	dlg:button({
		text = "Apply Changes",
		tooltip = "Gera o preview fantasma no mapa com as configuracoes atuais (nada e aplicado ainda).",
		onclick = guarded(function()
			if not setupEngine() then
				return
			end
			setStatus("Gerando preview...")
			if not engine:generatePreview(currentSeed()) then
				setStatus(engine.lastError)
				return
			end
			refreshPreviewOverlay()
			setStatus(previewStatus("Preview: ") .. " - ghost no mapa.")
		end),
	})
	dlg:button({
		text = "Reroll",
		tooltip = "Gera um novo preview com outra seed.",
		onclick = guarded(function()
			if not engine.preview.isValid then
				if not setupEngine() then
					return
				end
			end
			setStatus("Reroll...")
			if not engine:generatePreview(0) then
				setStatus(engine.lastError)
				return
			end
			refreshPreviewOverlay()
			setStatus(previewStatus("Reroll: ") .. " - ghost no mapa.")
		end),
	})
	dlg:button({
		text = "Reroll and Apply",
		tooltip = "Remove a ultima aplicacao, gera um novo preview e ja aplica.",
		onclick = guarded(function()
			local removedLast = false
			if #engine.lastApplied > 0 then
				if not engine:removeLastApplied() then
					setStatus(engine.lastError)
					return
				end
				removedLast = true
			end
			if not engine.preview.isValid then
				if not setupEngine() then
					if removedLast then
						refreshPreviewOverlay()
					end
					return
				end
			end
			if not engine:generatePreview(0) then
				setStatus(engine.lastError)
				if removedLast then
					refreshPreviewOverlay()
				end
				return
			end
			doApply()
		end),
	})
	dlg:button({
		text = "Apply to Map",
		tooltip = "Aplica o preview no mapa (Ctrl+Z desfaz).",
		onclick = guarded(function()
			if not engine.preview.isValid then
				setStatus("Gere um preview primeiro (Apply Changes).")
				return
			end
			doApply()
		end),
	})
	dlg:button({
		text = "Clear Preview",
		onclick = guarded(function()
			engine:clearPreview()
			refreshPreviewOverlay()
			setStatus("Preview descartado.")
		end),
	})
	dlg:button({
		text = "Remove Last Apply",
		tooltip = "Remove do mapa os itens da ultima aplicacao.",
		onclick = guarded(function()
			if not engine:removeLastApplied() then
				setStatus(engine.lastError)
				return
			end
			refreshPreviewOverlay()
			setStatus("Ultima aplicacao removida.")
		end),
	})
	dlg:endwrap()
	dlg:label({ id = "status", text = "No preview generated" })
	dlg:endbox()

	dlg:show({ wait = false })
	return dlg
end

if not app.hasMap() then
	app.alert("Abra um mapa antes de usar o Area Decoration.")
else
	buildDialog()
end

return AreaDecoration
