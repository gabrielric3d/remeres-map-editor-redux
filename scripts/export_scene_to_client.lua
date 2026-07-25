-- @Title: Export Scene to Client Lua
-- @Author: BlackTalon
-- @Description: Exporta a area selecionada como tabela Lua de CLIENT IDs, pronta para ser desenhada no client com widgets UIItem (ex.: a base/plataforma da character list).
-- @Version: 1.0
-- @Shortcut: Ctrl+Shift+E

-- ============================================================================
-- POR QUE CLIENT ID E NAO SERVER ID
-- ============================================================================
-- No client, UIItem:setItemId(id) cai em Item::setId(), que valida o id contra
-- o .dat (isValidDatId) e grava m_clientId = id. Ou seja: o client espera o
-- CLIENT ID. O server id do items.otb nao serve ali -- e o client nem carrega
-- items.otb. Por isso exportamos item.clientId, nunca item.id.
-- ============================================================================

-- Onde salvar por padrao. Se o modulo do client estiver no lugar de sempre,
-- exporta direto para la (o client so precisa reiniciar). Senao cai ao lado do
-- proprio script: SCRIPT_DIR e setado pelo engine para a pasta do arquivo,
-- inclusive em scripts de arquivo unico. Um nome relativo puro iria parar no
-- working directory do processo do editor -- que ninguem sabe qual e.
local CLIENT_TARGET =
	"b:/Github/blacktalon-workspace/client/modules/client_entergame/charlist_scene.lua"

local function exists(path)
	local f = io.open(path, "r")
	if f then f:close() return true end
	return false
end

local OUT_DIR = (type(SCRIPT_DIR) == "string" and SCRIPT_DIR ~= "") and SCRIPT_DIR or "."
local DEFAULT_FILE = exists(CLIENT_TARGET) and CLIENT_TARGET
	or (OUT_DIR .. "/charlist_scene.lua")

-- Nome da global que o client espera (o .otmod carrega este arquivo como script,
-- entao ele precisa definir uma global -- nao um 'return').
local DEFAULT_GLOBAL = "CharlistSceneData"

-- ---------------------------------------------------------------------------
-- Coleta
-- ---------------------------------------------------------------------------

