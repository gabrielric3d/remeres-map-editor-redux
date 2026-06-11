# Plan: Rotate Selection (rotacionar seleção 90° CW/CCW/180°)

## Overview
Portar a feature "Rotate Selection" do BT_MAPEDITORv3 para o rme_redux: rotacionar in-place a seleção de tiles (2+ tiles, um andar só) em 90° CW, 90° CCW ou 180°, com remapeamento inteligente de bordas, paredes e portas, tudo num único undo (`ACTION_ROTATE_SELECTION`). A lógica de rotação de itens que já existe em `CopyBuffer::rotate` será EXTRAÍDA para um helper compartilhado (`RotationUtility`) — sem duplicação como no BT.

## User Request
Portar `Editor::rotateSelection(int quarterTurns)` do BT_MAPEDITORv3 (referência: `B:\Github\BT_MAPEDITORv3\source\editor.cpp:1337-1842`) com: menu Edit → submenu "Rotate Selection" (Ctrl+Alt+Right/Left/Down), popup do canvas com submenu para seleção 2+, restrições de andar único e out-of-bounds tudo-ou-nada, integração com Structure Manager (CW gira o paste se ativo), e refactor do `CopyBuffer::rotate` para usar o helper compartilhado.

## Analysis
- **New Files**: Sim
  - `source/editor/operations/rotation_utility.h`
  - `source/editor/operations/rotation_utility.cpp`
- **Modified Files**: Sim
  - `source/editor/copybuffer.cpp` (refactor: usar RotationUtility)
  - `source/editor/action.h` (novo `ACTION_ROTATE_SELECTION`)
  - `source/editor/action_queue.cpp` (nome "Rotate Selection")
  - `source/editor/operations/selection_operations.h` / `.cpp` (nova operação `rotateSelection`)
  - `source/editor/editor.h` / `editor.cpp` (wrapper `Editor::rotateSelection`)
  - `source/ui/main_menubar.h` (3 ActionIDs + 3 handlers)
  - `source/ui/main_menubar.cpp` (3 handlers delegados)
  - `source/ui/menubar/menubar_action_manager.cpp` (MAKE_ACTION ×3 + EnableItem ×3 + include)
  - `source/ui/menubar/map_actions_handler.h` / `.cpp` (3 handlers reais)
  - `source/ui/gui_ids.h` (3 IDs de popup)
  - `source/ui/map_popup_menu.cpp` (submenu "Rotate selection" para seleção 2+; fix de leak Delete→Destroy)
  - `source/rendering/ui/popup_action_handler.h` / `.cpp` (`RotateSelection`)
  - `source/rendering/ui/map_menu_handler.h` / `.cpp` (binds + 3 handlers)
- **CMakeLists Update**: Sim (`source/CMakeLists.txt`: registrar rotation_utility.h/.cpp)
- **Menu/Toolbar Wiring**: Sim (menubar.xml + popup do canvas)
- **Action System**: Sim — novo `ACTION_ROTATE_SELECTION`, BatchAction multi-fase (até 5 Actions), template de `SelectionOperations::moveSelection`
- **Data Files (XML/TOML)**: Sim — `data/menubar.xml` (submenu no menu Edit)

