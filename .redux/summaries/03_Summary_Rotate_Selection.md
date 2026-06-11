# Summary: Rotate Selection (rotacionar seleção 90° CW/CCW/180°)

**Plan**: .redux/plans/04_Rotate_Selection.md
**Executed**: 2026-06-11
**Status**: Completed

## Changes Made

| File | Action | Description |
|------|--------|-------------|
| source/editor/operations/rotation_utility.h | Created | Helper compartilhado `RotationUtility` (rotatePosition / rotateItem / rotateTileItems + caches de borda e catálogo de paredes) |
| source/editor/operations/rotation_utility.cpp | Created | Lógica extraída verbatim das lambdas de `CopyBuffer::rotate` (tabelas CW de BorderType/wall alignment, `build_door_key`, catálogo via `g_item_definitions.allIds()`, fallback `doRotate()`) |
| source/editor/copybuffer.cpp | Modified | `rotate()` agora usa `RotationUtility` (normalização de turns, `rotatePosition`, `rotateTileItems`); removidos includes órfãos (`brushes/brush.h`, `auto_border.h`, `wall_brush.h`, `item_definition_store.h`); comportamento idêntico (Structure Manager intacto) |
| source/CMakeLists.txt | Modified | Registrados rotation_utility.h (headers de operations) e rotation_utility.cpp (sources) |
| source/editor/action.h | Modified | Novo `ACTION_ROTATE_SELECTION` ao FINAL do enum (após `ACTION_LUA_SCRIPT`) |
| source/editor/action_queue.cpp | Modified | Nome legível "Rotate Selection" no switch de `getActionName` |
| source/editor/operations/selection_operations.h | Modified | Declaração `static void rotateSelection(Editor&, int quarterTurns)` |
| source/editor/operations/selection_operations.cpp | Modified | Implementação multi-fase (até 5 Actions num BatchAction): remover origem → borderizar vizinhos origem (threshold) → inserir rotacionado (`rotateTileItems` antes do merge/replace) → borderizar vizinhos destino (threshold) → re-borderizar os próprios tiles (sem threshold, só `create_borders && doborders`) |
| source/editor/editor.h | Modified | Declaração `void rotateSelection(int quarterTurns)` |
| source/editor/editor.cpp | Modified | Wrapper delegando a `SelectionOperations::rotateSelection` |
| source/ui/main_menubar.h | Modified | 3 ActionIDs (`ROTATE_SELECTION_CW/CCW/180`) + 3 declarações de handler |
| source/ui/main_menubar.cpp | Modified | 3 handlers delegados a `mapActionsHandler` |
| source/ui/menubar/menubar_action_manager.cpp | Modified | 3 `MAKE_ACTION`; `EnableItem` (CW também habilita com paste do Structure Manager ativo via `CanRotatePaste()`); include de structure_manager_window.h |
| source/ui/menubar/map_actions_handler.h | Modified | 3 declarações de handler |
| source/ui/menubar/map_actions_handler.cpp | Modified | Handlers reais: CW chama `StructureManagerDialog::RotatePaste()` primeiro; guarda `IsEditorOpen` + `selection.size() >= 2`; `rotateSelection(1/3/2)` + `RefreshView` |
| data/menubar.xml | Modified | Submenu "Rotate Selection" no menu Edit (após Paste) com hotkeys Ctrl+Alt+Right/Left/Down |
| source/ui/gui_ids.h | Modified | 3 IDs `MAP_POPUP_MENU_ROTATE_SELECTION_CW/CCW/180` após `MAP_POPUP_MENU_ROTATE` |
| source/ui/map_popup_menu.cpp | Modified | Fix de leak: `Delete()` → `Destroy()` no loop de limpeza; submenu "Rotate selection" (com ícone ICON_ROTATE) no ramo `selection.size() >= 2` |
| source/rendering/ui/popup_action_handler.h/.cpp | Modified | `PopupActionHandler::RotateSelection(Editor&, int quarterTurns)` |
| source/rendering/ui/map_menu_handler.h/.cpp | Modified | 3 binds + 3 handlers chamando `RotateSelection(editor, 1/3/2)` |

## Tasks Executed
- Task 1.1 Criar RotationUtility — Done
- Task 1.2 Refatorar CopyBuffer::rotate — Done
- Task 1.3 Registrar no CMakeLists — Done
- Task 1.4 ACTION_ROTATE_SELECTION — Done
- Task 2.1 SelectionOperations::rotateSelection + wrapper — Done
- Task 3.1 Cadeia do menubar — Done
- Task 3.2 Popup do canvas — Done

## Testing Instructions
1. Compilar manualmente (CMake já atualizado).
2. Selecionar 2+ tiles num único andar → Edit → Rotate Selection → Rotate Clockwise (ou Ctrl+Alt+Right). Verificar bordas, paredes e portas remapeadas; um único undo (Ctrl+Z) desfaz tudo ("Rotate Selection" no histórico).
3. Repetir com Ctrl+Alt+Left (CCW) e Ctrl+Alt+Down (180).
4. Right-click no canvas com 2+ tiles selecionados → submenu "Rotate selection" com as 3 opções.
5. Seleção em múltiplos andares → status bar "Cannot rotate selection across multiple floors."; seleção que sairia do mapa → "Rotation would move selection out of bounds." (nada alterado).
6. Com paste do Structure Manager ativo, Ctrl+Alt+Right gira o paste (CW habilitado no menu mesmo sem seleção 2+).
7. Regressão: Structure Manager → tecla Z / botão rotate do paste continua funcionando (usa `CopyBuffer::rotate`, agora via `RotationUtility`).
8. Automagic ligado/desligado: com automagic, vizinhos e os próprios tiles são re-borderizados; merge vs replace segue `Config::MERGE_MOVE`.

## Notes
- A re-borderização interna (fase 5) é exclusiva da rotação (paridade com BT `editor.cpp:1813-1836`): roda sem threshold, só quando `USE_AUTOMAGIC && BORDERIZE_DRAG` e algum ground foi movido (`doborders`).
- O duplicado `(+1,+1)` no loop de vizinhos da fase 4 foi mantido de propósito — cópia exata do `moveSelection` existente (e do BT); `sort/unique` remove o duplicado.
- `ACTION_ROTATE_SELECTION` foi adicionado ao final do enum para não deslocar valores existentes.
- Summary numerado 03 (sequência da pasta summaries); o plano correspondente é o 04 na pasta plans.
- Não portados (conforme plano): waypoints/house exits na rotação, tecla Z para rotação de seleção, ícone na actions history window.