-- Le a selecao atual e devolve os tiles normalizados para o canto (0,0),
-- junto com as estatisticas que o resumo final mostra.
local function collect()
	local sel = app.selection
	if not sel or not sel.tiles or sel.size == 0 then
		return nil, "Nada selecionado. Selecione a area no mapa antes de exportar."
	end

	local tiles = sel.tiles
	local minx, miny, minz
	local maxx, maxy, maxz

	for _, t in ipairs(tiles) do
		if not minx or t.x < minx then minx = t.x end
		if not miny or t.y < miny then miny = t.y end
		if not minz or t.z < minz then minz = t.z end
		if not maxx or t.x > maxx then maxx = t.x end
		if not maxy or t.y > maxy then maxy = t.y end
		if not maxz or t.z > maxz then maxz = t.z end
	end

	local out = {
		origin  = { x = minx, y = miny, z = minz },
		width   = (maxx - minx) + 1,
		height  = (maxy - miny) + 1,
		floors  = (maxz - minz) + 1,
		tiles   = {},
		total   = 0,
		skipped = 0,
	}

	for _, t in ipairs(tiles) do
		local ids, names = {}, {}

		-- ground primeiro: no client ele tem que ser o UIItem de baixo da pilha
		local function push(item)
			if not item then return end
			local cid = item.clientId
			if cid and cid > 0 then
				ids[#ids + 1]   = cid
				names[#names + 1] = item.name or "?"
				out.total = out.total + 1
			else
				-- item sem sprite no .dat (so existe no otb) -- o client nao desenha
				out.skipped = out.skipped + 1
			end
		end

		push(t.ground)
		for _, it in ipairs(t.items or {}) do
			push(it)
		end

		if #ids > 0 then
			out.tiles[#out.tiles + 1] = {
				x     = t.x - minx,
				y     = t.y - miny,
				z     = t.z - minz,
				ids   = ids,
				names = names,
			}
		end
	end

	if #out.tiles == 0 then
		return nil, "A selecao nao tem nenhum item com sprite no .dat."
	end

	-- ordem estavel: andar, depois linha, depois coluna
	table.sort(out.tiles, function(a, b)
		if a.z ~= b.z then return a.z < b.z end
		if a.y ~= b.y then return a.y < b.y end
		return a.x < b.x
	end)

	return out
end

-- ---------------------------------------------------------------------------
-- Geracao do texto
-- ---------------------------------------------------------------------------

local function build(scene, globalName, withNames)
	local multiFloor = scene.floors > 1
	local L = {}

	L[#L + 1] = "-- Cena exportada do RME Redux (Export Scene to Client Lua)"
	L[#L + 1] = string.format("-- Origem no mapa: %d, %d, %d", scene.origin.x, scene.origin.y, scene.origin.z)
	L[#L + 1] = string.format("-- %d x %d tiles%s -- %d itens",
		scene.width, scene.height,
		multiFloor and string.format(" x %d andares", scene.floors) or "",
		scene.total)
	L[#L + 1] = "-- Os ids sao CLIENT IDs (.dat): use direto em UIItem:setItemId()."
	L[#L + 1] = ""

	local head = (globalName ~= "" and (globalName .. " = {")) or "return {"
	L[#L + 1] = head
	-- A origem NAO e decorativa: o client usa pos.x % numPatternX para escolher
	-- a variacao do ground (Item::calculatePatterns). Sem ela todo tile cai na
	-- variacao 0 e o chao sai diferente do desenhado aqui.
	L[#L + 1] = string.format("  origin = { x = %d, y = %d, z = %d },",
		scene.origin.x, scene.origin.y, scene.origin.z)
	L[#L + 1] = string.format("  width = %d,", scene.width)
	L[#L + 1] = string.format("  height = %d,", scene.height)
	if multiFloor then
		L[#L + 1] = string.format("  floors = %d,", scene.floors)
	end
	L[#L + 1] = "  tiles = {"

	for _, t in ipairs(scene.tiles) do
		local coord = multiFloor
			and string.format("x = %d, y = %d, z = %d", t.x, t.y, t.z)
			or  string.format("x = %d, y = %d", t.x, t.y)

		local line = string.format("    { %s, ids = { %s } },", coord, table.concat(t.ids, ", "))

		if withNames then
			line = line .. "  -- " .. table.concat(t.names, " | ")
		end
		L[#L + 1] = line
	end

	L[#L + 1] = "  },"
	L[#L + 1] = "}"
	L[#L + 1] = ""

	return table.concat(L, "\n")
end

-- ---------------------------------------------------------------------------
-- Dialog
-- ---------------------------------------------------------------------------

local function createDialog()
	local dlg = Dialog {
		title = "Export Scene to Client Lua",
		width = 460,
	}

	dlg:label { text = "Exporta a selecao atual como tabela de CLIENT IDs para o client." }
	dlg:label { text = "Cada tile vira uma pilha de UIItem, com o ground na base." }

	dlg:input {
		id = "global",
		label = "Variavel global",
		text = DEFAULT_GLOBAL,
	}
	dlg:label { text = "(vazio = gera 'return { ... }')" }

	dlg:check { id = "names", text = "Comentar cada linha com os nomes dos itens", selected = true }
	dlg:check { id = "clip",  text = "Copiar tambem para a area de transferencia", selected = true }

	dlg:file { id = "path", label = "Salvar em", filename = DEFAULT_FILE, save = true }
	dlg:label { text = "Padrao: " .. DEFAULT_FILE }

	dlg:button {
		text = "Exportar",
		onclick = function(d)
			local scene, err = collect()
			if not scene then
				app.alert(err)
				return
			end

			local data = d.data
			local text = build(scene, tostring(data.global or ""), data.names and true or false)

			if data.clip then
				app.setClipboard(text)
			end

			-- file picker vazio cai no default, em vez de exportar para lugar nenhum
			local path = data.path
			if not path or path == "" then
				path = DEFAULT_FILE
			end

			local f, ferr = io.open(path, "w")
			if not f then
				app.alert("Nao consegui escrever em:\n" .. path .. "\n\n" .. tostring(ferr))
				return
			end
			f:write(text)
			f:close()

			local msg = string.format(
				"Exportado: %d tiles, %d itens (%d x %d).",
				#scene.tiles, scene.total, scene.width, scene.height)

			if scene.floors > 1 then
				msg = msg .. string.format(
					"\n\nATENCAO: a selecao tem %d andares. Cada tile ganhou um campo 'z'," ..
					"\ne o client vai precisar decidir como empilhar isso.", scene.floors)
			end
			if scene.skipped > 0 then
				msg = msg .. string.format(
					"\n\n%d item(ns) foram ignorados por nao terem sprite no .dat" ..
					"\n(existem so no items.otb, entao o client nao consegue desenhar).", scene.skipped)
			end
			msg = msg .. "\n\nArquivo: " .. path
			if data.clip then
				msg = msg .. "\nCopiado para a area de transferencia."
			end

			app.alert(msg)
			d:close()
		end,
	}

	dlg:button { text = "Cancelar", onclick = function(d) d:close() end }

	dlg:show { wait = false }
end

-- ---------------------------------------------------------------------------
-- Entry
-- ---------------------------------------------------------------------------

if not app then
	print("Error: RME Lua API not found.")
	return
end

createDialog()