## Convenções verificadas no código atual (referências)
- `CopyBuffer::rotate(int quarterTurns)` em `source/editor/copybuffer.cpp:78-447`: contém TODA a lógica de remapeamento como lambdas — `rotate_border_type_cw_once` (123), `rotate_border_type` (145), cache `border_for_item_id` + `get_border_for_item` (158-170), `rotate_wall_alignment_cw_once` (173), `rotate_wall_alignment` (203), `WallBrushCatalog`/`build_door_key`/`ensure_wall_catalogs` (216-281), `rotate_position` (283), `rotate_item` (298-385, com fallback `doRotate()` em loop), `rotate_tile_items` (387-395). Único consumidor externo: Structure Manager (`structure_manager_window.cpp:1987` e tecla Z em 2760).
- API redux nas lambdas: `border->getTileId(rotated)` (NÃO `border->tiles[rotated]` do BT); catálogo de paredes itera `g_item_definitions.allIds()` com `ItemFlag::IsWall`/`IsBrushDoor`/`IsOpen` e `ItemAttributeKey::BorderAlignment` (NÃO `g_items.getMinID()..getMaxID()` do BT). Conversão já feita — mover verbatim.
- `BorderType` e `DoorType`: `source/brushes/brush_enums.h:42` e `:102`.
- `g_brushes.findAutoBorderByBorderItem(uint16_t itemId, BorderType alignmentHint = BORDER_NONE)` em `source/brushes/brush.h:94`.
- Template multi-fase: `SelectionOperations::moveSelection` em `source/editor/operations/selection_operations.cpp:87-334` (remover origem → borderizar vizinhos origem [com threshold] → inserir destino [merge se `Config::MERGE_MOVE` ou sem ground, senão replace] → borderizar vizinhos destino [com threshold] → `editor.addBatch` + `selection.updateSelectionCount()`). `TileSet = std::vector<Tile*>` (`source/map/tile.h:255`), storage usa ponteiros crus liberados com `delete` no merge — copiar o padrão exato.
- Referência BT completa: `B:\Github\BT_MAPEDITORv3\source\editor.cpp:1337-1842`. Diferenças da rotação vs move: (a) validação out-of-bounds tudo-ou-nada ANTES do batch (1637-1643); (b) `rotate_tile_items(tile)` no storage tile antes do merge/replace (1742); (c) fase 5 extra (1811-1836): re-borderizar os PRÓPRIOS tiles rotacionados com GroundBrush, condição `create_borders && borderize` SEM threshold.
- Action system: `ActionIdentifier` em `source/editor/action.h:100-116` (último: `ACTION_LUA_SCRIPT`); nomes em `source/editor/action_queue.cpp:60-76`.
- Wrappers do Editor: `source/editor/editor.h:80-86` (declarações), `source/editor/editor.cpp:105-135` (delegação a `SelectionOperations`).
- Cadeia de menu (modelo BORDERIZE_SELECTION): enum em `source/ui/main_menubar.h:76`; `MAKE_ACTION(BORDERIZE_SELECTION, wxITEM_NORMAL, OnBorderizeSelection)` em `source/ui/menubar/menubar_action_manager.cpp:73`; `EnableItem(BORDERIZE_SELECTION, has_map && has_selection)` em `menubar_action_manager.cpp:284` (flags `has_map`/`has_selection`/`editor` calculados em 228-244); declaração handler em `main_menubar.h:315`; delegação em `main_menubar.cpp:505-507`; implementação em `source/ui/menubar/map_actions_handler.cpp:156-163` (+ declaração em `map_actions_handler.h:18`); XML em `data/menubar.xml:74`.
- Menu Edit do menubar.xml: bloco Cut/Copy/Paste em `data/menubar.xml:87-89`, fim do menu Edit na linha 90. Submenus aninhados suportados (ex.: "Border Options" linha 70). Estilo redux NÃO usa `$` de acelerador.
- Hotkeys: parser em `source/editor/hotkey_utils.cpp` suporta "RIGHT"/"LEFT"/"DOWN" (111-116) e modificadores Ctrl/Alt. Conflito verificado: nenhum `Ctrl+Alt+<seta>` em uso (só `Ctrl+Alt+S` na linha 36). Hotkey aparece automaticamente no diálogo Hotkey Configuration via atributo `hotkey` do XML.
- Popup do canvas: `MapPopupMenu::Update()` em `source/ui/map_popup_menu.cpp:44-262` — bloco `if (anything_selected)` na linha 78 contém só o ramo `size() == 1` (linha 79); o loop de limpeza (46-50) usa `Delete()` com comentário avisando que submenu não é deletado → trocar para `Destroy()`. IDs em `source/ui/gui_ids.h:56-57` (`MAP_POPUP_MENU_SWITCH_DOOR`, `MAP_POPUP_MENU_ROTATE`). Binds em `source/rendering/ui/map_menu_handler.cpp:34-68`; padrão de ação em `source/rendering/ui/popup_action_handler.cpp:33-49` (`RotateItem`).
- Structure Manager: `StructureManagerDialog::RotatePaste()` e `CanRotatePaste()` JÁ EXISTEM como estáticos públicos em `source/ui/dialogs/structure_manager_window.h:46-47` (impl. em `structure_manager_window.cpp:2690-2725`). Integração viável: handler CW chama `RotatePaste()` primeiro, igual ao BT (`B:\Github\BT_MAPEDITORv3\source\main_menubar.cpp:2032-2049`).
- Seleção: `selection.minPosition()/maxPosition()` em `source/editor/selection.h:74-75`; `Position::isValid()` em `source/map/position.h:153-155`; status bar via `g_gui.SetStatusText(...)` (`selection_operations.cpp` já usa e já inclui `ui/gui.h`).
- CMakeLists: headers de operations em `source/CMakeLists.txt:66-69`, sources em `:501-504`.

