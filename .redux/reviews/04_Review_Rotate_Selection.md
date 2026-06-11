# Code Review: Rotate Selection (90° CW / CCW / 180°)

## Summary
- **Files Reviewed**: 22
- **Issues Found**: 4 (0 critical, 0 medium, 4 low)
- **Overall Assessment**: APPROVED

Plano: `.redux/plans/04_Rotate_Selection.md` | Summary: `.redux/summaries/03_Summary_Rotate_Selection.md`

---

## Verificação dos pontos de atenção (todos OK)

### 1. Refactor do CopyBuffer → RotationUtility (extração verbatim) — OK
Comparei lado a lado o código removido de `source/editor/copybuffer.cpp` (git diff) com `source/editor/operations/rotation_utility.cpp`:

| Lambda original | Destino | Verbatim? |
|---|---|---|
| `rotate_border_type_cw_once` (tabela switch de BorderType) | função em namespace anônimo (rotation_utility.cpp:20-40) | Sim — todas as 12 entradas idênticas |
| `rotate_wall_alignment_cw_once` (tabela de wall alignment) | função em namespace anônimo (:43-71) | Sim — V↔H, ENDs, Ts, diagonais, POLE/INTERSECTION/UNTOUCHABLE fixos |
| `build_door_key` (`alignment \| doorType<<8 \| open<<16`) | função em namespace anônimo (:73-77) | Sim — bit a bit idêntico |
| `rotate_border_type` / `rotate_wall_alignment` (normalização + loop) | métodos `rotateBorderType`/`rotateWallAlignment` (:91-105) | Equivalente — a normalização `% 4` + ajuste de negativos foi movida para o construtor (:81-89); como o chamador original sempre passava `turns` já normalizado, o resultado é idêntico |
| `get_border_for_item` (cache + `findAutoBorderByBorderItem` com alignmentHint) | `getBorderForItem` (:107-116), cache `border_for_item_id` com `reserve(128)` | Sim |
| `ensure_wall_catalogs` (iteração `g_item_definitions.allIds()`, flags IsWall/IsBrushDoor/IsOpen, `BorderAlignment`, sort dos vetores) | `ensureWallCatalogs` (:118-169) | Sim — incluindo os bounds `[0,17)` e a ordenação final |
| `rotate_position` (CW `(minX + h-1-ry, minY + rx)`, 180, CCW) | `rotatePosition` (:171-184) | Sim — fórmulas idênticas, Z preservado; min/width/height viraram parâmetros com os mesmos valores no call site |
| `rotate_item` (bordas → `border->getTileId(rotated)`; paredes/portas → catálogo com `idx % newIds.size()`; fallback `doRotate()` em loop) | `rotateItem` (:186-273) | Sim — fluxo de `return`s e fall-through para o fallback idênticos |
| `rotate_tile_items` (ground + items) | `rotateTileItems` (:275-283) | Sim |

- `CopyBuffer::rotate` manteve: early-returns, bounding box, deep copy + `PendingTile`, `clear(true)` + reinserção, passo final de `wallize`, atualização de `copyPos`. As únicas substituições são `RotationUtility rot(quarterTurns)` / `rot.isIdentity()` / `rot.rotatePosition(...)` / `rot.rotateTileItems(...)` — exatamente o que o plano pedia.
- Lifetime dos caches preservado: antes eram variáveis locais por chamada de `rotate()`; agora são membros de uma instância criada por chamada. Mesmo escopo efetivo → Structure Manager (structure_manager_window.cpp:1987 e tecla Z em 2760) intacto.
- Includes órfãos: grep em copybuffer.cpp confirma zero referências remanescentes a `g_brushes`, `AutoBorder`, `WallBrush`, `g_item_definitions`, `ItemFlag`, `ItemAttributeKey`, `BorderType`, `DoorType` — remoção dos 4 includes correta. `rotation_utility.cpp` inclui tudo que usa (`app/main.h`, item.h, tile.h, brush.h, auto_border.h, wall_brush.h, item_definition_store.h, `<algorithm>`); o header inclui `map/position.h`, `brushes/brush_enums.h`, `<cstdint>`, `<unordered_map>`, `<vector>` e usa forward declarations corretas.

### 2. Ownership em `SelectionOperations::rotateSelection` — OK
(`source/editor/operations/selection_operations.cpp:337-642`)
- Padrão idêntico ao `moveSelection`: `tmp_storage_tile.release()` para dentro do `TileSet`; na fase 3, cada ponteiro cru é consumido exatamente uma vez — `delete tile` no caminho `!new_pos.isValid()`, `delete tile` após `TileOperations::merge`, ou adoção via `new_dest_tile.reset(tile)` no caminho replace. Sem double-free e sem leak (após a validação tudo-ou-nada não existe nenhum `return` antes do consumo da `tmp_storage`).
- Todos os Changes/Actions/Tiles usam `std::make_unique<Change>(std::move(...))`, `createBatch`/`createAction` + `addAndCommitAction(std::move(action))` + `editor.addBatch(std::move(batchAction))`. Sem use-after-move.
- Fase 2 usa `tmp_storage` ANTES da fase 3 deletar/adotar os ponteiros — ordem correta.

