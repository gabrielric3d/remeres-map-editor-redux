# Summary: Line Tool (terceiro shape de brush, drag A->B com Bresenham)

**Plan**: Line Tool (BRUSHSHAPE_LINE) — toolbar Sizes
**Executed**: 2026-05-24
**Status**: Completed

## Changes Made

| File | Action | Description |
|------|--------|-------------|
| `source/brushes/brush_enums.h` | Modified | Adicionado `BRUSHSHAPE_LINE` ao enum `BrushShape` |
| `source/brushes/brush_footprint.cpp` | Modified | `containsOffset` agora trata `BRUSHSHAPE_LINE` igual a `BRUSHSHAPE_SQUARE` (footprint por ponto e quadrado; o LINE so muda a forma macro de aplicacao) |
| `source/ui/gui_ids.h` | Modified | Novos IDs `PALETTE_BRUSHSHAPE_LINE` e `TOOLBAR_SIZES_LINE` |
| `source/brushes/brush_utility.h` | Modified | Declaracao de `BrushUtility::GetLineTiles(Position a, Position b, ...)` |
| `source/brushes/brush_utility.cpp` | Modified | Implementacao de `GetLineTiles` com Bresenham 2D + aplicacao do footprint atual em cada ponto da linha + deduplicacao via `unordered_set<int64_t>` (chave `(y<<32) | x`) para `tilestodraw` e `tilestoborder`. Incluidos `<cmath>`, `<cstdint>`, `<unordered_set>` |
| `source/rendering/ui/drawing_controller.cpp` | Modified | Novo ramo prioritario em `HandleRelease`: se `BrushShape == BRUSHSHAPE_LINE` e brush e `WallBrush`/`GroundBrush`/`RAWBrush`, monta `tilestodraw`/`tilestoborder` via `GetLineTiles` e chama `editor.draw/undraw` em batch unico. Comentario indica limitacao da Fase 1. |
| `source/rendering/drawers/overlays/brush_overlay_drawer.cpp` | Modified | Preview overlay LINE durante drag: usa `GetLineTiles` para obter os tiles e desenha `DrawRawBrush` (RAW) ou `sprite_batch.drawRect(brushColor)` (Wall/Ground). Adicionados includes `brushes/brush_utility.h` e `brushes/ground/ground_brush.h` |
| `source/ui/toolbar/size_toolbar.cpp` | Modified | Botao `TOOLBAR_SIZES_LINE` na size toolbar (icone wxART_NORMAL_FILE como placeholder, TODO de sprite proprio); `Update()` habilita o tool junto com os demais; `UpdateBrushSize` agora trata 3 ramos (CIRCLE / LINE / SQUARE) togglando os 3 botoes corretamente; `OnToolbarClick` chama `g_gui.SetBrushShape(BRUSHSHAPE_LINE)` |
| `source/lua/lua_api_app.cpp` | Modified | Getter de `app.brushShape` retorna `"line"` quando `BRUSHSHAPE_LINE`; setter aceita string `"line"` |

## Tasks Executed

- **1.1** Adicionar `BRUSHSHAPE_LINE` ao enum — Done
- **1.2** `BrushFootprint::containsOffset` aceita LINE — Done
- **1.3** IDs `TOOLBAR_SIZES_LINE` e `PALETTE_BRUSHSHAPE_LINE` — Done
- **2.1** Implementar `BrushUtility::GetLineTiles` — Done
- **3.1** Ramo LINE em `DrawingController::HandleRelease` — Done
- **4.1** Ramo LINE em `BrushOverlayDrawer::draw` — Done
- **5.1** Botao `TOOLBAR_SIZES_LINE` (construtor + `Update` + `UpdateBrushSize` + `OnToolbarClick`) — Done
- **6.1** Lua bindings `app.brushShape = "line"` (getter + setter) — Done
- **7.4** Comentario de limitacao Fase 1 — Done (embutido na Task 3.1)

## Testing Instructions

1. Abrir um mapa.
2. Verificar que o terceiro botao "Line Brush" aparece na size toolbar ao lado de Circular e Rectangular.
3. Selecionar uma `WallBrush`, clicar no botao Line, segurar Shift e arrastar entre dois pontos do mapa.
4. Durante o drag, conferir que um overlay translucido marca os tiles da linha entre A e B (Bresenham).
5. Soltar o mouse: a parede deve ser pintada ao longo da linha como um unico undo (Ctrl+Z desfaz tudo).
6. Repetir com `GroundBrush` (auto-borders devem ser aplicados ao redor) e `RAWBrush` (preview mostra o sprite do item).
7. Ctrl+arrasto com LINE em GroundBrush deve apagar a linha (undraw).
8. Trocar de volta para Rectangular / Circular e confirmar que o comportamento original nao mudou.
9. Lua: `app.brushShape = "line"` deve setar e `print(app.brushShape)` deve retornar `"line"`.

## Notes

- **Limitacao Fase 1**: o ramo LINE so se ativa para `WallBrush`, `GroundBrush` e `RAWBrush`. Outras brushes (Carpet, Table, Doodad, Spawn, Creature, etc.) caem nos ramos rectangular/circular existentes mesmo se o usuario tiver LINE selecionado — comportamento intencional ate Fase 2.
- **Icone do botao Line**: placeholder com `wxART_NORMAL_FILE`. O comentario `// TODO: substituir por sprite proprio quando criado em data/png/line_4_small.png` deixa registrado o asset que precisa ser criado posteriormente. O `IMAGE_MANAGER` nao tem um enum `IMAGE_LINE_*_SMALL` ainda; quando o sprite for adicionado, basta cadastrar no `image_manager.h/cpp` e trocar a inicializacao do `line_bitmap` em `size_toolbar.cpp`.
- **Deduplicacao**: a Bresenham gera tiles que se sobrepoem em footprints > 1x1; o `unordered_set<int64_t>` evita pushar duplicatas tanto em `tilestodraw` quanto em `tilestoborder`, mantendo o batch de `editor.draw` enxuto.
- **Sem novos arquivos**: nenhuma entrada nova foi adicionada a `source/CMakeLists.txt` — toda a logica vive em arquivos pre-existentes.
- **`canDrag()`**: Wall/Ground/RAW ja retornam `canDrag()=true`, entao o `ASSERT(brush->canDrag())` em `BrushOverlayDrawer::draw` continua satisfeito quando o usuario arrasta com LINE.
- **Settings persistence**: `BrushShape` e armazenado in-memory por `BrushManager`; nao ha persistencia em TOML para esse campo, entao adicionar um terceiro valor ao enum nao quebra serializacao existente.