## Tasks

### Phase 1: Helper compartilhado + Action system

#### Task 1.1: Criar `RotationUtility` (extração de CopyBuffer::rotate)
- **Description**: Extrair as lambdas de rotação de item de `copybuffer.cpp:123-395` para uma classe reutilizável com caches por operação. Movimentação de código SEM mudança de comportamento.
- **Files**:
  - `source/editor/operations/rotation_utility.h` (novo)
  - `source/editor/operations/rotation_utility.cpp` (novo)
- **Details**:
  - Header com guard `RME_ROTATION_UTILITY_H` seguindo estilo dos vizinhos. Incluir `map/position.h`, `brushes/brush_enums.h` (para `BorderType`/`DoorType`), `<unordered_map>`, `<vector>`, `<cstdint>`. Forward declarations: `Item`, `Tile`, `AutoBorder`, `WallBrush`.
  - API pública da classe `RotationUtility`:
    ```cpp
    class RotationUtility {
    public:
        // quarterTurns: 1 = 90 CW, 2 = 180, 3 = 90 CCW. Normaliza para 0..3 (negativos somam 4).
        explicit RotationUtility(int quarterTurns);
        int turns() const { return turns_; }
        bool isIdentity() const { return turns_ == 0; }
        // Rotaciona pos dentro do bounding box ancorado em minPos (width x height), Z inalterado.
        Position rotatePosition(const Position& pos, const Position& minPos, int width, int height) const;
        // Remapeia ID/orientação de um item (borda → grupo AutoBorder; parede/porta → catálogo WallBrush; fallback doRotate).
        void rotateItem(Item* item);
        // rotateItem no ground e em todos os items do tile.
        void rotateTileItems(Tile* tile);
    private:
        // ... métodos privados e caches (ver abaixo)
        int turns_;
        std::unordered_map<uint16_t, const AutoBorder*> border_for_item_id;
        struct WallBrushCatalog {
            std::vector<uint16_t> byAlignment[17];
            std::unordered_map<uint32_t, std::vector<uint16_t>> doorsByKey;
        };
        bool wall_catalog_built = false;
        std::unordered_map<const WallBrush*, WallBrushCatalog> wall_catalog_by_brush;
    };
    ```
  - No `.cpp`, mover verbatim (adaptando lambda→método/função estática de arquivo):
    - `rotate_border_type_cw_once` e `rotate_wall_alignment_cw_once` → funções `static` no .cpp (tabelas de switch idênticas a `copybuffer.cpp:123-143` e `:173-201`).
    - `rotate_border_type` / `rotate_wall_alignment` → métodos privados usando `turns_`.
    - `get_border_for_item` → método privado `getBorderForItem(uint16_t itemId, BorderType alignmentHint)` com o cache `border_for_item_id` (idêntico a `:161-170`).
    - `build_door_key` → função `static` no .cpp (idêntico a `:224-228`).
    - `ensure_wall_catalogs` → método privado `ensureWallCatalogs()` (idêntico a `:230-281`, incluindo iteração de `g_item_definitions.allIds()` e ordenação dos vetores).
    - `rotate_position` → `rotatePosition(...)` (fórmulas idênticas a `:283-296`: CW `(x,y)→(minX + h-1-ry, minY + rx)`; 180; CCW).
    - `rotate_item` → `rotateItem(...)` (idêntico a `:298-385`: 1ª camada bordas com `border->getTileId(rotated)`, 2ª camada paredes/portas com catálogo e `idx % newIds.size()`, fallback `item->doRotate()` em loop `turns_` vezes).
    - `rotate_tile_items` → `rotateTileItems(...)` (idêntico a `:387-395`).
  - Includes do .cpp (copiar do que `copybuffer.cpp` usa hoje): `app/main.h`, `editor/operations/rotation_utility.h`, `game/item.h`, `map/tile.h`, `brushes/brush.h`, `brushes/ground/auto_border.h`, `brushes/wall/wall_brush.h`, `item_definitions/core/item_definition_store.h`, `<algorithm>`.