### 3. Ordem das fases e iteração de `editor.selection` — OK
- Sequência preservada: remover origem → borderizar vizinhos da origem (threshold) → inserir rotacionado (com `rot.rotateTileItems(tile)` antes do merge/replace, paridade BT editor.cpp:1742) → borderizar vizinhos do destino (threshold) → re-borderizar os próprios tiles (sem threshold, `create_borders && doborders`, paridade BT 1813-1836).
- Fases 4 e 5 iteram `editor.selection` após os commits (a seleção já aponta para os tiles de destino) — não foi "otimizado". As listas de vizinhos das fases 2 e 4 são cópias exatas do `moveSelection`, incluindo o duplicado proposital `(+1,+1)` da fase 4 (removido por sort/unique) — os 8 vizinhos seguem todos cobertos.
- Hoist da condição em `borderize_neighbors`/`create_borders` é equivalente à avaliação lazy do `moveSelection` (a contagem da seleção é estável entre os commits).

### 4. Guardas — OK
- Seleção vazia → status "No items selected. Can't rotate." + return.
- Identidade (`rot.isIdentity()`) → return silencioso.
- Multi-andar (`min_pos.z != max_pos.z`) → "Cannot rotate selection across multiple floors." + return.
- Out-of-bounds tudo-ou-nada ANTES de `createBatch` → "Rotation would move selection out of bounds." (ver issue LOW-1 sobre a semântica de `isValid`).

### 5. Enum `ACTION_ROTATE_SELECTION` — OK
Adicionado ao FINAL de `ActionIdentifier` (action.h:116, após `ACTION_LUA_SCRIPT`); nome "Rotate Selection" no switch de `action_queue.cpp:75`.

### 6. Cadeia do menubar — OK
- enum `ROTATE_SELECTION_CW/CCW/180` (main_menubar.h:79-81) → `MAKE_ACTION` ×3 (menubar_action_manager.cpp:77-79; macro usa `#id`, casando com os `action=` do XML) → `EnableItem` (menubar_action_manager.cpp:293-297) → handlers delegados (main_menubar.cpp:521-531) → handlers reais (map_actions_handler.cpp:201-246) → `data/menubar.xml:90-95`.
- `EnableItem` CW = `canRotateSelection || structurePasteActive`; CCW/180 = `canRotateSelection`. O `editor->selection.size()` em :293 é protegido por short-circuit (`has_map` = `editor != nullptr` em :244).
- `OnRotateSelectionCW` chama `StructureManagerDialog::RotatePaste()` primeiro (paridade BT); CCW→`rotateSelection(3)`, 180→`rotateSelection(2)`; todos com guarda `IsEditorOpen` + `size() >= 2` + `RefreshView()`. `CanRotatePaste()` é barato e sem efeitos colaterais (verificado em structure_manager_window.cpp:2716-2725) — seguro no `UpdateState`.
- Hotkeys Ctrl+Alt+Right/Left/Down: parser suporta RIGHT/LEFT/DOWN (hotkey_utils.cpp:113-116); único outro `Ctrl+Alt+*` no XML é `Ctrl+Alt+S` — sem conflito. Persistência de hotkey é por NOME da ação, não por ID do enum → inserir os IDs no meio de `MenuBar::ActionID` é seguro.

### 7. Popup do canvas — OK
- Fix de leak: `Delete(m_item)` → `Destroy(m_item)` no loop de limpeza (map_popup_menu.cpp:46-50), comentário atualizado — `wxMenu::Destroy` também deleta submenus anexados.
- Submenu "Rotate selection" só no ramo `else` de `if (editor.selection.size() == 1)` (:260-269), com ícone `ICON_ROTATE` (IMAGE_MANAGER já usado no arquivo).
- 3 IDs novos em gui_ids.h logo após `MAP_POPUP_MENU_ROTATE` (compile-time, nada persistido); 3 binds em map_menu_handler.cpp:45-47; handlers → `PopupActionHandler::RotateSelection(editor, 1/3/2)` com guarda `size() < 2`.

### 8. CMakeLists — OK
`rotation_utility.h` (linha 69, seção de headers de operations) e `rotation_utility.cpp` (linha 505, seção de sources) registrados nos lugares certos.

### 9. Includes — OK
Ver item 1. `map_actions_handler.cpp` e `menubar_action_manager.cpp` incluem `ui/dialogs/structure_manager_window.h`; `selection_operations.cpp` inclui `rotation_utility.h`.

