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

-- ============================================================================
-- ONDE O ARQUIVO PODE SER GRAVADO
-- ============================================================================
-- O sandbox Lua do RME DESLIGA a biblioteca io (ver source/lua/lua_engine.cpp:
-- "Disable IO library completely - scripts must use app.storage"). A unica via
-- de escrita e app.storage(nome), e ela so aceita caminhos dentro da pasta
-- scripts/ ou data/ do editor -- qualquer coisa fora disso e barrada com
-- "[Lua Security] Blocked unsafe path".
--
-- Ou seja: NAO da para exportar direto para dentro do client. O fluxo e:
--   1. exporta para data/ do editor (aqui do lado)
--   2. copia para a area de transferencia (ligado por padrao)
--   3. voce cola em client/modules/client_entergame/scenes/<id>.lua
--
-- O caminho de destino no client fica so no texto do dialogo, como lembrete.
-- ============================================================================
local CLIENT_HINT =
	"client/modules/client_entergame/scenes/<id>.lua"

-- Grava em data/, e nao em scripts/, por dois motivos:
--
--   1. O editor varre scripts/ recursivamente no boot e registra TODO .lua como
--      um script no menu -- a cena exportada viraria uma entrada inutil ali
--      (nao chega a rodar sozinha: para isso precisaria de @AutoRun).
--   2. app.storage abre um ofstream direto no caminho e NAO cria diretorio
--      nenhum, entao uma subpasta que ainda nao existe faria a escrita falhar
--      com um "nao consegui escrever" sem explicacao. data/ ja existe.
--
-- Um nome relativo simples NAO cairia em data/: o app.storage testa as raizes
-- na ordem SCRIPT_DIR, scripts/, data/, e para na primeira que serve -- ou seja,
-- sempre em scripts/. Por isso o caminho de data/ e montado explicito.
local FILE_NAME = "charlist_scene_export.lua"

-- Resolvido na abertura do dialogo, e nao no topo do arquivo: o script e
-- carregado no boot do editor, e ler `app` fora da guarda la de baixo daria um
-- erro de indexacao de nil em vez da mensagem clara de "RME Lua API not found".
local function defaultFile()
	local dataDir = (app and type(app.getDataDirectory) == "function")
		and app.getDataDirectory() or nil
	if type(dataDir) == "string" and dataDir ~= "" then
		-- normaliza a barra final, que varia conforme a plataforma
		if dataDir:sub(-1) ~= "/" and dataDir:sub(-1) ~= "\\" then
			dataDir = dataDir .. "/"
		end
		return dataDir .. FILE_NAME
	end
	-- sem a API: cai em scripts/, que sempre e uma raiz valida
	return FILE_NAME
end

-- (o valor e calculado em createDialog, ja com o `app` garantido)

-- Nome da global que o client espera. O registro de bases (charlist_bases.lua)
-- carrega o arquivo com dofile e colhe esta global -- por isso e sempre a mesma
-- em toda cena, nao uma por base.
local DEFAULT_GLOBAL = "CharlistSceneData"