#### Task 1.2: Refatorar `CopyBuffer::rotate` para usar o helper
- **Depends On**: Task 1.1
- **Description**: Substituir as lambdas de `copybuffer.cpp:78-447` pelo uso de `RotationUtility`, mantendo comportamento idêntico.
- **Files**: `source/editor/copybuffer.cpp`
- **Details**:
  - Manter: early-returns (`!tiles || size()==0`), cálculo do bounding box (`:91-120`), o loop de deep copy + recolocação (`:397-435`), o passo final de `wallize` nos tiles com parede (`:437-443`) e a atualização de `copyPos` (`:445-446`).
  - Substituir: normalização de turns por `RotationUtility rot(quarterTurns); if (rot.isIdentity()) return;`; chamadas `rotate_position(...)` → `rot.rotatePosition(oldPos, minPos, width, height)`; `rotate_tile_items(...)` → `rot.rotateTileItems(...)`.
  - Adicionar `#include "editor/operations/rotation_utility.h"`; remover includes que ficarem órfãos APENAS se não usados por mais nada no arquivo (conferir `auto_border.h`, `wall_brush.h`, `item_definition_store.h` — provavelmente podem sair; `brushes/brush.h` é usado? verificar antes de remover).

#### Task 1.3: Registrar arquivos novos no CMakeLists
- **Parallel**: Sim (junto com 1.2)
- **Description**: Registrar header e source do helper.
- **Files**: `source/CMakeLists.txt`
- **Details**:
  - Adicionar `${CMAKE_CURRENT_LIST_DIR}/editor/operations/rotation_utility.h` junto aos headers de operations (~linha 68, após `selection_operations.h`).
  - Adicionar `${CMAKE_CURRENT_LIST_DIR}/editor/operations/rotation_utility.cpp` junto aos sources (~linha 503, após `selection_operations.cpp`).

#### Task 1.4: Novo `ACTION_ROTATE_SELECTION`
- **Parallel**: Sim (independente de 1.1-1.3)
- **Description**: Registrar o identificador da ação e seu nome legível.
- **Files**:
  - `source/editor/action.h`
  - `source/editor/action_queue.cpp`
- **Details**:
  - Em `action.h:115`, adicionar `ACTION_ROTATE_SELECTION,` ao final do enum (após `ACTION_LUA_SCRIPT`) — não inserir no meio para não deslocar valores existentes.
  - Em `action_queue.cpp:74` (switch de nomes), adicionar `case ACTION_ROTATE_SELECTION: return "Rotate Selection";` antes do `default`.

### Phase 2: Operação rotateSelection

#### Task 2.1: `SelectionOperations::rotateSelection` + wrapper no Editor
- **Depends On**: Task 1.1, 1.4
- **Description**: Implementar a operação in-place seguindo o template multi-fase de `moveSelection` (selection_operations.cpp:87-334) com as 3 diferenças da rotação (validação prévia, rotateTileItems no destino, fase 5 de re-borderização interna). Referência BT: `editor.cpp:1637-1842`.
- **Files**:
  - `source/editor/operations/selection_operations.h`
  - `source/editor/operations/selection_operations.cpp`
  - `source/editor/editor.h`
  - `source/editor/editor.cpp`