---

## Critical Issues

Nenhum.

## Medium Issues

Nenhum.

## Low Issues

### [LOW] Validação "out of bounds" usa limites absolutos, não as dimensões reais do mapa
- **File**: source/editor/operations/selection_operations.cpp:361-366
- **Type**: Quality / Observação de paridade
- **Description**: `Position::isValid()` (position.h:153-155) valida contra `MAP_MAX_WIDTH/HEIGHT` (65000) e `MAP_MAX_LAYER`, não contra `map.getWidth()/getHeight()`. Rotacionar uma seleção retangular encostada na borda leste/sul de um mapa pequeno pode criar tiles além das dimensões nominais do mapa sem disparar a mensagem.
- **Suggestion**: É paridade exata com o BT e consistente com `moveSelection` (que valida só o Z) — aceitável. Se quiser endurecer no futuro, comparar `new_pos` com `editor.map.getWidth()/getHeight()`.

### [LOW] Ctrl+Alt+setas deixam de chegar ao canvas
- **File**: data/menubar.xml:92-94
- **Type**: UX / Observação
- **Description**: A accelerator table do frame (main_menubar.cpp:898-924) intercepta Ctrl+Alt+Right/Left/Down antes do `KeyboardHandler` do canvas; antes, esses combos caíam na navegação por setas (Alt era ignorado, comportando-se como Ctrl+seta). Trade-off intencional e de baixo impacto.
- **Suggestion**: Nada a fazer; hotkeys são reconfiguráveis no diálogo de Hotkey Configuration.

### [LOW] Itens do submenu do popup não são desabilitados para seleção multi-andar
- **File**: source/ui/map_popup_menu.cpp:260-269
- **Type**: UX / Quality
- **Description**: Com 2+ tiles em andares diferentes, as 3 opções aparecem habilitadas; o clique só mostra a mensagem na status bar. Comportamento seguro (a operação aborta), mas o usuário não vê a restrição no menu.
- **Suggestion**: Opcional — `rotate_menu->Enable(id, min.z == max.z)` usando `selection.minPosition()/maxPosition()`.

### [LOW] Fases 2 e 4 duplicam ~170 linhas do moveSelection
- **File**: source/editor/operations/selection_operations.cpp:419-489, 530-616
- **Type**: Quality / Manutenibilidade
- **Description**: Os blocos de borderize de vizinhos são cópias verbatim do `moveSelection` (exigência do plano para reduzir risco). Uma futura mudança no algoritmo de borderize-on-move precisará ser aplicada em dois lugares.
- **Suggestion**: Quando conveniente, extrair um helper `borderizeNeighbors(editor, batch, tiles, doborders)` e usá-lo em ambas as operações (fora do escopo desta feature, conforme o plano).

## Positive Notes
- Extração do `CopyBuffer::rotate` é genuinamente verbatim — tabelas, chave de porta, `idx % newIds.size()`, fallback `doRotate()` e fórmulas de `rotatePosition` conferidas entrada por entrada contra o git diff. Risco de regressão no Structure Manager: mínimo.
- Ownership impecável dentro do padrão do arquivo: cada `Tile*` cru da `tmp_storage` tem exatamente um destino (delete/merge/adoção); todos os Changes/Actions via `unique_ptr` + `std::move`.
- Guardas e validação tudo-ou-nada ANTES de criar o batch — nenhum batch vazio ou parcial vai para a fila de undo em caso de erro.
- Fix de leak real no popup (`Delete`→`Destroy`) aplicado junto, como planejado.
- `rotateSelection` tem guard de seleção vazia com `return` — melhor que os vizinhos `borderizeSelection`/`randomizeSelection` (que têm um bug pré-existente: `SetStatusText` sem `return`, fora do escopo deste review).

## Recommendations
1. O working tree contém mudanças não relacionadas nos MESMOS arquivos desta feature (`CARPET_LIKE_GROUND_BORDERS` e `GHOST_GROUNDS` em main_menubar.h/.cpp + menubar_action_manager.cpp + menubar.xml; `TOOLBAR_SIZES_LINE_SNAP/SNAP_ANGLE` em gui_ids.h; `IMPORT_CREATURES_SPAWN`/`MERGE_MAPS_MINIMAP` em menubar.xml). Ao commitar, ou aceite que viajam juntas ou faça stage por hunk (`git add -p`).
2. Teste manual prioritário (valida o refactor): Structure Manager → colar estrutura → tecla Z / choice de rotação — paredes, portas e bordas devem girar exatamente como antes do refactor.
3. Teste de undo: 1 único Ctrl+Z deve desfazer a rotação inteira, com "Rotate Selection" no histórico.
