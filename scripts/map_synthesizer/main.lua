-- Map Synthesizer
-- Aprende padroes de areas de exemplo (seleção) e gera novas areas no mesmo
-- estilo usando Wave Function Collapse (modelo overlapping).
--
-- Fluxo:
--   1. Crie um perfil (ex: "blood", "snow") - um perfil por bioma/tema.
--   2. Selecione uma area de exemplo no mapa e clique em "Aprender da selecao".
--      Repita com 2-3 exemplos para mais variedade.
--   3. Selecione uma area vazia e clique em "Gerar na selecao".
--
-- O aprendizado guarda o brush de ground de cada tile (bordas sao ignoradas e
-- regeneradas pelo auto-border) e a frequencia de doodads por tipo de chao.

-- ============================================================================
-- Storage
-- ============================================================================

local STORE = app.storage("map_synthesizer")
local db = STORE:load() or { profiles = {} }
if type(db.profiles) ~= "table" then
	db.profiles = {}
end

local function saveDb()
	STORE:save(db)
end

local EMPTY = 1 -- indice do label "vazio" (sem tile/sem ground), sempre o primeiro

local function newProfile()
	return {
		labels = { { kind = "empty" } },
		samples = {},
		doodads = { { tiles = 0, stacks = {} } },
	}
end