- **Details**:
  - Header: adicionar `static void rotateSelection(Editor& editor, int quarterTurns);` após `moveSelection` (linha 13).
  - `editor.h`: adicionar declaração `// Rotates the selected area in 90-degree steps (1 = CW, 2 = 180, 3 = CCW)` + `void rotateSelection(int quarterTurns);` após `moveSelection` (linha 80). `editor.cpp`: wrapper delegando a `SelectionOperations::rotateSelection(*this, quarterTurns)` junto aos demais (~linha 131).
  - Implementação em `selection_operations.cpp` (incluir `editor/operations/rotation_utility.h`):
    1. **Guardas**: `if (editor.selection.empty()) { g_gui.SetStatusText("No items selected. Can't rotate."); return; }`. Construir `RotationUtility rot(quarterTurns); if (rot.isIdentity()) return;`.
    2. **Andar único**: `Position min_pos = editor.selection.minPosition(); Position max_pos = editor.selection.maxPosition(); if (min_pos.z != max_pos.z) { g_gui.SetStatusText("Cannot rotate selection across multiple floors."); return; }`. Calcular `width`/`height` do bounding box.
    3. **Validação tudo-ou-nada** (ANTES de criar o batch): para cada `Tile* tile : editor.selection`, se `!rot.rotatePosition(tile->getPosition(), min_pos, width, height).isValid()` → `g_gui.SetStatusText("Rotation would move selection out of bounds."); return;`.
    4. **Batch**: `auto batchAction = editor.actionQueue->createBatch(ACTION_ROTATE_SELECTION);`.
    5. **Action 1 — remover origem**: cópia EXATA do bloco de `moveSelection` linhas 92-135 (deep copy do source, `TileOperations::popSelectedItems`, mover spawn/creature selecionados, transferir `house_id` + MapFlags quando o ground vai junto, flag `doborders`, `tmp_storage.push_back(tmp_storage_tile.release())`, `addChange`, `addAndCommitAction`).
    6. **Action 2 — borderizar vizinhos da origem** (condição `g_settings.getInteger(Config::USE_AUTOMAGIC) && g_settings.getInteger(Config::BORDERIZE_DRAG) && editor.selection.size() < size_t(g_settings.getInteger(Config::BORDERIZE_DRAG_THRESHOLD))` — guardar essa condição num bool `borderize_neighbors` e `create_borders = USE_AUTOMAGIC && BORDERIZE_DRAG` para reuso): cópia exata das linhas 138-202 de `moveSelection` (9 vizinhos incluindo a própria pos, sort/unique, deepCopy + borderize [se doborders]/wallize/tableize/carpetize/selectGround, commit).
    7. **Action 3 — inserir tiles rotacionados**: como linhas 205-242 de `moveSelection`, MAS: `Position new_pos = rot.rotatePosition(old_pos, min_pos, width, height);` em vez de `old_pos - offset`; defensivamente `if (!new_pos.isValid()) { delete tile; continue; }`; e chamar `rot.rotateTileItems(tile);` ANTES do merge/replace (paridade com BT `editor.cpp:1742`). Merge vs replace idêntico (`Config::MERGE_MOVE || !tile->ground`).
    8. **Action 4 — borderizar vizinhos do destino** (mesma condição com threshold): cópia exata das linhas 245-329 de `moveSelection` (itera `editor.selection`, que após o commit da Action 3 já aponta para os tiles de destino).
    9. **Action 5 — re-borderizar os próprios tiles rotacionados** (EXCLUSIVO da rotação; condição `create_borders && doborders`, SEM threshold — paridade com BT `editor.cpp:1813-1836`): nova Action; para cada `Tile* tile : editor.selection`: `Tile* map_tile = editor.map.getTile(tile->getPosition()); if (!map_tile || !map_tile->ground || !map_tile->ground->getGroundBrush()) continue;` → `deepCopy`, `TileOperations::borderize/wallize/tableize/carpetize`, `selectGround` se o ground estiver selecionado, `addChange`; `addAndCommitAction`.
    10. **Final**: `editor.addBatch(std::move(batchAction)); editor.selection.updateSelectionCount();`.
  - Convenções obrigatórias: `std::unique_ptr` + `std::move` em todos os Changes/Actions/Tiles; `TileOperations::deepCopy(tile, editor.map)`; nunca `action->empty()` (usar `size()`).