-- ============================================================================
-- MARCADORES DE POSICAO (action id)
-- ============================================================================
-- Em vez de contar tiles na mao para descobrir onde o personagem entra e onde
-- ele para, marque no RME: ponha a action id abaixo em QUALQUER item do tile
-- (o chao serve) e o exportador escreve a coordenada ja normalizada no arquivo.
--
--   64001 -> POSICAO INICIAL: de onde o personagem sai (o portal)
--   64002 -> POSICAO FINAL:   onde ele para e fica em idle
--
-- A action id nao faz nada no jogo -- e so uma etiqueta legivel no editor. A
-- faixa 64xxx foi escolhida por estar livre: o maior aid registrado no servidor
-- e 57002 (ver data/actions, data/movements e data/scripts).
--
-- So a FINAL e obrigatoria. Uma cena marcada apenas com 64002 significa "o
-- personagem ja comeca parado aqui", e o client pula a caminhada.
local MARK_START = 64001
local MARK_REST  = 64002

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
		local tx, ty = t.x - minx, t.y - miny

		-- Marcador de posicao: a action id e so uma etiqueta, entao vale em
		-- qualquer item do tile -- chao, decoracao, o que for mais comodo marcar
		-- no editor. O item continua sendo exportado normalmente.
		local function mark(item)
			if not item then return end
			local aid = item.actionId
			if not aid or aid == 0 then return end

			if aid == MARK_START then
				if out.start then out.dupStart = true end
				out.start = { x = tx, y = ty }
			elseif aid == MARK_REST then
				if out.rest then out.dupRest = true end
				out.rest = { x = tx, y = ty }
			end
		end

		-- ground primeiro: no client ele tem que ser o UIItem de baixo da pilha
		local function push(item)
			if not item then return end
			mark(item)

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

	-- Marcador num tile que nao entrou na exportacao (nenhum item com sprite)
	-- deixaria o personagem pisando no vazio. Vale avisar na hora, nao no jogo.
	local function rendered(p)
		if not p then return true end
		for _, t in ipairs(out.tiles) do
			if t.x == p.x and t.y == p.y then return true end
		end
		return false
	end
	out.startOrphan = not rendered(out.start)
	out.restOrphan  = not rendered(out.rest)

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

	-- Posicoes marcadas com action id no mapa (ver MARK_START / MARK_REST). Sao
	-- coordenadas JA normalizadas para o canto (0,0) da cena, do jeito que o
	-- charlist_bases.lua espera. Sem marcador, o campo simplesmente nao sai e a
	-- base define a posicao a mao.
	if scene.start then
		L[#L + 1] = string.format("  portal = { x = %d, y = %d },   -- action id %d",
			scene.start.x, scene.start.y, MARK_START)
	end
	if scene.rest then
		L[#L + 1] = string.format("  rest = { x = %d, y = %d },   -- action id %d",
			scene.rest.x, scene.rest.y, MARK_REST)
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
	local DEFAULT_FILE = defaultFile()

	local dlg = Dialog {
		title = "Export Scene to Client Lua",
		width = 520,
	}

	-- O Dialog empilha os widgets LADO A LADO ate alguem fechar a linha: sem os
	-- newrow() abaixo o dialogo inteiro vira uma unica faixa horizontal ilegivel.
	-- box(), endbox() e separator() ja fecham a linha sozinhos (todos chamam
	-- finishCurrentRow no C++), entao so os widgets soltos precisam do newrow.
	dlg:label { text = "Exporta a selecao atual como tabela de CLIENT IDs." }
	dlg:newrow()
	dlg:label { text = "Cada tile vira uma pilha de UIItem, com o ground na base." }

	dlg:separator()

	dlg:box { orient = "vertical", label = "Saida", padding = 8, margin = 4, expand = false }
		dlg:input { id = "global", label = "Variavel global:", text = DEFAULT_GLOBAL }
		dlg:newrow()
		dlg:label { text = "(vazio = gera 'return { ... }')" }
		dlg:newrow()
		dlg:check { id = "names", text = "Comentar cada linha com os nomes dos itens", selected = true }
		dlg:newrow()
		dlg:check { id = "clip", text = "Copiar para a area de transferencia", selected = true }
	dlg:endbox()

	dlg:box { orient = "vertical", label = "Arquivo", padding = 8, margin = 4, expand = false }
		-- input de TEXTO, e nao dlg:file: o seletor de arquivos deixaria escolher
		-- um caminho absoluto qualquer, que o sandbox do app.storage recusaria.
		dlg:input { id = "path", label = "Salvar em:", text = DEFAULT_FILE }
		dlg:newrow()
		dlg:label { text = "O sandbox do RME so permite gravar em scripts/ ou data/." }
		dlg:newrow()
		dlg:label { text = "Depois copie na mao para:" }
		dlg:newrow()
		dlg:label { text = "  " .. CLIENT_HINT }
	dlg:endbox()

	-- botoes lado a lado, na propria linha
	dlg:box { orient = "horizontal", padding = 4, expand = false }
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

			-- campo vazio cai no default, em vez de exportar para lugar nenhum
			local name = data.path
			if not name or name == "" then
				name = DEFAULT_FILE
			end

			-- app.storage devolve uma tabela VAZIA quando o caminho e barrado pelo
			-- sandbox (fora de scripts/ e data/) -- por isso a checagem do save.
			local store = app.storage(name)
			if not store or not store.save then
				app.alert(
					"Caminho recusado pelo sandbox do RME:\n" .. name ..
					"\n\nSo da para gravar dentro das pastas scripts/ ou data/ do editor." ..
					"\nUse um nome simples (ex.: " .. FILE_NAME .. ") e depois" ..
					"\ncopie o arquivo para o client.")
				return
			end

			if not store:save(text) then
				app.alert("Nao consegui escrever em:\n" .. tostring(store.path or name))
				return
			end

			local path = store.path or name

			local msg = string.format(
				"Exportado: %d tiles, %d itens (%d x %d).",
				#scene.tiles, scene.total, scene.width, scene.height)

			-- Posicoes marcadas por action id: dizer o que foi (ou nao foi) achado
			-- evita o "exportei e o personagem continua no lugar errado".
			msg = msg .. "\n\nPosicoes marcadas:"
			if scene.start then
				msg = msg .. string.format("\n  inicial (%d): tile %d,%d", MARK_START, scene.start.x, scene.start.y)
			else
				msg = msg .. string.format("\n  inicial (%d): nenhuma -- o personagem", MARK_START)
				msg = msg .. "\n    ja comeca parado na posicao final (sem caminhada)"
			end
			if scene.rest then
				msg = msg .. string.format("\n  final   (%d): tile %d,%d", MARK_REST, scene.rest.x, scene.rest.y)
			else
				msg = msg .. string.format("\n  final   (%d): NENHUMA -- marque um tile com essa", MARK_REST)
				msg = msg .. "\n    action id, senao a base tera de definir a posicao a mao"
			end

			if scene.dupStart or scene.dupRest then
				msg = msg .. "\n\nATENCAO: a mesma action id aparece em mais de um tile;" ..
					"\nvaleu a ULTIMA encontrada. Deixe so uma de cada."
			end
			if scene.startOrphan or scene.restOrphan then
				msg = msg .. "\n\nATENCAO: um marcador esta num tile que NAO foi exportado" ..
					"\n(sem item com sprite no .dat). O personagem ficaria no vazio."
			end

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

			-- O sandbox nao deixa gravar dentro do client, entao o ultimo passo e
			-- sempre manual -- deixar isso explicito evita a duvida de "exportei,
			-- e agora nao mudou nada no jogo".
			msg = msg .. "\n\nFalta levar para o client:"
			if data.clip then
				msg = msg .. "\n  ja esta na area de transferencia -- cole em"
			else
				msg = msg .. "\n  copie o arquivo acima para"
			end
			msg = msg .. "\n  " .. CLIENT_HINT
			msg = msg .. "\n\nDepois registre a base em charlist_bases.lua" ..
				"\n(file = 'scenes/<id>') e confira com" ..
				"\nCharlistScene.previewBase('<id>') no terminal do client."

			app.alert(msg)
			d:close()
		end,
	}

	dlg:button { text = "Cancelar", onclick = function(d) d:close() end }
	dlg:endbox()

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