local function profileNames()
	local names = {}
	for name in pairs(db.profiles) do
		names[#names + 1] = name
	end
	table.sort(names)
	return names
end

-- ============================================================================
-- Labels
-- ============================================================================

local function labelKey(lbl)
	if lbl.kind == "empty" then
		return "empty"
	elseif lbl.kind == "brush" then
		return "brush:" .. lbl.name
	end
	return "ground:" .. tostring(lbl.id)
end

local function findOrAddLabel(profile, lbl)
	local key = labelKey(lbl)
	for i, l in ipairs(profile.labels) do
		if labelKey(l) == key then
			return i
		end
	end
	profile.labels[#profile.labels + 1] = lbl
	profile.doodads[#profile.doodads + 1] = { tiles = 0, stacks = {} }
	return #profile.labels
end

-- tile.groundBrush requer build recente; pcall cobre builds antigos
local function getGroundBrush(tile)
	local ok, brush = pcall(function()
		return tile.groundBrush
	end)
	if ok then
		return brush
	end
	return nil
end

local function tileLabel(tile)
	if not tile or not tile.hasGround then
		return { kind = "empty" }
	end
	local brush = getGroundBrush(tile)
	if brush and brush.name and brush.name ~= "" then
		return { kind = "brush", name = brush.name, fallbackId = tile.ground.id }
	end
	return { kind = "ground", id = tile.ground.id }
end

local function labelDisplay(lbl)
	if lbl.kind == "empty" then
		return "(vazio)"
	elseif lbl.kind == "brush" then
		return lbl.name
	end
	return "item " .. tostring(lbl.id)
end

-- ============================================================================
-- Aprendizado
-- ============================================================================

local function learnFromSelection(profile)
	local sel = app.selection
	if not sel or sel.size == 0 then
		return nil, "Selecione uma area de exemplo no mapa primeiro."
	end
	local b = sel.bounds
	if b.min.z ~= b.max.z then
		return nil, "Selecione tiles de um unico andar."
	end
	local z = b.min.z
	local w = b.max.x - b.min.x + 1
	local h = b.max.y - b.min.y + 1
	if w < 3 or h < 3 then
		return nil, "Area muito pequena (minimo 3x3)."
	end
	if w * h > 65536 then
		return nil, "Area de exemplo muito grande (maximo ~65k tiles)."
	end

	local rows = {}
	local stats = {} -- doodads DESTA amostra (permite remover a amostra depois)
	local drawItems = {} -- pilha de itens por tile (bordas incluidas), so para o preview
	for y = 0, h - 1 do
		local row = {}
		for x = 0, w - 1 do
			local tile = app.map:getTile(b.min.x + x, b.min.y + y, z)
			local idx = findOrAddLabel(profile, tileLabel(tile))
			row[#row + 1] = idx

			if tile and tile.itemCount > 0 then
				local drawIds = {}
				for _, item in ipairs(tile.items) do
					drawIds[#drawIds + 1] = item.id
				end
				drawItems[#drawItems + 1] = { x = x + 1, y = y + 1, ids = drawIds }
			end

			while #stats < #profile.labels do
				stats[#stats + 1] = { tiles = 0, stacks = {} }
			end

			-- estatistica de doodads por label (bordas excluidas; sao
			-- regeneradas pelo auto-border na geracao)
			local st = stats[idx]
			st.tiles = st.tiles + 1
			if tile and idx ~= EMPTY then
				local ids = {}
				for _, item in ipairs(tile.items) do
					if not item.isBorder then
						ids[#ids + 1] = item.id
					end
				end
				if #ids > 0 then
					table.sort(ids)
					local key = table.concat(ids, ",")
					local found
					for _, s in ipairs(st.stacks) do
						if s.key == key then
							found = s
							break
						end
					end
					if found then
						found.count = found.count + 1
					else
						st.stacks[#st.stacks + 1] = { key = key, ids = ids, count = 1 }
					end
				end
			end
		end
		rows[#rows + 1] = row
		app.yield()
	end

	while #stats < #profile.labels do
		stats[#stats + 1] = { tiles = 0, stacks = {} }
	end

	profile.samples[#profile.samples + 1] = { w = w, h = h, rows = rows, doodads = stats, items = drawItems }
	return true
end

-- Combina as estatisticas de doodads de todas as amostras (mais o formato
-- legado agregado em profile.doodads, de amostras antigas).
local function mergedDoodads(profile)
	local merged = {}
	for i = 1, #profile.labels do
		merged[i] = { tiles = 0, stacks = {} }
	end
	local function addStats(src)
		if type(src) ~= "table" then
			return
		end
		for liKey, st in pairs(src) do
			local li = tonumber(liKey)
			local m = li and merged[li]
			if m and type(st) == "table" then
				m.tiles = m.tiles + (st.tiles or 0)
				for _, s in ipairs(st.stacks or {}) do
					local found
					for _, ms in ipairs(m.stacks) do
						if ms.key == s.key then
							found = ms
							break
						end
					end
					if found then
						found.count = found.count + (s.count or 0)
					else
						m.stacks[#m.stacks + 1] = { key = s.key, ids = s.ids, count = s.count or 0 }
					end
				end
			end
		end
	end
	addStats(profile.doodads)
	for _, s in ipairs(profile.samples) do
		addStats(s.doodads)
	end
	return merged
end

-- ============================================================================
-- WFC - extracao de padroes
-- ============================================================================

local function rotateCells(cells, N)
	local out = {}
	for y = 1, N do
		for x = 1, N do
			out[(y - 1) * N + x] = cells[(N - x) * N + y]
		end
	end
	return out
end

local function reflectCells(cells, N)
	local out = {}
	for y = 1, N do
		for x = 1, N do
			out[(y - 1) * N + x] = cells[(y - 1) * N + (N - x + 1)]
		end
	end
	return out
end

local function compilePatterns(profile, N, useSymmetry)
	local patterns = {}
	local index = {}

	local function addPattern(cells)
		local key = table.concat(cells, ",")
		local p = index[key]
		if p then
			patterns[p].weight = patterns[p].weight + 1
		else
			patterns[#patterns + 1] = { cells = cells, weight = 1 }
			index[key] = #patterns
		end
	end

	for _, s in ipairs(profile.samples) do
		for y = 1, s.h - N + 1 do
			for x = 1, s.w - N + 1 do
				local cells = {}
				for dy = 0, N - 1 do
					local row = s.rows[y + dy]
					for dx = 0, N - 1 do
						cells[#cells + 1] = row[x + dx]
					end
				end
				addPattern(cells)
				if useSymmetry then
					local c = cells
					for _ = 1, 3 do
						c = rotateCells(c, N)
						addPattern(c)
					end
					local m = reflectCells(cells, N)
					addPattern(m)
					for _ = 1, 3 do
						m = rotateCells(m, N)
						addPattern(m)
					end
				end
			end
			app.yield()
		end
	end
	return patterns
end

-- direcoes: +x, -x, +y, -y (em celulas da grade de padroes)
local DIRS = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }

-- compat[d][a] = lista de padroes b que podem ficar no offset DIRS[d] de a
-- (a regiao de sobreposicao dos dois padroes precisa coincidir)
local function buildCompat(patterns, N)
	local compat = { {}, {}, {}, {} }
	local P = #patterns
	for d = 1, 4 do
		local dx, dy = DIRS[d][1], DIRS[d][2]
		local yMin, yMax = math.max(1, 1 + dy), math.min(N, N + dy)
		local xMin, xMax = math.max(1, 1 + dx), math.min(N, N + dx)
		for a = 1, P do
			local list = {}
			local ca = patterns[a].cells
			for b = 1, P do
				local cb = patterns[b].cells
				local match = true
				for y = yMin, yMax do
					for x = xMin, xMax do
						if ca[(y - 1) * N + x] ~= cb[(y - dy - 1) * N + (x - dx)] then
							match = false
							break
						end
					end
					if not match then
						break
					end
				end
				if match then
					list[#list + 1] = b
				end
			end
			compat[d][a] = list
		end
		app.yield()
	end
	return compat
end

-- ============================================================================
-- WFC - solver
-- ============================================================================

local Heap = {}
Heap.__index = Heap

function Heap.new()
	return setmetatable({ items = {}, size = 0 }, Heap)
end

function Heap:push(key, value)
	local items = self.items
	local i = self.size + 1
	self.size = i
	items[i] = { key = key, value = value }
	while i > 1 do
		local p = math.floor(i / 2)
		if items[p].key <= items[i].key then
			break
		end
		items[p], items[i] = items[i], items[p]
		i = p
	end
end

function Heap:pop()
	local items = self.items
	local n = self.size
	if n == 0 then
		return nil
	end
	local top = items[1]
	items[1] = items[n]
	items[n] = nil
	self.size = n - 1
	n = n - 1
	local i = 1
	while true do
		local l, r = i * 2, i * 2 + 1
		local smallest = i
		if l <= n and items[l].key < items[smallest].key then
			smallest = l
		end
		if r <= n and items[r].key < items[smallest].key then
			smallest = r
		end
		if smallest == i then
			break
		end
		items[i], items[smallest] = items[smallest], items[i]
		i = smallest
	end
	return top.key, top.value
end

-- Resolve a grade de padroes PW x PH. Retorna wave (set de padroes por celula)
-- ou nil em contradicao.
local function solve(patterns, compat, PW, PH, opts, onProgress)
	local P = #patterns
	local cells = PW * PH
	local wave, count = {}, {}
	for i = 1, cells do
		local set = {}
		for p = 1, P do
			set[p] = true
		end
		wave[i] = set
		count[i] = P
	end

	local heap = Heap.new()
	local queue, qHead, qTail = {}, 1, 0
	local inQueue = {}

	local function enqueue(i)
		if not inQueue[i] then
			inQueue[i] = true
			qTail = qTail + 1
			queue[qTail] = i
		end
	end

	local function restrict(i, allowed)
		local set = wave[i]
		local removed = false
		for p in pairs(set) do
			if not allowed[p] then
				set[p] = nil
				count[i] = count[i] - 1
				removed = true
			end
		end
		if removed then
			if count[i] == 0 then
				return false
			end
			heap:push(count[i], i)
			enqueue(i)
		end
		return true
	end

	-- restringe a moldura externa a padroes 100% vazios para o resultado nao
	-- encostar nas bordas da area de geracao
	if opts.allEmptySet then
		for y = 1, PH do
			for x = 1, PW do
				if x == 1 or y == 1 or x == PW or y == PH then
					if not restrict((y - 1) * PW + x, opts.allEmptySet) then
						return nil
					end
				end
			end
		end
	end

	local function propagate()
		while qHead <= qTail do
			local i = queue[qHead]
			queue[qHead] = nil
			qHead = qHead + 1
			inQueue[i] = nil
			local cx = (i - 1) % PW + 1
			local cy = math.floor((i - 1) / PW) + 1
			local set = wave[i]
			for d = 1, 4 do
				local nx, ny = cx + DIRS[d][1], cy + DIRS[d][2]
				if nx >= 1 and nx <= PW and ny >= 1 and ny <= PH then
					local ni = (ny - 1) * PW + nx
					if count[ni] > 1 then
						local allowed = {}
						local cl = compat[d]
						for p in pairs(set) do
							local lst = cl[p]
							for k = 1, #lst do
								allowed[lst[k]] = true
							end
						end
						if not restrict(ni, allowed) then
							return false
						end
					end
				end
			end
		end
		return true
	end

	if not propagate() then
		return nil
	end

	heap:push(P, math.random(cells))
	local collapsed = 0

	while true do
		local key, i
		while true do
			key, i = heap:pop()
			if key == nil then
				break
			end
			if count[i] == key and key > 1 then
				break
			end
		end
		if key == nil then
			-- celulas nunca tocadas pela propagacao (regioes desconexas)
			local found
			for c = 1, cells do
				if count[c] > 1 then
					found = c
					break
				end
			end
			if not found then
				break
			end
			i = found
		end

		-- colapso ponderado pela frequencia observada
		local set = wave[i]
		local total = 0
		for p in pairs(set) do
			total = total + patterns[p].weight
		end
		local r = math.random() * total
		local chosen
		for p in pairs(set) do
			r = r - patterns[p].weight
			if r <= 0 then
				chosen = p
				break
			end
		end
		if not chosen then
			chosen = next(set)
		end
		for p in pairs(set) do
			if p ~= chosen then
				set[p] = nil
			end
		end
		count[i] = 1
		enqueue(i)
		if not propagate() then
			return nil
		end

		collapsed = collapsed + 1
		if collapsed % 64 == 0 then
			app.yield()
			if onProgress then
				onProgress(collapsed, cells)
			end
		end
	end

	return wave, count
end

-- Converte a grade de padroes resolvida em uma grade W x H de labels.
local function decode(wave, count, patterns, PW, PH, W, H, N)
	local out = {}
	for y = 1, H do
		local row = {}
		for x = 1, W do
			local cx = math.min(x, PW)
			local cy = math.min(y, PH)
			local i = (cy - 1) * PW + cx
			local pid = next(wave[i])
			if pid and count[i] >= 1 then
				local ox, oy = x - cx + 1, y - cy + 1
				row[x] = patterns[pid].cells[(oy - 1) * N + ox]
			else
				row[x] = nil -- contradicao: deixa sem pintar
			end
		end
		out[y] = row
	end
	return out
end

-- Mantem apenas os maxShapes maiores grupos conexos de celulas nao-vazias,
-- respeitando um espacamento minimo (em tiles, por bounding box) entre os
-- shapes mantidos; o resto vira vazio. Retorna quantos shapes ficaram.
local function filterShapes(out, maxShapes, spacing)
	spacing = spacing or 0
	local H = #out
	local W = #out[1]
	local comp = {}
	for y = 1, H do
		comp[y] = {}
	end

	local components = {}
	for sy = 1, H do
		for sx = 1, W do
			if not comp[sy][sx] and out[sy][sx] and out[sy][sx] ~= EMPTY then
				-- BFS 8-conectado a partir de (sx, sy)
				local id = #components + 1
				local c0 = { cells = {}, minx = sx, miny = sy, maxx = sx, maxy = sy }
				components[id] = c0
				comp[sy][sx] = id
				local stack = { { sx, sy } }
				while #stack > 0 do
					local c = stack[#stack]
					stack[#stack] = nil
					c0.cells[#c0.cells + 1] = c
					if c[1] < c0.minx then c0.minx = c[1] end
					if c[1] > c0.maxx then c0.maxx = c[1] end
					if c[2] < c0.miny then c0.miny = c[2] end
					if c[2] > c0.maxy then c0.maxy = c[2] end
					for dy = -1, 1 do
						for dx = -1, 1 do
							local nx, ny = c[1] + dx, c[2] + dy
							if nx >= 1 and nx <= W and ny >= 1 and ny <= H
								and not comp[ny][nx]
								and out[ny][nx] and out[ny][nx] ~= EMPTY then
								comp[ny][nx] = id
								stack[#stack + 1] = { nx, ny }
							end
						end
					end
				end
			end
		end
	end

	table.sort(components, function(a, b)
		return #a.cells > #b.cells
	end)

	local kept = {}
	for _, c0 in ipairs(components) do
		local keep = #kept < maxShapes
		if keep and spacing > 0 then
			for _, k in ipairs(kept) do
				local apartX = c0.maxx + spacing < k.minx or k.maxx + spacing < c0.minx
				local apartY = c0.maxy + spacing < k.miny or k.maxy + spacing < c0.miny
				if not (apartX or apartY) then
					keep = false
					break
				end
			end
		end
		if keep then
			kept[#kept + 1] = c0
		else
			for _, c in ipairs(c0.cells) do
				out[c[2]][c[1]] = EMPTY
			end
		end
	end

	return #kept
end

-- ============================================================================
-- Modo "Formato novo": gera uma silhueta organica inedita e a veste com as
-- camadas (aneis) aprendidas das amostras
-- ============================================================================

-- distancia (chebyshev) de cada celula nao-vazia ate o vazio; fora da grade
-- conta como vazio. dist[y][x] = nil para celulas vazias.
local function distanceToEmpty(rows, w, h)
	local dist = {}
	for y = 1, h do
		dist[y] = {}
	end
	local function isEmpty(x, y)
		if x < 1 or x > w or y < 1 or y > h then
			return true
		end
		local v = rows[y][x]
		return v == nil or v == EMPTY
	end
	local queue, head = {}, 1
	for y = 1, h do
		for x = 1, w do
			if not isEmpty(x, y) then
				local touches = false
				for dy = -1, 1 do
					for dx = -1, 1 do
						if isEmpty(x + dx, y + dy) then
							touches = true
						end
					end
				end
				if touches then
					dist[y][x] = 1
					queue[#queue + 1] = { x, y }
				end
			end
		end
	end
	while head <= #queue do
		local c = queue[head]
		head = head + 1
		local d = dist[c[2]][c[1]]
		for dy = -1, 1 do
			for dx = -1, 1 do
				local nx, ny = c[1] + dx, c[2] + dy
				if nx >= 1 and nx <= w and ny >= 1 and ny <= h
					and not isEmpty(nx, ny) and not dist[ny][nx] then
					dist[ny][nx] = d + 1
					queue[#queue + 1] = { nx, ny }
				end
			end
		end
	end
	return dist
end

-- Estrutura de aneis das amostras: label dominante a cada distancia da borda,
-- tamanho medio (tiles) e complexidade media do contorno (circulo = 1; quanto
-- maior, mais recortado o formato).
local function computeRingStats(profile)
	local hist = {}
	local dmax = 0
	local totalTiles = 0
	local complexitySum = 0
	for _, s in ipairs(profile.samples) do
		local dist = distanceToEmpty(s.rows, s.w, s.h)
		local area, perim = 0, 0
		for y = 1, s.h do
			for x = 1, s.w do
				local d = dist[y][x]
				if d then
					totalTiles = totalTiles + 1
					area = area + 1
					if d == 1 then
						perim = perim + 1
					end
					if d > dmax then
						dmax = d
					end
					hist[d] = hist[d] or {}
					local li = s.rows[y][x]
					hist[d][li] = (hist[d][li] or 0) + 1
				end
			end
		end
		if area > 0 then
			complexitySum = complexitySum + perim / (2 * math.sqrt(math.pi * area))
		end
		app.yield()
	end
	local ringLabel = {}
	for d = 1, dmax do
		local best, bestCount = EMPTY, -1
		for li, c in pairs(hist[d] or {}) do
			if c > bestCount then
				best, bestCount = li, c
			end
		end
		ringLabel[d] = best
	end
	local samples = #profile.samples
	return ringLabel, dmax,
		(samples > 0 and totalTiles / samples or 0),
		(samples > 0 and complexitySum / samples or 1)
end

-- Silhueta organica: uniao de 2-3 "blobs harmonicos" (raio modulado por
-- senos com fases aleatorias). Sempre inedita, nunca blocada. cx0/cy0
-- opcionais posicionam o centro (default: centro da area).
local function generateSilhouette(W, H, targetTiles, cx0, cy0)
	cx0 = cx0 or (W + 1) / 2
	cy0 = cy0 or (H + 1) / 2
	local maxR = (math.min(W, H) - 4) / 2
	local R = math.min(math.sqrt(targetTiles / math.pi), maxR * 0.7)
	if R < 2 then
		R = 2
	end

	local lobes = {}
	for _ = 1, math.random(3, 5) do
		local ang = math.random() * 2 * math.pi
		local off = R * (0.25 + math.random() * 0.85)
		local lobe = {
			cx = cx0 + math.cos(ang) * off,
			cy = cy0 + math.sin(ang) * off,
			r = R * (0.4 + math.random() * 0.35),
			harm = {},
		}
		for k = 2, 7 do
			lobe.harm[k] = { amp = math.random() * 0.6 / k, phase = math.random() * 2 * math.pi }
		end
		lobes[#lobes + 1] = lobe
	end

	local mask = {}
	for y = 1, H do
		mask[y] = {}
		for x = 1, W do
			if x > 2 and x < W - 1 and y > 2 and y < H - 1 then
				for _, lobe in ipairs(lobes) do
					local dx, dy = x - lobe.cx, y - lobe.cy
					local rr = math.sqrt(dx * dx + dy * dy)
					local theta = math.atan(dy, dx)
					local mod = 1
					for k, hm in pairs(lobe.harm) do
						mod = mod + hm.amp * math.sin(k * theta + hm.phase)
					end
					if rr <= lobe.r * mod then
						mask[y][x] = true
						break
					end
				end
			end
		end
	end
	return mask
end

-- Filtro de maioria 3x3: remove celulas isoladas e suaviza o contorno.
local function smoothMask(mask, W, H)
	local out = {}
	for y = 1, H do
		out[y] = {}
		for x = 1, W do
			local n = 0
			for dy = -1, 1 do
				for dx = -1, 1 do
					local r = mask[y + dy]
					if r and r[x + dx] then
						n = n + 1
					end
				end
			end
			if n >= 5 then
				out[y][x] = true
			end
		end
	end
	return out
end

-- Area e complexidade do contorno de uma mascara (circulo = 1).
local function measureMask(mask, W, H)
	local area, perim = 0, 0
	for y = 1, H do
		for x = 1, W do
			if mask[y][x] then
				area = area + 1
				local touches = false
				for dy = -1, 1 do
					for dx = -1, 1 do
						local r = mask[y + dy]
						if not (r and r[x + dx]) then
							touches = true
						end
					end
				end
				if touches then
					perim = perim + 1
				end
			end
		end
	end
	if area == 0 then
		return 0, 1
	end
	return area, perim / (2 * math.sqrt(math.pi * area))
end

-- Recorta o contorno: alterna "mordidas" (remove) e protuberancias (adiciona)
-- em pontos aleatorios da borda, deixando o formato irregular como os exemplos.
local function addDetail(mask, W, H, count, baseR)
	for _ = 1, count do
		local boundary = {}
		for y = 3, H - 2 do
			for x = 3, W - 2 do
				if mask[y][x] then
					local touch = false
					for dy = -1, 1 do
						for dx = -1, 1 do
							local r = mask[y + dy]
							if not (r and r[x + dx]) then
								touch = true
							end
						end
					end
					if touch then
						boundary[#boundary + 1] = { x, y }
					end
				end
			end
		end
		if #boundary == 0 then
			return mask
		end
		local b = boundary[math.random(#boundary)]
		local rr = math.max(1.5, baseR * (0.12 + math.random() * 0.22))
		local add = math.random() < 0.5
		local ir = math.ceil(rr)
		for dy = -ir, ir do
			for dx = -ir, ir do
				if dx * dx + dy * dy <= rr * rr then
					local nx, ny = b[1] + dx, b[2] + dy
					if nx > 2 and nx < W - 1 and ny > 2 and ny < H - 1 then
						mask[ny][nx] = add and true or nil
					end
				end
			end
		end
	end
	return mask
end

-- Limpeza leve: tira celulas quase isoladas e fecha buracos de 1 tile, sem
-- arredondar o contorno (diferente do smoothMask).
local function despeckle(mask, W, H)
	for y = 1, H do
		for x = 1, W do
			local n = 0
			for dy = -1, 1 do
				for dx = -1, 1 do
					if not (dx == 0 and dy == 0) then
						local r = mask[y + dy]
						if r and r[x + dx] then
							n = n + 1
						end
					end
				end
			end
			if mask[y][x] and n <= 1 then
				mask[y][x] = nil
			elseif not mask[y][x] and n >= 7 and x > 2 and x < W - 1 and y > 2 and y < H - 1 then
				mask[y][x] = true
			end
		end
	end
	return mask
end

-- Engorda a mascara em r passos (chebyshev), respeitando a margem.
local function dilateMask(mask, W, H, r)
	local function touches(x, y)
		for dy = -1, 1 do
			for dx = -1, 1 do
				local row = mask[y + dy]
				if row and row[x + dx] then
					return true
				end
			end
		end
		return false
	end
	for _ = 1, r do
		local add = {}
		for y = 1, H do
			for x = 1, W do
				if not mask[y][x] and x > 2 and x < W - 1 and y > 2 and y < H - 1 and touches(x, y) then
					add[#add + 1] = { x, y }
				end
			end
		end
		for _, c in ipairs(add) do
			mask[c[2]][c[1]] = true
		end
	end
	return mask
end

local function maskBBox(mask, W, H)
	local minx, miny, maxx, maxy
	for y = 1, H do
		for x = 1, W do
			if mask[y][x] then
				if not minx or x < minx then minx = x end
				if not maxx or x > maxx then maxx = x end
				if not miny or y < miny then miny = y end
				if not maxy or y > maxy then maxy = y end
			end
		end
	end
	return { minx = minx or 1, miny = miny or 1, maxx = maxx or 0, maxy = maxy or 0 }
end

-- Preenche bolsoes vazios fechados dentro da mascara (sem "ilhas" de vazio,
-- que gerariam bordas internas estranhas).
local function fillHoles(mask, W, H)
	local outside = {}
	for y = 1, H do
		outside[y] = {}
	end
	local queue, head = {}, 1
	local function push(x, y)
		if x >= 1 and x <= W and y >= 1 and y <= H and not outside[y][x] and not mask[y][x] then
			outside[y][x] = true
			queue[#queue + 1] = { x, y }
		end
	end
	for x = 1, W do
		push(x, 1)
		push(x, H)
	end
	for y = 1, H do
		push(1, y)
		push(W, y)
	end
	while head <= #queue do
		local c = queue[head]
		head = head + 1
		push(c[1] + 1, c[2])
		push(c[1] - 1, c[2])
		push(c[1], c[2] + 1)
		push(c[1], c[2] - 1)
	end
	for y = 1, H do
		for x = 1, W do
			if not mask[y][x] and not outside[y][x] then
				mask[y][x] = true
			end
		end
	end
	return mask
end

-- Caminho serpenteante (rio/trilha): polilinha por deslocamento de ponto
-- medio, varrida com discos. A largura do miolo (halfCore) nunca cai abaixo
-- de ~90% do pedido; a variacao organica so engorda.
local function generatePath(W, H, fringeDepth, halfCore)
	local horizontal = math.random() < 0.5
	local pts
	if horizontal then
		pts = {
			{ x = 3, y = H * (0.3 + math.random() * 0.4) },
			{ x = W - 2, y = H * (0.3 + math.random() * 0.4) },
		}
	else
		pts = {
			{ x = W * (0.3 + math.random() * 0.4), y = 3 },
			{ x = W * (0.3 + math.random() * 0.4), y = H - 2 },
		}
	end

	-- subdivide deslocando o ponto medio na perpendicular
	local mag = math.min(W, H) * 0.18
	for _ = 1, 4 do
		local out = {}
		for i = 1, #pts - 1 do
			local a, b = pts[i], pts[i + 1]
			out[#out + 1] = a
			local dx, dy = b.x - a.x, b.y - a.y
			local len = math.sqrt(dx * dx + dy * dy)
			if len > 0 then
				local off = (math.random() * 2 - 1) * mag
				out[#out + 1] = {
					x = math.max(3, math.min(W - 2, (a.x + b.x) / 2 - dy / len * off)),
					y = math.max(3, math.min(H - 2, (a.y + b.y) / 2 + dx / len * off)),
				}
			end
		end
		out[#out + 1] = pts[#pts]
		pts = out
		mag = mag * 0.55
	end

	local mask = {}
	for y = 1, H do
		mask[y] = {}
	end
	local function stamp(cx, cy, r)
		local ir = math.ceil(r)
		for dy = -ir, ir do
			for dx = -ir, ir do
				if dx * dx + dy * dy <= r * r then
					local nx = math.floor(cx + dx + 0.5)
					local ny = math.floor(cy + dy + 0.5)
					if nx > 2 and nx < W - 1 and ny > 2 and ny < H - 1 then
						mask[ny][nx] = true
					end
				end
			end
		end
	end

	-- variacao organica apenas no miolo: wmod fica entre 0.9 e 1.3, entao a
	-- largura pedida e praticamente garantida em todo o percurso
	local p1 = math.random() * 2 * math.pi
	local p2 = math.random() * 2 * math.pi
	local total = #pts
	for i = 1, total - 1 do
		local a, b = pts[i], pts[i + 1]
		local steps = math.max(1, math.ceil(math.max(math.abs(b.x - a.x), math.abs(b.y - a.y))))
		for s = 0, steps do
			local t = (i - 1 + s / steps) / (total - 1)
			local x = a.x + (b.x - a.x) * s / steps
			local y = a.y + (b.y - a.y) * s / steps
			local wmod = 1.1 + 0.15 * math.sin(t * 9 + p1) + 0.05 * math.sin(t * 23 + p2)
			stamp(x, y, math.max(1, fringeDepth + halfCore * wmod))
		end
	end
	return mask
end

-- Regra de contorno: nenhuma parede reta com 3+ celulas alinhadas (geraria 3+
-- bordas retas em sequencia no auto-border). Quebra cada sequencia inserindo
-- um "dente" para fora no meio dela, repetindo ate convergir.
local function breakStraightRuns(mask, W, H)
	local function inBounds(x, y)
		return x > 2 and x < W - 1 and y > 2 and y < H - 1
	end
	for _ = 1, 10 do
		local changed = false

		-- paredes verticais: face leste (dx=1) e oeste (dx=-1)
		for _, dx in ipairs({ 1, -1 }) do
			for x = 1, W do
				local run = 0
				for y = 1, H + 1 do
					local wall = y <= H and mask[y][x]
						and not (mask[y] and mask[y][x + dx])
					if wall then
						run = run + 1
					else
						if run >= 3 then
							local my = y - 1 - math.floor(run / 2)
							if inBounds(x + dx, my) then
								mask[my][x + dx] = true
								changed = true
							end
						end
						run = 0
					end
				end
			end
		end

		-- paredes horizontais: face sul (dy=1) e norte (dy=-1)
		for _, dy in ipairs({ 1, -1 }) do
			for y = 1, H do
				local run = 0
				for x = 1, W + 1 do
					local wall = x <= W and mask[y][x]
						and not (mask[y + dy] and mask[y + dy][x])
					if wall then
						run = run + 1
					else
						if run >= 3 then
							local mx = x - 1 - math.floor(run / 2)
							if inBounds(mx, y + dy) then
								mask[y + dy][mx] = true
								changed = true
							end
						end
						run = 0
					end
				end
			end
		end

		if not changed then
			break
		end
	end
	return mask
end

-- Pipeline do modo "Formato novo": silhueta inedita + aneis aprendidos.
local function synthesizeShape(profile, opts, W, H, setStatus)
	if #profile.samples == 0 then
		return nil, "O perfil ainda nao tem amostras. Use 'Aprender da selecao' primeiro."
	end
	if W < 10 or H < 10 then
		return nil, "Area de geracao muito pequena para o modo Formato novo (minimo 10x10)."
	end
	if W * H > 40000 then
		return nil, "Area de geracao muito grande (maximo ~40.000 tiles)."
	end

	local seed = opts.seed
	if not seed or seed == 0 then
		seed = os.time()
	end
	math.randomseed(seed)

	setStatus("Analisando aneis das amostras...")
	local ringLabel, dmax, avgTiles, targetC = computeRingStats(profile)
	if dmax == 0 or avgTiles <= 0 then
		return nil, "Amostras sem conteudo util."
	end

	-- profundidade da franja: quantos aneis existem antes do label central
	local interiorLabel = ringLabel[dmax]
	local fringeDepth = dmax - 1
	for d = 1, dmax do
		if ringLabel[d] == interiorLabel then
			fringeDepth = d - 1
			break
		end
	end

	local target = avgTiles * (opts.sizePct or 1)
	local baseR = math.sqrt(target / math.pi)
	-- quanto mais recortadas as amostras, mais mordidas/protuberancias;
	-- roughPct permite exagerar/suavizar manualmente
	local rough = opts.roughPct or 1
	targetC = 1 + (targetC - 1) * rough
	local nDetail = math.floor((6 + 18 * math.max(0, targetC - 1)) * rough)

	local mask
	local placedCount = 1

	if opts.shapeKind == "path" then
		-- caminho (rio/trilha): largura do centro controlada pelo usuario
		local coreW = opts.coreWidth or 0
		if coreW <= 0 then
			coreW = math.max(2, 2 * (dmax - fringeDepth))
		end
		setStatus("Gerando caminho...")
		mask = generatePath(W, H, fringeDepth, coreW / 2)
		fillHoles(mask, W, H)
		despeckle(mask, W, H)
		breakStraightRuns(mask, W, H)
	else
		-- mancha(s): coloca ate maxShapes silhuetas respeitando o espacamento;
		-- 0 = preenche a area com quantos shapes couberem
		local wanted = opts.maxShapes or 1
		local unlimited = wanted == 0
		local shapes = unlimited and 64 or math.max(1, wanted)
		local spacing = opts.spacing or 0
		local neededDist = nil
		if (opts.coreWidth or 0) > 0 then
			neededDist = fringeDepth + math.ceil(opts.coreWidth / 2)
		end

		mask = {}
		for y = 1, H do
			mask[y] = {}
		end
		local placedBoxes = {}
		placedCount = 0

		local failStreak = 0
		for s = 1, shapes do
			if unlimited then
				setStatus("Gerando silhueta " .. s .. " (preenchendo a area)...")
			else
				setStatus("Gerando silhueta " .. s .. "/" .. shapes .. "...")
			end
			local best, bestScore, bestBox
			for _ = 1, 8 do
				-- com varios shapes, sorteia o centro de cada um
				local cx, cy
				if shapes > 1 then
					local m = baseR * 1.2 + 3
					if W - 2 * m > 1 then
						cx = m + math.random() * (W - 2 * m)
					end
					if H - 2 * m > 1 then
						cy = m + math.random() * (H - 2 * m)
					end
				end
				local cand = smoothMask(generateSilhouette(W, H, target, cx, cy), W, H)
				cand = addDetail(cand, W, H, nDetail, baseR)
				fillHoles(cand, W, H)
				despeckle(cand, W, H)

				-- largura minima do centro: engorda ate o miolo ter a
				-- profundidade necessaria
				if neededDist then
					local dist = distanceToEmpty(cand, W, H)
					local dM = 0
					for y = 1, H do
						for x = 1, W do
							local d = dist[y][x]
							if d and d > dM then
								dM = d
							end
						end
					end
					if dM > 0 and dM < neededDist then
						dilateMask(cand, W, H, neededDist - dM)
					end
				end

				-- quebra retas antes do bbox, para o espacamento valer no
				-- contorno final
				breakStraightRuns(cand, W, H)

				local area, c = measureMask(cand, W, H)
				if area > 0 then
					local box = maskBBox(cand, W, H)
					-- respeita o espacamento com os shapes ja colocados
					local fits = true
					for _, k in ipairs(placedBoxes) do
						local apartX = box.maxx + spacing < k.minx or k.maxx + spacing < box.minx
						local apartY = box.maxy + spacing < k.miny or k.maxy + spacing < box.miny
						if not (apartX or apartY) then
							fits = false
							break
						end
					end
					if fits then
						local score = math.abs(area - target) / target + 0.8 * math.abs(c - targetC)
						if not bestScore or score < bestScore then
							best, bestScore, bestBox = cand, score, box
						end
					end
				end
				app.yield()
			end
			if best then
				for y = 1, H do
					for x = 1, W do
						if best[y][x] then
							mask[y][x] = true
						end
					end
				end
				placedBoxes[#placedBoxes + 1] = bestBox
				placedCount = placedCount + 1
				failStreak = 0
			else
				failStreak = failStreak + 1
				-- area saturada: varios shapes seguidos sem lugar
				if unlimited and failStreak >= 3 then
					break
				end
			end
		end

		if placedCount == 0 then
			return nil, "Nenhum shape coube na area (reduza espacamento/tamanho ou aumente a area)."
		end
	end

	-- veste a silhueta: label por distancia ate a borda, como nas amostras
	local rows = {}
	for y = 1, H do
		rows[y] = {}
		for x = 1, W do
			rows[y][x] = mask[y][x] and 0 or EMPTY -- 0 = placeholder nao-vazio
		end
	end
	local dist = distanceToEmpty(rows, W, H)
	local out = {}
	for y = 1, H do
		out[y] = {}
		for x = 1, W do
			local d = dist[y][x]
			if d then
				out[y][x] = ringLabel[math.min(d, dmax)]
			else
				out[y][x] = EMPTY
			end
		end
	end
	-- remove sobras desconexas alem dos shapes colocados
	placedCount = filterShapes(out, math.max(1, placedCount))
	return out, placedCount
end

-- ============================================================================
-- Aplicacao no mapa
-- ============================================================================

-- Pinta a grade no mapa. Retorna a lista de posicoes pintadas e a lista de
-- tiles vizinhos que ganharam bordas (ambas usadas pelo modo clipboard).
local function applyResult(profile, out, originX, originY, z, opts)
	local painted, borderAdded = {}, {}
	app.transaction("Map Synthesizer", function()
		for y = 1, #out do
			local row = out[y]
			for x = 1, #row do
				local li = row[x]
				if li and li ~= EMPTY then
					local lbl = profile.labels[li]
					local mx, my = originX + x - 1, originY + y - 1
					local tile = app.map:getOrCreateTile(mx, my, z)
					if opts.wipe then
						for _, item in ipairs(tile.items) do
							tile:removeItem(item)
						end
					end
					local ok = false
					if lbl.kind == "brush" then
						ok = tile:applyBrush(lbl.name, false)
					end
					if not ok then
						local gid = (lbl.kind == "ground" and lbl.id) or lbl.fallbackId
						if gid then
							-- ids vindos do JSON podem chegar como float
							tile.ground = math.floor(gid)
							ok = true
						end
					end
					if ok then
						painted[#painted + 1] = { x = mx, y = my, li = li }
					end
				end
			end
		end

		-- auto-border: tiles pintados + vizinhos (bordas caem em tiles vizinhos)
		if opts.borderize then
			local paintedSet = {}
			for _, p in ipairs(painted) do
				paintedSet[p.x .. ":" .. p.y] = true
			end
			local seen = {}
			for _, p in ipairs(painted) do
				for dy = -1, 1 do
					for dx = -1, 1 do
						seen[(p.x + dx) .. ":" .. (p.y + dy)] = { p.x + dx, p.y + dy }
					end
				end
			end
			for key, pos in pairs(seen) do
				local t = app.map:getOrCreateTile(pos[1], pos[2], z)
				local wasEmpty = not paintedSet[key] and not t.hasGround and t.itemCount == 0
				t:borderize()
				if wasEmpty and t.itemCount > 0 then
					borderAdded[#borderAdded + 1] = { x = pos[1], y = pos[2] }
				end
			end
		end

		-- doodads: itens raros nas amostras (ex: arvore grande) sao "landmarks"
		-- e entram ~na mesma quantidade por geracao; o resto e espalhado com a
		-- frequencia aprendida por label
		if opts.doodads then
			local sampleCount = math.max(1, #profile.samples)
			local doodadStats = mergedDoodads(profile)
			local tilesByLabel = {}
			for _, p in ipairs(painted) do
				local list = tilesByLabel[p.li]
				if not list then
					list = {}
					tilesByLabel[p.li] = list
				end
				list[#list + 1] = p
			end

			local used = {}
			local function placeStack(p, s)
				local tile = app.map:getTile(p.x, p.y, z)
				if tile then
					for _, id in ipairs(s.ids) do
						tile:addItem(math.floor(id))
					end
				end
				used[p.x .. ":" .. p.y] = true
			end

			for li, list in pairs(tilesByLabel) do
				local st = doodadStats[li]
				if st and st.tiles > 0 and #st.stacks > 0 then
					local common = {}
					for _, s in ipairs(st.stacks) do
						local expected = s.count / sampleCount
						if expected <= 2 then
							-- landmark: coloca ~expected unidades em tiles
							-- aleatorios deste label
							local n = math.floor(expected * opts.density)
							if math.random() < (expected * opts.density - n) then
								n = n + 1
							end
							for _ = 1, n do
								for _ = 1, 8 do -- tenta achar tile livre
									local p = list[math.random(#list)]
									if not used[p.x .. ":" .. p.y] then
										placeStack(p, s)
										break
									end
								end
							end
						else
							common[#common + 1] = s
						end
					end

					for _, p in ipairs(list) do
						if not used[p.x .. ":" .. p.y] then
							local r = math.random()
							local acc = 0
							for _, s in ipairs(common) do
								acc = acc + (s.count / st.tiles) * opts.density
								if r < acc then
									placeStack(p, s)
									break
								end
							end
						end
					end
				end
			end
		end

	end)
	return painted, borderAdded
end

-- ============================================================================
-- Geracao (orquestra: padroes -> solver -> aplicacao)
-- ============================================================================

-- Roda o pipeline WFC para uma area W x H. Retorna a grade de labels (com o
-- filtro de shapes ja aplicado) e o numero de shapes mantidos.
local function synthesize(profile, opts, W, H, setStatus)
	if #profile.samples == 0 then
		return nil, "O perfil ainda nao tem amostras. Use 'Aprender da selecao' primeiro."
	end

	local N = opts.patternSize
	if W < N or H < N then
		return nil, "Area de geracao menor que o tamanho do padrao (" .. N .. "x" .. N .. ")."
	end
	if W * H > 20000 then
		return nil, "Area de geracao muito grande (maximo ~20.000 tiles). Gere em partes."
	end

	setStatus("Compilando padroes...")
	local patterns = compilePatterns(profile, N, opts.symmetry)
	if #patterns < 2 then
		return nil, "Poucos padroes aprendidos. Adicione amostras maiores/mais variadas."
	end

	local PW, PH = W - N + 1, H - N + 1
	if #patterns * PW * PH > 3000000 then
		return nil, ("Combinacao pesada demais (%d padroes x %d celulas). " ..
			"Use uma area menor, padrao 2x2 ou desligue a simetria."):format(#patterns, PW * PH)
	end

	setStatus("Calculando adjacencias (" .. #patterns .. " padroes)...")
	local compat = buildCompat(patterns, N)

	-- padroes inteiramente vazios, usados para fechar a moldura
	local allEmptySet = nil
	if opts.closeBorder then
		allEmptySet = {}
		local any = false
		for pid, pat in ipairs(patterns) do
			local allEmpty = true
			for _, c in ipairs(pat.cells) do
				if c ~= EMPTY then
					allEmpty = false
					break
				end
			end
			if allEmpty then
				allEmptySet[pid] = true
				any = true
			end
		end
		if not any then
			allEmptySet = nil -- amostras sem area vazia: nao da para fechar
		end
	end

	local seed = opts.seed
	if not seed or seed == 0 then
		seed = os.time()
	end

	local wave, count
	local attempts = 8
	for attempt = 1, attempts do
		setStatus(("Gerando... tentativa %d/%d"):format(attempt, attempts))
		math.randomseed(seed + attempt * 7919)
		wave, count = solve(patterns, compat, PW, PH, { allEmptySet = allEmptySet }, function(done, total)
			setStatus(("Gerando... tentativa %d/%d (%d/%d)"):format(attempt, attempts, done, total))
		end)
		if wave then
			break
		end
	end
	if not wave then
		return nil, "Nao convergiu apos " .. attempts .. " tentativas. Tente outra seed ou amostras maiores."
	end

	local out = decode(wave, count, patterns, PW, PH, W, H, N)
	local shapesKept = nil
	if opts.maxShapes and opts.maxShapes > 0 then
		shapesKept = filterShapes(out, opts.maxShapes, opts.spacing)
	end
	return out, shapesKept
end

-- Gera dentro da selecao atual (ou na `area` informada, usada pelo Reroll)
-- e pinta direto no mapa. Retorna tiles pintados, extra e a area usada.
local function generate(profile, opts, setStatus, area)
	if not area then
		local sel = app.selection
		if not sel or sel.size == 0 then
			return nil, "Selecione a area de destino no mapa."
		end
		local b = sel.bounds
		if b.min.z ~= b.max.z then
			return nil, "Selecione tiles de um unico andar."
		end
		area = {
			x = b.min.x,
			y = b.min.y,
			z = b.min.z,
			w = b.max.x - b.min.x + 1,
			h = b.max.y - b.min.y + 1,
		}
	end

	local synth = (opts.mode == "wfc") and synthesize or synthesizeShape
	local out, extra = synth(profile, opts, area.w, area.h, setStatus)
	if not out then
		return nil, extra
	end

	setStatus("Aplicando no mapa...")
	local painted = applyResult(profile, out, area.x, area.y, area.z, opts)
	app.refresh()
	return #painted, extra, area
end

-- Gera numa area temporaria, copia para o clipboard interno e desfaz a
-- pintura. O shape fica disponivel para colar (Ctrl+V) onde o usuario quiser.
-- `area` opcional (usada pelo Reroll); retorna tambem a area usada.
local function generateToClipboard(profile, opts, setStatus, area)
	if not area then
		local sel = app.selection
		if sel and sel.size > 0 then
			local b = sel.bounds
			if b.min.z ~= b.max.z then
				return nil, "Selecione tiles de um unico andar."
			end
			area = {
				x = b.min.x,
				y = b.min.y,
				z = b.min.z,
				w = b.max.x - b.min.x + 1,
				h = b.max.y - b.min.y + 1,
			}
		else
			-- sem selecao: quadrado centrado na camera
			local cam = app.getCameraPosition()
			if not cam then
				return nil, "Nao foi possivel obter a posicao da camera."
			end
			local side = opts.genSize or 36
			area = {
				x = cam.x - math.floor(side / 2),
				y = cam.y - math.floor(side / 2),
				z = cam.z,
				w = side,
				h = side,
			}
		end
	end
	local originX, originY, z, W, H = area.x, area.y, area.z, area.w, area.h

	local editor = app.editor
	if not editor then
		return nil, "Editor indisponivel."
	end

	local synth = (opts.mode == "wfc") and synthesize or synthesizeShape
	local out, extra = synth(profile, opts, W, H, setStatus)
	if not out then
		return nil, extra
	end

	setStatus("Gerando shape temporario...")
	local painted, borderAdded = applyResult(profile, out, originX, originY, z, opts)
	if #painted == 0 then
		-- nada foi pintado: nao ha o que desfazer/copiar
		return nil, "Nada foi gerado (tente outra seed ou mais amostras)."
	end

	-- seleciona o shape (sessao interna: nao entra no historico de undo)
	setStatus("Copiando para o clipboard...")
	local selection = app.selection
	selection:transaction(function()
		selection:clear()
		for _, p in ipairs(painted) do
			local t = app.map:getTile(p.x, p.y, z)
			if t then
				selection:add(t)
			end
		end
		for _, p in ipairs(borderAdded) do
			local t = app.map:getTile(p.x, p.y, z)
			if t then
				selection:add(t)
			end
		end
	end)

	app.copy()

	-- limpa a selecao antes do undo (os tiles temporarios vao sumir)
	selection:transaction(function()
		selection:clear()
	end)
	editor:undo()
	app.refresh()

	-- app.canPaste existe a partir do build com o bypass de modo no app.copy
	if app.canPaste and not app.canPaste() then
		return nil, "A copia falhou (clipboard vazio). Tente novamente em modo selecao."
	end
	return #painted, extra, area
end

-- ============================================================================
-- UI
-- ============================================================================

local buildDialog -- declaracao adiantada (dialogo se reconstroi ao criar/excluir perfil)

local function profileInfo(name)
	local p = db.profiles[name]
	if not p then
		return "Nenhum perfil selecionado. Crie um perfil para comecar."
	end
	local names = {}
	for i, lbl in ipairs(p.labels) do
		if i ~= EMPTY then
			names[#names + 1] = labelDisplay(lbl)
		end
	end
	local groundList = #names > 0 and table.concat(names, ", ") or "nenhum"
	if #groundList > 120 then
		groundList = groundList:sub(1, 117) .. "..."
	end
	return ("%d area(s) aprendida(s) | grounds: %s"):format(#p.samples, groundList)
end

-- Composicao de preview requer Image.blank/blit (build recente)
local hasPreview = (Image ~= nil and Image.blank ~= nil)

-- Renderiza uma amostra como imagem, com o sprite do ground de cada tile.
local function renderSamplePreview(profile, sample, maxW, maxH)
	if not hasPreview then
		return nil
	end
	local px = math.max(2, math.min(16, math.floor(maxW / sample.w), math.floor(maxH / sample.h)))
	local img = Image.blank(sample.w * px, sample.h * px, 12, 12, 12)
	if not img.valid then
		return nil
	end
	local cache = {}
	for y = 1, sample.h do
		local row = sample.rows[y]
		for x = 1, sample.w do
			local li = row[x]
			if li and li ~= EMPTY then
				local spr = cache[li]
				if spr == nil then
					local lbl = profile.labels[li]
					local gid = lbl and ((lbl.kind == "ground" and lbl.id) or lbl.fallbackId)
					spr = false
					if gid then
						local s = Image.fromItemSprite(math.floor(gid))
						if s and s.valid then
							spr = s:resize(px, px, false)
						end
					end
					cache[li] = spr
				end
				if spr then
					img:blit(spr, (x - 1) * px, (y - 1) * px)
				end
			end
		end
	end

	-- itens por cima do ground (bordas, vegetacao, arvores), na ordem salva;
	-- sprites maiores que 32px sao ancorados no canto inferior-direito do tile
	local itemCache = {}
	for _, e in ipairs(sample.items or {}) do
		for _, id in ipairs(e.ids) do
			local spr = itemCache[id]
			if spr == nil then
				spr = false
				local s = Image.fromItemSprite(math.floor(id))
				if s and s.valid then
					local w = math.max(1, math.floor(s.width / 32 * px + 0.5))
					local h = math.max(1, math.floor(s.height / 32 * px + 0.5))
					spr = s:resize(w, h, false)
				end
				itemCache[id] = spr
			end
			if spr then
				img:blit(spr, e.x * px - spr.width, e.y * px - spr.height)
			end
		end
	end
	return img
end

-- Dimensoes fixas da janela de preview
local PREVIEW_W, PREVIEW_H = 240, 180

-- Preview em "tela" de tamanho fixo, com a amostra centralizada (o layout do
-- dialogo nao muda conforme a proporcao da area).
local function renderPreviewCanvas(profile, sample)
	local img = renderSamplePreview(profile, sample, PREVIEW_W - 4, PREVIEW_H - 4)
	if not img then
		return nil
	end
	local canvas = Image.blank(PREVIEW_W, PREVIEW_H, 12, 12, 12)
	canvas:blit(img, math.floor((PREVIEW_W - img.width) / 2), math.floor((PREVIEW_H - img.height) / 2))
	return canvas
end

-- Itens da lista de amostras (com thumbnail quando a amostra nao e enorme).
local function sampleListItems(profile)
	local items = {}
	if profile then
		for i, s in ipairs(profile.samples) do
			local it = { text = ("Area %d  (%dx%d)"):format(i, s.w, s.h) }
			if hasPreview and s.w * s.h <= 2500 then
				local thumb = renderSamplePreview(profile, s, 48, 48)
				if thumb then
					it.image = thumb
				end
			end
			items[#items + 1] = it
		end
	end
	return items
end

buildDialog = function(selectedName)
	local names = profileNames()
	if #names == 0 then
		names = { "" }
	end
	local current = selectedName or names[1]

	local dlg
	dlg = Dialog({ title = "Map Synthesizer", resizable = true })

	local function setStatus(text)
		dlg:modify({ status = { text = text } })
		dlg:repaint()
		app.yield()
	end

	-- dialogo nao-modal: evita reentrar em aprender/gerar enquanto um roda
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

	local function currentProfile()
		local name = dlg.data.profile
		if not name or name == "" then
			return nil, nil
		end
		return db.profiles[name], name
	end

	local function refreshSamples()
		local profile, name = currentProfile()
		dlg:modify({
			samplesList = { items = sampleListItems(profile) },
			info = { text = profileInfo(name) },
		})
		dlg:repaint()
	end

	-- ------------------------------------------------------------------
	-- 1) Perfil
	-- ------------------------------------------------------------------
	dlg:box({ orient = "vertical", label = "1) Perfil (um por bioma/tema: blood, snow...)" })
	dlg:combobox({
		id = "profile",
		label = "Perfil",
		options = names,
		option = current,
		onchange = function()
			refreshSamples()
		end,
	})
	dlg:label({ id = "info", text = profileInfo(current) })
	dlg:wrap({})
	dlg:input({ id = "newName", label = "Novo perfil" })
	dlg:button({
		text = "Criar",
		onclick = function()
			local name = (dlg.data.newName or ""):gsub("^%s+", ""):gsub("%s+$", "")
			if name == "" then
				app.alert("Digite um nome para o novo perfil.")
				return
			end
			if not db.profiles[name] then
				db.profiles[name] = newProfile()
				saveDb()
			end
			dlg:close()
			buildDialog(name)
		end,
	})
	dlg:button({
		text = "Excluir",
		onclick = function()
			local _, name = currentProfile()
			if not name then
				return
			end
			local result = app.alert({
				title = "Excluir perfil",
				text = "Excluir o perfil '" .. name .. "' e todo o seu aprendizado?",
				buttons = { "Sim", "Nao" },
			})
			if result == 1 then
				db.profiles[name] = nil
				saveDb()
				dlg:close()
				buildDialog()
			end
		end,
	})
	dlg:endwrap()
	dlg:endbox()

	-- ------------------------------------------------------------------
	-- 2) Areas de exemplo (o que o gerador aprende)
	-- ------------------------------------------------------------------
	dlg:box({ orient = "vertical", label = "2) Areas de exemplo" })
	dlg:label({ text = "Selecione uma area no mapa e clique em 'Aprender'. Mais areas = mais variedade." })
	dlg:wrap({})
	dlg:list({
		id = "samplesList",
		items = sampleListItems(db.profiles[current]),
		icon_size = 48,
		width = 250,
		height = PREVIEW_H + 10,
		max_height = PREVIEW_H + 10,
		expand = false,
		onchange = function()
			if not hasPreview then
				return
			end
			local profile = currentProfile()
			local idx = dlg.data.samplesList
			if profile and idx and idx > 0 and profile.samples[idx] then
				local img = renderPreviewCanvas(profile, profile.samples[idx])
				if img then
					dlg:modify({ preview = { image = img, smooth = false } })
					dlg:repaint()
				end
			end
		end,
	})
	if hasPreview then
		dlg:image({ id = "preview", image = Image.blank(PREVIEW_W, PREVIEW_H, 12, 12, 12), smooth = false })
	else
		dlg:label({ text = "(preview disponivel apos recompilar o editor)" })
	end
	dlg:endwrap()
	dlg:wrap({})
	dlg:button({
		text = "Aprender da selecao",
		onclick = guarded(function()
			local profile = currentProfile()
			if not profile then
				app.alert("Crie/selecione um perfil primeiro.")
				return
			end
			setStatus("Aprendendo da selecao...")
			local ok, err = learnFromSelection(profile)
			if not ok then
				setStatus(err)
				return
			end
			saveDb()
			refreshSamples()
			setStatus("Area aprendida! (" .. #profile.samples .. " no total)")
		end),
	})
	dlg:button({
		text = "Remover area",
		onclick = guarded(function()
			local profile = currentProfile()
			if not profile then
				app.alert("Crie/selecione um perfil primeiro.")
				return
			end
			local idx = dlg.data.samplesList
			if not idx or idx < 1 or not profile.samples[idx] then
				setStatus("Selecione uma area na lista para remover.")
				return
			end
			table.remove(profile.samples, idx)
			saveDb()
			refreshSamples()
			setStatus("Area removida (" .. #profile.samples .. " restante(s)).")
		end),
	})
	dlg:endwrap()
	dlg:endbox()

	-- ------------------------------------------------------------------
	-- 3) Gerar
	-- ------------------------------------------------------------------
	dlg:box({ orient = "vertical", label = "3) Gerar" })

	-- icone de sprite no inicio da primeira linha de cada grupo
	-- icones dos assets do editor (requer rebuild); fallback: sprite de item
	local function groupIcon(assetPath, fallbackItemId)
		if Image.fromAsset then
			local ok, img = pcall(function()
				return Image.fromAsset(assetPath, 16, 224, 224, 224)
			end)
			if ok and img and img.valid then
				dlg:image({ image = img, valign = "center" })
				return
			end
		end
		dlg:image({ itemid = fallbackItemId, width = 20, height = 20, smooth = false, valign = "center" })
	end

	dlg:box({ orient = "vertical", label = "Estilo" })
	dlg:wrap({})
	groupIcon("svg/solid/wand-magic-sparkles.svg", 4526)
	dlg:combobox({
		id = "mode",
		label = "Modo",
		options = { "Formato novo (silhueta)", "WFC (textura fiel)" },
		option = "Formato novo (silhueta)",
		tooltip = "Formato novo: cria silhuetas ineditas no estilo das amostras. WFC: recombina pedacos fieis das amostras.",
	})
	dlg:combobox({
		id = "shapeKind",
		label = "Forma",
		options = { "Mancha (blob)", "Caminho (rio)" },
		option = "Mancha (blob)",
		tooltip = "Mancha: forma fechada organica. Caminho: faixa que atravessa a area, ideal para rios e trilhas.",
	})
	dlg:endwrap()
	dlg:endbox()

	dlg:box({ orient = "vertical", label = "Tamanho e contorno" })
	dlg:wrap({})
	groupIcon("svg/solid/ruler-combined.svg", 2554)
	dlg:number({
		id = "sizePct",
		label = "Tamanho (%)",
		value = 100,
		min = 30,
		max = 300,
		tooltip = "Tamanho do shape em relacao a media das areas aprendidas (100 = igual).",
	})
	dlg:number({
		id = "coreWidth",
		label = "Largura do centro (0 = auto)",
		value = 0,
		min = 0,
		max = 30,
		tooltip = "Largura minima, em tiles, do chao central. Ex: a largura de um rio. 0 usa o aprendido.",
	})
	dlg:number({
		id = "roughPct",
		label = "Recorte (%)",
		value = 100,
		min = 0,
		max = 300,
		tooltip = "Quao recortado e o contorno. 100 = igual as amostras; maior = mais irregular.",
	})
	dlg:endwrap()
	dlg:endbox()

	dlg:box({ orient = "vertical", label = "Quantidade e distribuicao" })
	dlg:wrap({})
	groupIcon("svg/solid/shuffle.svg", 2148)
	dlg:number({
		id = "shapes",
		label = "Quantidade (0 = preencher)",
		value = 1,
		min = 0,
		max = 50,
		tooltip = "Quantos shapes gerar de uma vez. 0 = vai colocando ate a area encher.",
	})
	dlg:number({
		id = "spacing",
		label = "Espacamento (tiles)",
		value = 3,
		min = 0,
		max = 30,
		tooltip = "Folga minima, em tiles, entre um shape e outro.",
	})
	dlg:endwrap()
	dlg:endbox()

	dlg:box({ orient = "vertical", label = "Conteudo dos tiles" })
	dlg:wrap({})
	groupIcon("svg/solid/tree.svg", 2785)
	dlg:check({
		id = "borderize",
		text = "Auto-border",
		selected = true,
		tooltip = "Gera as bordas automaticas ao redor dos grounds pintados.",
	})
	dlg:check({
		id = "doodads",
		text = "Doodads",
		selected = true,
		tooltip = "Espalha a vegetacao/arvores aprendidas das amostras.",
	})
	dlg:check({
		id = "wipe",
		text = "Limpar itens",
		selected = true,
		tooltip = "Remove itens que ja existirem nos tiles antes de pintar.",
	})
	dlg:number({
		id = "density",
		label = "Doodads (%)",
		value = 100,
		min = 0,
		max = 300,
		tooltip = "Frequencia dos doodads em relacao ao aprendido (100 = igual as amostras).",
	})
	dlg:endwrap()
	dlg:endbox()

	dlg:box({ orient = "vertical", label = "Avancado" })
	dlg:wrap({})
	groupIcon("svg/solid/sliders.svg", 2553)
	dlg:number({
		id = "seed",
		label = "Seed (0 = aleatoria)",
		value = 0,
		min = 0,
		max = 999999999,
		tooltip = "Mesma seed = mesmo resultado. 0 sorteia uma nova a cada geracao.",
	})
	dlg:number({
		id = "genSize",
		label = "Area do Paste (lado)",
		value = 36,
		min = 8,
		max = 140,
		tooltip = "Tamanho do quadrado usado pelo 'Gerar p/ Paste' quando nao ha selecao (centrado na camera).",
	})
	dlg:endwrap()
	dlg:wrap({})
	dlg:combobox({
		id = "patternSize",
		label = "Padrao WFC",
		options = { "2x2", "3x3" },
		option = "3x3",
		tooltip = "So no modo WFC: tamanho dos padroes aprendidos. 3x3 gera estruturas mais coerentes.",
	})
	dlg:check({
		id = "symmetry",
		text = "Rotacoes/reflexoes",
		selected = true,
		tooltip = "So no modo WFC: tambem aprende as amostras rotacionadas/espelhadas.",
	})
	dlg:check({
		id = "closeBorder",
		text = "Margem vazia",
		selected = true,
		tooltip = "So no modo WFC: forca 1 tile vazio ao redor da area gerada.",
	})
	dlg:endwrap()
	dlg:endbox()

	dlg:wrap({})
	-- ultima geracao (para o Reroll) e contador para variar a seed
	local lastGen = nil
	local seedCounter = 0

	local function readOpts()
		return {
			mode = (dlg.data.mode == "WFC (textura fiel)") and "wfc" or "shape",
			shapeKind = (dlg.data.shapeKind == "Caminho (rio)") and "path" or "blob",
			coreWidth = math.floor(dlg.data.coreWidth or 0),
			spacing = math.floor(dlg.data.spacing or 0),
			sizePct = (dlg.data.sizePct or 100) / 100,
			roughPct = (dlg.data.roughPct or 100) / 100,
			patternSize = (dlg.data.patternSize == "3x3") and 3 or 2,
			symmetry = dlg.data.symmetry,
			closeBorder = dlg.data.closeBorder,
			borderize = dlg.data.borderize,
			doodads = dlg.data.doodads,
			wipe = dlg.data.wipe,
			density = (dlg.data.density or 100) / 100,
			seed = dlg.data.seed,
			maxShapes = math.floor(dlg.data.shapes or 1),
			genSize = math.floor(dlg.data.genSize or 36),
		}
	end

	dlg:button({
		text = "Gerar na selecao",
		tooltip = "Gera dentro da area selecionada no mapa.",
		onclick = guarded(function()
			local profile = currentProfile()
			if not profile then
				app.alert("Crie/selecione um perfil primeiro.")
				return
			end
			local paintedCount, extra, area = generate(profile, readOpts(), setStatus)
			if not paintedCount then
				setStatus(extra)
				return
			end
			lastGen = {
				kind = "sel",
				area = area,
				hist = app.editor and app.editor.historyIndex or nil,
			}
			local msg = "Pronto! " .. paintedCount .. " tiles pintados"
			if extra then
				msg = msg .. " em " .. extra .. " shape(s)"
			end
			setStatus(msg .. " (Ctrl+Z desfaz; Reroll regera).")
		end),
	})
	dlg:button({
		text = "Gerar p/ Paste",
		tooltip = "Gera, copia para o clipboard e desfaz - depois e so colar com Ctrl+V onde quiser.",
		onclick = guarded(function()
			local profile = currentProfile()
			if not profile then
				app.alert("Crie/selecione um perfil primeiro.")
				return
			end
			local paintedCount, extra, area = generateToClipboard(profile, readOpts(), setStatus)
			if not paintedCount then
				setStatus(extra)
				return
			end
			lastGen = { kind = "paste", area = area }
			local msg = "Shape no clipboard (" .. paintedCount .. " tiles"
			if extra then
				msg = msg .. ", " .. extra .. " shape(s)"
			end
			setStatus(msg .. ") - use Ctrl+V para colar onde quiser.")
		end),
	})
	dlg:button({
		text = "Reroll",
		tooltip = "Desfaz a ultima geracao e gera outra no mesmo lugar, com seed nova.",
		onclick = guarded(function()
			if not lastGen then
				setStatus("Nada para regerar - use um dos botoes Gerar primeiro.")
				return
			end
			local profile = currentProfile()
			if not profile then
				app.alert("Crie/selecione um perfil primeiro.")
				return
			end

			-- seed sempre nova no reroll (mesmo com seed fixa no campo)
			local opts = readOpts()
			seedCounter = seedCounter + 1
			if (opts.seed or 0) ~= 0 then
				opts.seed = opts.seed + seedCounter
			else
				opts.seed = os.time() + seedCounter * 7919
			end

			if lastGen.kind == "sel" then
				local editor = app.editor
				if not editor then
					setStatus("Editor indisponivel.")
					return
				end
				-- so deleta a geracao anterior se nada mudou no mapa desde ela
				if lastGen.hist and editor.historyIndex ~= lastGen.hist then
					setStatus("O mapa mudou desde a ultima geracao - desfaca com Ctrl+Z e use Gerar.")
					return
				end
				editor:undo()
				local count, extra = generate(profile, opts, setStatus, lastGen.area)
				if not count then
					lastGen = nil
					setStatus(extra)
					return
				end
				lastGen.hist = editor.historyIndex
				local msg = "Reroll! " .. count .. " tiles"
				if extra then
					msg = msg .. " em " .. extra .. " shape(s)"
				end
				setStatus(msg .. " (Ctrl+Z desfaz; Reroll regera).")
			else
				local count, extra = generateToClipboard(profile, opts, setStatus, lastGen.area)
				if not count then
					setStatus(extra)
					return
				end
				setStatus("Reroll no clipboard (" .. count .. " tiles) - Ctrl+V para colar.")
			end
		end),
	})
	dlg:endwrap()
	dlg:endbox()

	dlg:label({
		id = "status",
		text = "Fluxo: 1) crie/escolha um perfil  2) aprenda areas de exemplo  3) gere (selecao ou Paste).",
	})

	dlg:show({ wait = false })
	return dlg
end

if not app.hasMap() then
	app.alert("Abra um mapa antes de usar o Map Synthesizer.")
else
	buildDialog()
end