### Phase 3: UI — menu Edit + popup do canvas

#### Task 3.1: Cadeia do menubar
- **Depends On**: Task 2.1
- **Description**: Expor a operação no menu Edit com hotkeys, seguindo o padrão BORDERIZE_SELECTION, incluindo a integração CW ↔ Structure Manager.
- **Files**:
  - `source/ui/main_menubar.h`
  - `source/ui/main_menubar.cpp`
  - `source/ui/menubar/menubar_action_manager.cpp`
  - `source/ui/menubar/map_actions_handler.h`
  - `source/ui/menubar/map_actions_handler.cpp`
  - `data/menubar.xml`
- **Details**:
  - `main_menubar.h`: no enum `MenuBar::ActionID`, adicionar `ROTATE_SELECTION_CW, ROTATE_SELECTION_CCW, ROTATE_SELECTION_180,` após `RANDOMIZE_MAP` (linha 79). Declarar `void OnRotateSelectionCW/CCW/180(wxCommandEvent&);` após `OnRandomizeMap` (linha 318).
  - `main_menubar.cpp`: handlers delegados após `OnRandomizeMap` (~linha 519), no padrão `mapActionsHandler->OnRotateSelectionCW(event);`.
  - `menubar_action_manager.cpp`:
    - `MAKE_ACTION(ROTATE_SELECTION_CW, wxITEM_NORMAL, OnRotateSelectionCW);` (+ CCW, 180) após a linha 76 (`RANDOMIZE_MAP`).
    - Em `UpdateState`, após linha 287: 
      ```cpp
      bool canRotateSelection = has_map && has_selection && editor->selection.size() >= 2;
      bool structurePasteActive = StructureManagerDialog::CanRotatePaste();
      mb->EnableItem(ROTATE_SELECTION_CW, canRotateSelection || structurePasteActive);
      mb->EnableItem(ROTATE_SELECTION_CCW, canRotateSelection);
      mb->EnableItem(ROTATE_SELECTION_180, canRotateSelection);
      ```
    - Adicionar `#include "ui/dialogs/structure_manager_window.h"` no topo.
  - `map_actions_handler.h`: declarar os 3 handlers após `OnRandomizeSelection` (linha 20).
  - `map_actions_handler.cpp` (paridade com BT `main_menubar.cpp:2032-2079`): 
    - `OnRotateSelectionCW`: `if (StructureManagerDialog::RotatePaste()) return;` → guarda `g_gui.IsEditorOpen()` → `Editor* editor = g_gui.GetCurrentEditor(); if (!editor || editor->selection.size() < 2) return;` → `editor->rotateSelection(1); g_gui.RefreshView();`.
    - `OnRotateSelectionCCW`: idem sem RotatePaste, `rotateSelection(3)`.
    - `OnRotateSelection180`: idem, `rotateSelection(2)`.
    - Adicionar `#include "ui/dialogs/structure_manager_window.h"` (e conferir que `editor/editor.h` já está incluído).
  - `data/menubar.xml`: após a linha 89 (`Paste`), dentro do menu Edit, adicionar:
    ```xml
    <separator/>
    <menu name="Rotate Selection">
        <item name="Rotate Clockwise" hotkey="Ctrl+Alt+Right" action="ROTATE_SELECTION_CW" help="Rotate the selection 90 degrees clockwise."/>
        <item name="Rotate Counterclockwise" hotkey="Ctrl+Alt+Left" action="ROTATE_SELECTION_CCW" help="Rotate the selection 90 degrees counterclockwise."/>
        <item name="Rotate 180" hotkey="Ctrl+Alt+Down" action="ROTATE_SELECTION_180" help="Rotate the selection 180 degrees."/>
    </menu>
    ```
    (estilo redux, sem `$`; hotkeys verificadas sem conflito e suportadas pelo parser de `hotkey_utils.cpp:113-116`; aparecem automaticamente no diálogo Hotkey Configuration).

#### Task 3.2: Popup do canvas
- **Depends On**: Task 2.1
- **Parallel**: Sim (com Task 3.1)
- **Description**: Submenu "Rotate selection" no popup quando a seleção tem 2+ tiles (paridade com BT `map_display.cpp:4152-4158`).
- **Files**:
  - `source/ui/gui_ids.h`
  - `source/ui/map_popup_menu.cpp`
  - `source/rendering/ui/popup_action_handler.h`
  - `source/rendering/ui/popup_action_handler.cpp`
  - `source/rendering/ui/map_menu_handler.h`
  - `source/rendering/ui/map_menu_handler.cpp`
- **Details**:
  - `gui_ids.h`: adicionar `MAP_POPUP_MENU_ROTATE_SELECTION_CW, MAP_POPUP_MENU_ROTATE_SELECTION_CCW, MAP_POPUP_MENU_ROTATE_SELECTION_180,` logo após `MAP_POPUP_MENU_ROTATE` (linha 57). (Enum compile-time, deslocar valores seguintes é seguro — nada é persistido.)
  - `map_popup_menu.cpp` em `Update()`:
    - Trocar o loop de limpeza (linhas 46-50) de `Delete(m_item)` para `Destroy(m_item)` (wxMenu::Destroy também deleta o submenu; evita leak a cada right-click) e atualizar/remover o comentário da linha 48.
    - No bloco `if (anything_selected)` da linha 78, adicionar ramo `else` ao `if (editor.selection.size() == 1)` (após a linha 260, fechando o ramo size==1):
      ```cpp
      } else {
          // selection.size() >= 2
          AppendSeparator();
          wxMenu* rotate_menu = newd wxMenu();
          rotate_menu->Append(MAP_POPUP_MENU_ROTATE_SELECTION_CW, "Rotate selection clockwise", "Rotate the selection 90 degrees clockwise");
          rotate_menu->Append(MAP_POPUP_MENU_ROTATE_SELECTION_CCW, "Rotate selection counterclockwise", "Rotate the selection 90 degrees counterclockwise");
          rotate_menu->Append(MAP_POPUP_MENU_ROTATE_SELECTION_180, "Rotate selection 180", "Rotate the selection 180 degrees");
          AppendSubMenu(rotate_menu, "Rotate selection");
      }
      ```
      Opcional: `SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_ROTATE, wxSize(16,16)))` no item do submenu, como o "Rotate item" da linha 139.
  - `popup_action_handler.h/.cpp`: adicionar `static void RotateSelection(Editor& editor, int quarterTurns);` — implementação: `if (editor.selection.size() < 2) return; editor.rotateSelection(quarterTurns); g_gui.RefreshView();` (incluir `editor/editor.h` já incluso).
  - `map_menu_handler.h`: declarar `void OnRotateSelectionCW/CCW/180(wxCommandEvent&);`.
  - `map_menu_handler.cpp`: em `BindEvents()` (após linha 44, junto do `MAP_POPUP_MENU_ROTATE`), bindar os 3 IDs; handlers chamam `PopupActionHandler::RotateSelection(editor, 1 / 3 / 2)`.

## Execution Order
1. Phase 1: Task 1.1 → depois Task 1.2 + Task 1.3 (paralelos); Task 1.4 pode rodar em paralelo com tudo da fase 1.
2. Phase 2: Task 2.1 (após 1.1 e 1.4).
3. Phase 3: Task 3.1 + Task 3.2 (paralelos, após 2.1).

## Testing Notes
(Sem build automatizado — o usuário compila manualmente. Verificação manual após o build:)
1. **Regressão do Structure Manager** (valida o refactor 1.1/1.2): abrir Structure Manager, colar uma estrutura, pressionar Z / trocar a rotação no choice — paredes, portas e bordas devem girar exatamente como antes.
2. **Rotação básica**: selecionar área 2+ tiles num andar, Ctrl+Alt+Right (90° CW), Ctrl+Alt+Left (CCW), Ctrl+Alt+Down (180°). Conferir pivô no canto superior-esquerdo do bounding box e Z inalterado.
3. **Remapeamento**: rotacionar área com grounds com borda mista (bordas internas devem reorientar), cluster de paredes com porta (alinhamento V↔H, portas preservando tipo/aberta-fechada), e itens rotateáveis simples (cadeia rotateTo).
4. **Undo/Redo**: 1 único Ctrl+Z desfaz a rotação inteira; o histórico mostra "Rotate Selection".
5. **Guardas**: seleção multi-andar → status "Cannot rotate selection across multiple floors." sem alterar o mapa; seleção encostada na borda do mapa onde a rotação sairia dos limites → "Rotation would move selection out of bounds." sem alterar nada.
6. **Enable/Disable**: itens do menu desabilitados com 0-1 tile selecionado; CW habilitado quando o Structure Manager tem paste ativo (e Ctrl+Alt+Right com foco no mapa gira o paste em vez da seleção).
7. **Popup**: right-click com 2+ tiles selecionados mostra submenu "Rotate selection" com as 3 opções funcionando; com 1 tile mostra o popup antigo intacto.
8. **Automagic off**: com Automagic desligado, rotação ainda gira paredes/bordas por remapeamento de ID (fases 2/4/5 puladas), sem borderize.
9. **MERGE_MOVE**: testar com a config ligada e desligada (merge vs replace no destino).
10. **House/flags**: rotacionar tiles de house com ground selecionado → house_id e flags viajam; spawns/creatures selecionados viajam juntos.

## Risks & Considerations
- **Refactor do CopyBuffer**: é a parte mais sensível — mover as lambdas verbatim; qualquer "melhoria" de lógica pode quebrar a rotação do Structure Manager. A única mudança permitida é lambda→método.
- **Selection após commits**: a fase 4 e a fase 5 iteram `editor.selection` DEPOIS dos commits das fases anteriores — nesse ponto a seleção já aponta para os tiles de destino (mesmo mecanismo já usado por `moveSelection` linhas 245-329). Não "otimizar" capturando a lista antes.
- **Ownership no tmp_storage**: o padrão de `moveSelection` usa `TileSet` com ponteiros crus (`release()` + `delete` no merge). Manter o padrão exato do arquivo; não introduzir double-free ao misturar com unique_ptr.
- **Leak de submenu no popup**: sem a troca `Delete`→`Destroy` no `MapPopupMenu::Update()`, o submenu vazaria a cada right-click (o comentário da linha 48 já avisa). Conferir que `Destroy` não quebra os itens normais (não quebra — wxMenu::Destroy cobre ambos).
- **Enum ActionIdentifier**: adicionar SEMPRE ao final (após `ACTION_LUA_SCRIPT`) — inserir no meio mudaria valores usados potencialmente em sessões live.
- **Hotkey com foco no diálogo do Structure Manager**: o accelerator Ctrl+Alt+Right dispara com foco no frame principal/canvas; com foco dentro do diálogo, o BT tinha um check extra (`MatchesActionHotkey` em `structure_manager_window.cpp:2989`) — fora de escopo aqui; a tecla Z global do redux já cobre esse caso.
- **NÃO portar**: ícone/case na actions history window (não existe no redux); tratamento de waypoints/house exits na rotação (paridade com BT — não tratados); tecla Z para rotação de seleção (Z já é variação de brush no canvas).
- **NUNCA buildar** — o usuário compila manualmente.
