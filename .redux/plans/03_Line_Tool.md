# Plan: Line Tool (terceiro shape de brush, drag A→B com Bresenham)

## Overview
Adicionar uma nova ferramenta "Line Tool" como terceiro `BrushShape` (`BRUSHSHAPE_LINE`), acessivel pela toolbar Sizes ao lado de Square/Circle. Quando ativada, o usuario segura Shift e arrasta entre dois pontos para pintar uma linha de tiles (algoritmo de Bresenham) usando o brush atual (wall, ground ou raw), respeitando o footprint de tamanho do brush. Toda a linha vira uma unica acao undo-able.

## User Request
Adicionar uma nova ferramenta "Line Tool" ao editor, integrada como terceiro shape (BRUSHSHAPE_LINE) na toolbar Sizes, ao lado de Square e Circle. Usuario seleciona um brush (wall, ground, raw), clica no novo botao Line na toolbar Sizes, segura Shift e arrasta do ponto A ao ponto B. Durante o drag um overlay translucido mostra preview dos tiles da linha. Na soltura do mouse, os tiles ao longo da linha sao pintados como um unico undo. Respeita brush size: cada ponto da Bresenham aplica o footprint atual do brush. Fase 1 cobre apenas wall, ground e raw.

## Analysis
- **New Files**: Nao (a logica de Bresenham vai em `brush_utility.{h,cpp}`).
- **Modified Files**: Sim
  - `source/brushes/brush_enums.h` (novo enum value `BRUSHSHAPE_LINE`)
  - `source/brushes/brush_utility.h` / `brush_utility.cpp` (novo helper `GetLineTiles`)
  - `source/rendering/ui/drawing_controller.cpp` (`HandleRelease`: novo ramo para LINE)
  - `source/rendering/drawers/overlays/brush_overlay_drawer.cpp` (novo ramo de preview LINE)
  - `source/ui/toolbar/size_toolbar.cpp` (novo botao `TOOLBAR_SIZES_LINE` + handler + `Update()` + `UpdateBrushSize()`)
  - `source/ui/gui_ids.h` (novos IDs `PALETTE_BRUSHSHAPE_LINE` e `TOOLBAR_SIZES_LINE`)
  - `source/lua/lua_api_app.cpp` (reconhecer string `"line"` nos bindings de `brushShape`)
  - `source/brushes/brush_footprint.cpp` (ajuste em `containsOffset` para tratar `BRUSHSHAPE_LINE` igual a `BRUSHSHAPE_SQUARE` ao montar footprint, pois LINE so muda a forma de aplicacao macro e nao a forma do footprint por ponto)
- **CMakeLists Update**: Nao (nenhum arquivo novo).
- **Menu/Toolbar Wiring**: Sim (botao novo na size toolbar).
- **Action System**: Sim, indireto (reuso de `editor.draw(tilestodraw, tilestoborder, alt)` em batch unico).
- **Data Files (XML/TOML)**: Nao.

## Convencoes verificadas no codigo atual (referencias)
- `enum BrushShape { BRUSHSHAPE_CIRCLE, BRUSHSHAPE_SQUARE };` em `source/brushes/brush_enums.h:21-24`.
- `BrushUtility::GetTilesToDraw(...)` em `source/brushes/brush_utility.cpp:22-84` usa `BrushFootprint footprint = g_gui.GetBrushFootprint()` e itera o bbox aplicando `footprint.containsOffset(x,y)` por ponto unico do mouse. Vamos reutilizar exatamente o mesmo footprint, so que aplicado em cada ponto da linha.
- `BrushFootprint::containsOffset` em `source/brushes/brush_footprint.cpp:55-84`: para `BRUSHSHAPE_SQUARE` retorna true sempre que cabe no bbox. Para LINE, o footprint por ponto deve comportar-se igual ao SQUARE (so a aplicacao macro muda).
- `HandleRelease` em `source/rendering/ui/drawing_controller.cpp:301-415` ja faz o "release-drag" para Wall, Square, Circle e Spawn; nosso ramo LINE entra antes do branch SQUARE/CIRCLE e tambem antes do branch especifico de WallBrush, porque o usuario espera que LINE sobrescreva o comportamento de wall-rectangle quando shape=LINE.
- `BrushOverlayDrawer::draw` em `source/rendering/drawers/overlays/brush_overlay_drawer.cpp:129-296`: o bloco `if (drawer->canvas->drawing_controller->IsDraggingDraw())` ja trata Wall, SQUARE+RAW, SQUARE+normal e CIRCLE; nosso ramo LINE entra antes do branch WallBrush para que LINE tambem sobrescreva o preview retangular de paredes.
- `editor.draw(tilestodraw, tilestoborder, alt)` em `source/editor/editor.h:104` cria um unico batch internamente (verificado em uso similar no ramo Wall/Square/Circle), entao reusar essa chamada ja garante "single undo".
- IMAGE assets disponiveis: somente `IMAGE_RECTANGULAR_*` e `IMAGE_CIRCULAR_*` (ver `source/util/image_manager.h:1739-1767`). Nao ha sprite "line" — usaremos `wxArtProvider` `wxART_NORMAL_FILE` como placeholder (decisao abaixo).

## Tasks

### Phase 1: Estruturas de dados e enum

#### Task 1.1: Adicionar `BRUSHSHAPE_LINE` ao enum
- **Description**: Estender o enum `BrushShape` para suportar a nova forma.
- **Files**:
  - `source/brushes/brush_enums.h`
- **Details**:
  - Em `enum BrushShape { ... }` (linhas 21-24) adicionar `BRUSHSHAPE_LINE` apos `BRUSHSHAPE_SQUARE`. Resultado:
    ```cpp
    enum BrushShape {
        BRUSHSHAPE_CIRCLE,
        BRUSHSHAPE_SQUARE,
        BRUSHSHAPE_LINE,
    };
    ```
  - Manter ordem (SQUARE antes de LINE) para nao quebrar valores numericos persistidos em settings/Lua que dependam de `BRUSHSHAPE_SQUARE == 1` se houver.

#### Task 1.2: Garantir que `BrushFootprint::containsOffset` aceita LINE
- **Parallel**: Sim (com 1.1, mas escreve no mesmo modulo distinto)
- **Description**: Garantir que `BrushFootprint::containsOffset` trata `BRUSHSHAPE_LINE` exatamente como `BRUSHSHAPE_SQUARE` (footprint quadrado por ponto da linha).
- **Files**:
  - `source/brushes/brush_footprint.cpp`
- **Details**:
  - Em `BrushFootprint::containsOffset` (linhas 55-84), trocar:
    ```cpp
    if (shape == BRUSHSHAPE_SQUARE) {
        return true;
    }
    ```
    por:
    ```cpp
    if (shape == BRUSHSHAPE_SQUARE || shape == BRUSHSHAPE_LINE) {
        return true;
    }
    ```
  - Em `MakeBrushFootprint` (`brush_footprint.cpp:22`) e em `BrushSizeState` / `BrushFootprint` membros `shape` (em `brush_footprint.h:7,15`), nao e preciso mudar nada: o `shape` ja e propagado.

#### Task 1.3: Adicionar IDs `TOOLBAR_SIZES_LINE` e `PALETTE_BRUSHSHAPE_LINE`
- **Parallel**: Sim (1.1, 1.2)
- **Description**: Adicionar dois novos IDs ao enum `EditorActionID`.
- **Files**:
  - `source/ui/gui_ids.h`
- **Details**:
  - Apos `PALETTE_BRUSHSHAPE_CIRCLE` (linha 118), adicionar `PALETTE_BRUSHSHAPE_LINE,`.
  - Apos `TOOLBAR_SIZES_RECTANGULAR` (linha 188), adicionar `TOOLBAR_SIZES_LINE,`. Inserir antes de `TOOLBAR_SIZES_1` para deixar agrupado com os shape buttons.

### Phase 2: Algoritmo Bresenham com footprint

#### Task 2.1: Implementar `BrushUtility::GetLineTiles`
- **Depends On**: 1.1, 1.2
- **Description**: Novo helper estatico que recebe ponto A e ponto B, executa Bresenham 2D no plano e, para cada ponto da linha, aplica o `BrushFootprint` atual (`g_gui.GetBrushFootprint()`) para popular `tilestodraw` e `tilestoborder`. De-duplicar tiles para evitar trabalho redundante no Editor::draw.
- **Files**:
  - `source/brushes/brush_utility.h`
  - `source/brushes/brush_utility.cpp`
- **Details (header)**:
  - Em `class BrushUtility` (publico) adicionar:
    ```cpp
    static void GetLineTiles(const Position& a, const Position& b,
                              std::vector<Position>* tilestodraw,
                              std::vector<Position>* tilestoborder);
    ```
- **Details (cpp)**:
  - Includes: ja tem `brushes/brush_utility.h`, `ui/gui.h`, `map/position.h`.
  - Adicionar `#include <unordered_set>` para de-dup.
  - Implementacao (espelha estrutura de `GetTilesToDraw` no ramo nao-fill):
    1. Reusar `const BrushFootprint footprint = g_gui.GetBrushFootprint();`
    2. Bresenham padrao entre `(a.x,a.y)` e `(b.x,b.y)` no mesmo z = `a.z` (assumir mesmo floor; se z difere usar `a.z`). Para cada ponto `(px, py)` da linha:
       - Iterar `y` de `footprint.min_offset_y - 1` ate `footprint.max_offset_y + 1` e `x` analogo (mesmo loop de `GetTilesToDraw:61-82`).
       - Para `containsOffset(x,y)` true: pushar `Position(px+x, py+y, a.z)` em `tilestodraw`.
       - Para vizinhanca 3x3: se algum vizinho `containsOffset` true, pushar em `tilestoborder` (replica logica existente).
    3. De-duplicar: usar `unordered_set<int64_t>` com chave `((int64_t)y << 32) | (uint32_t)x` (z fixo) ou simplesmente `unordered_set<Position, PositionHash>` se Position ja tiver hash. Verificar em `map/position.h`; se nao houver `std::hash<Position>`, usar a chave bitshift e checar antes de pushar.
  - Pseudocodigo Bresenham (referencia):
    ```cpp
    int x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
    int dx =  std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        applyFootprintAt(x0, y0); // popula tilestodraw/tilestoborder com de-dup
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    ```
  - **Importante**: nao chamar `g_gui.GetBrushFootprint()` dentro do loop; cachear uma vez.

### Phase 3: Integracao no HandleRelease (commit da acao)

#### Task 3.1: Ramo LINE em `DrawingController::HandleRelease`
- **Depends On**: 2.1
- **Description**: Quando `dragging_draw` termina e o usuario esta com `BRUSHSHAPE_LINE`, computar `tilestodraw`/`tilestoborder` via `GetLineTiles` e chamar `editor.draw(tilestodraw, tilestoborder, ground_replace_release)`. Esse ramo deve ter prioridade sobre o branch WallBrush e sobre o branch SQUARE/CIRCLE.
- **Files**:
  - `source/rendering/ui/drawing_controller.cpp`
- **Details**:
  - Localizar `HandleRelease` (linha 301). Logo apos `if (brush)` e antes de `if (brush->is<SpawnBrush>())` (linha 305), inserir:
    ```cpp
    if (g_gui.GetBrushShape() == BRUSHSHAPE_LINE
        && (brush->is<WallBrush>() || brush->is<GroundBrush>() || brush->is<RAWBrush>())) {
        PositionVector tilestodraw;
        PositionVector tilestoborder;
        Position start(canvas->last_click_map_x, canvas->last_click_map_y, mouse_map_pos.z);
        BrushUtility::GetLineTiles(start, mouse_map_pos, &tilestodraw, &tilestoborder);
        bool ground_replace_release = brush->is<GroundBrush>()
            && IsGroundReplaceModifier(shift_down, ctrl_down, alt_down);
        if (ctrl_down) {
            editor.undraw(tilestodraw, tilestoborder, ground_replace_release);
        } else {
            editor.draw(tilestodraw, tilestoborder, ground_replace_release);
        }
    } else if (brush->is<SpawnBrush>()) {
        // ... bloco SpawnBrush atual permanece igual ...
    } else {
        // ... bloco Wall/Square/Circle atual permanece igual ...
    }
    ```
  - Garantir `#include "brushes/brush_utility.h"` em `drawing_controller.cpp` (provavelmente ja esta; senao adicionar).
  - **Restricao de Fase 1**: outros brushes (carpet, table, doodad, spawn) continuam usando o branch existente — o `if` exige `WallBrush || GroundBrush || RAWBrush`.
  - **Single undo**: `editor.draw(PositionVector, PositionVector, bool)` ja envolve uma BatchAction interna (ver `editor.h:104`), entao essa unica chamada e suficiente.

### Phase 4: Preview overlay durante o drag

#### Task 4.1: Ramo LINE em `BrushOverlayDrawer::draw`
- **Depends On**: 2.1, 3.1
- **Description**: Durante o drag (`drawing_controller->IsDraggingDraw()`), quando shape=LINE e brush valido (wall/ground/raw), desenhar overlay translucido em cada tile retornado por uma chamada local `GetLineTiles(...)`.
- **Files**:
  - `source/rendering/drawers/overlays/brush_overlay_drawer.cpp`
- **Details**:
  - Em `BrushOverlayDrawer::draw`, no bloco `if (drawer->canvas->drawing_controller->IsDraggingDraw())` (linha 129), antes do branch WallBrush (linha 132), inserir:
    ```cpp
    if (g_gui.GetBrushShape() == BRUSHSHAPE_LINE
        && (brush->is<WallBrush>() || brush->is<GroundBrush>() || brush->is<RAWBrush>())) {
        std::vector<Position> tilestodraw;
        Position start(drawer->canvas->last_click_map_x, drawer->canvas->last_click_map_y, view.floor);
        Position end_pos(view.mouse_map_x, view.mouse_map_y, view.floor);
        BrushUtility::GetLineTiles(start, end_pos, &tilestodraw, nullptr);

        RAWBrush* raw_brush = brush->is<RAWBrush>() ? brush->as<RAWBrush>() : nullptr;
        if (g_gui.gfx.ensureAtlasManager()) {
            const AtlasManager& atlas = *g_gui.gfx.getAtlasManager();
            for (const Position& p : tilestodraw) {
                int cx = p.x * TILE_SIZE - view.view_scroll_x - view.getFloorAdjustment();
                int cy = p.y * TILE_SIZE - view.view_scroll_y - view.getFloorAdjustment();
                if (raw_brush) {
                    item_drawer->DrawRawBrush(sprite_batch, sprite_drawer, cx, cy,
                                              raw_brush->getItemID(), 160, 160, 160, 160);
                } else {
                    sprite_batch.drawRect(static_cast<float>(cx), static_cast<float>(cy),
                                          static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE),
                                          brushColor, atlas);
                }
            }
        }
    } else if (brush->is<WallBrush>()) {
        // ... bloco WallBrush existente permanece ...
    } else {
        // ... bloco SQUARE/CIRCLE existente permanece ...
    }
    ```
  - Incluir `#include "brushes/brush_utility.h"` no topo do arquivo se ainda nao estiver.
  - **Performance**: `GetLineTiles` e chamado por frame durante drag; com size=0 e linhas de ate ~200 tiles e barato. Para size grande (size_x=11) e linhas longas, o footprint quadrado pode gerar muitos tiles — aceitavel para Fase 1.

### Phase 5: UI e toolbar

#### Task 5.1: Botao `TOOLBAR_SIZES_LINE` na size toolbar
- **Depends On**: 1.3
- **Description**: Adicionar o terceiro botao de shape "Line Brush" na size toolbar, com placeholder de bitmap, handler e logica de toggling.
- **Files**:
  - `source/ui/toolbar/size_toolbar.cpp`
- **Details (construtor SizeToolBar::SizeToolBar)**:
  - Antes do `toolbar->AddSeparator();` (linha 32), apos a linha do CIRCULAR (linha 31), adicionar:
    ```cpp
    wxBitmap line_bitmap = wxArtProvider::GetBitmap(wxART_NORMAL_FILE, wxART_TOOLBAR, icon_size);
    // TODO: substituir por sprite proprio quando criado em data/png/line_4_small.png
    toolbar->AddTool(TOOLBAR_SIZES_LINE, wxEmptyString, line_bitmap, wxNullBitmap,
                     wxITEM_CHECK, "Line Brush", wxEmptyString, nullptr);
    ```
  - Observacao: ja existe `#include <wx/artprov.h>` no topo do arquivo (linha 11).
- **Details (`SizeToolBar::Update`)**:
  - Apos `toolbar->EnableTool(TOOLBAR_SIZES_RECTANGULAR, has_map);` (linha 56) adicionar:
    ```cpp
    toolbar->EnableTool(TOOLBAR_SIZES_LINE, has_map);
    ```
- **Details (`SizeToolBar::UpdateBrushSize`)**:
  - No bloco `if (shape == BRUSHSHAPE_CIRCLE)` (linha 73) e no `else` (linha 85), adicionar `toolbar->ToggleTool(TOOLBAR_SIZES_LINE, false);` para desligar Line quando outro shape esta ativo.
  - Adicionar um terceiro ramo apos o `else` para `BRUSHSHAPE_LINE`:
    ```cpp
    if (shape == BRUSHSHAPE_LINE) {
        toolbar->ToggleTool(TOOLBAR_SIZES_CIRCULAR, false);
        toolbar->ToggleTool(TOOLBAR_SIZES_RECTANGULAR, false);
        toolbar->ToggleTool(TOOLBAR_SIZES_LINE, true);
        // Para size icons, reusar os RECTANGULAR (a forma do footprint por ponto e quadrada)
        wxSize icon_size = wxSize(16, 16);
        toolbar->SetToolBitmap(TOOLBAR_SIZES_1, IMAGE_MANAGER.GetBitmap(IMAGE_RECTANGULAR_1_SMALL, icon_size));
        // ... idem para 2..7
    }
    ```
  - Estrutura final sugerida: trocar o `if/else` por `if (shape == CIRCLE) {...} else if (shape == LINE) {...} else {...}` (SQUARE = default).
- **Details (`SizeToolBar::OnToolbarClick`)**:
  - No `switch (event.GetId())` (linha 115), adicionar:
    ```cpp
    case TOOLBAR_SIZES_LINE:
        g_gui.SetBrushShape(BRUSHSHAPE_LINE);
        break;
    ```

#### Task 5.2: Estado inicial do botao
- **Parallel**: Sim (com 5.1, mesmo arquivo so coordenar conflitos)
- **Description**: Garantir que ao abrir o editor o botao Line nao venha pre-tocado.
- **Files**:
  - `source/ui/toolbar/size_toolbar.cpp`
- **Details**:
  - O `ToggleTool(TOOLBAR_SIZES_RECTANGULAR, true)` (linha 41) ja deixa SQUARE como default; nao precisa adicionar nada extra para LINE — mantemos OFF por default.

### Phase 6: Bindings Lua

#### Task 6.1: Suportar `"line"` em `app.brushShape` getter/setter
- **Depends On**: 1.1
- **Description**: Permitir Lua ler/escrever `"line"` alem de `"circle"` e `"square"`.
- **Files**:
  - `source/lua/lua_api_app.cpp`
- **Details (getter, linha 714-715)**:
  - Trocar a expressao ternaria por:
    ```cpp
    } else if (key == "brushShape") {
        BrushShape bs = g_gui.GetBrushShape();
        const char* s = "square";
        if (bs == BRUSHSHAPE_CIRCLE) s = "circle";
        else if (bs == BRUSHSHAPE_LINE) s = "line";
        return sol::make_object(lua, s);
    }
    ```
- **Details (setter, linha 734-741)**:
  - Adicionar terceiro ramo:
    ```cpp
    } else if (s == "line") {
        g_gui.SetBrushShape(BRUSHSHAPE_LINE);
    }
    ```

### Phase 7: Polimento / edge cases

#### Task 7.1: Tratamento de drag com ponto unico (A == B)
- **Description**: Quando o usuario clica e solta no mesmo tile com LINE, `GetLineTiles` deve produzir um unico ponto (igual ao single-click do brush). O Bresenham padrao ja trata isso (loop executa pelo menos uma vez antes do break), entao nenhum codigo extra e necessario — apenas validar manualmente.
- **Files**: nenhum (verificacao apenas)

#### Task 7.2: Cancelamento de drag fora do mapa
- **Description**: Se `mouse_map_pos.x < 0` ou fora dos limites, `GetLineTiles` ainda gerara Positions invalidas. Garantir que `editor.draw(tilestodraw, tilestoborder, alt)` ja descarta posicoes invalidas — isso e o comportamento atual de Wall/Square/Circle, entao nao adicionar checagem nova.
- **Files**: nenhum

#### Task 7.3: Conferir restricao do branch RAW no preview
- **Description**: O preview overlay para RAW desenha o sprite via `item_drawer->DrawRawBrush`; verificar que essa chamada ja foi feita por tile em outros branches (sim, em `brush_overlay_drawer.cpp:286-287`), entao seguro reusar.
- **Files**: nenhum

#### Task 7.4: Documentar limitacao Fase 1
- **Description**: Adicionar comentario inline em `drawing_controller.cpp` no novo `if` LINE explicando que apenas Wall/Ground/RAW estao habilitados:
  ```cpp
  // Phase 1 of Line Tool: only Wall, Ground, and RAW brushes are supported.
  // Other brushes (carpet, table, doodad, spawn) continue using the existing
  // rectangle/circle release semantics.
  ```
- **Files**: `source/rendering/ui/drawing_controller.cpp` (comentario apenas)

## Execution Order

1. **Phase 1 (paralelo)**: Tasks 1.1, 1.2 e 1.3 podem ser feitas em paralelo (arquivos distintos).
2. **Phase 2**: Task 2.1 depende de 1.1 + 1.2 (enum + footprint).
3. **Phase 3**: Task 3.1 depende de 2.1.
4. **Phase 4**: Task 4.1 depende de 2.1 e idealmente apos 3.1 (mas pode ir em paralelo com 3.1 se o desenvolvedor for cuidadoso).
5. **Phase 5 (paralelo com 6)**: Tasks 5.1 e 5.2 dependem de 1.3.
6. **Phase 6 (paralelo com 5)**: Task 6.1 depende de 1.1.
7. **Phase 7**: Tarefas de polimento ao final.

## Testing Notes (manual, sem build automatizado)

O usuario compila manualmente. Apos build:

1. **Sanity check Square/Circle**: confirmar que o comportamento existente nao mudou — clicar e arrastar com Wall/Ground/RAW em modo Square e em modo Circle deve produzir resultado identico ao atual.
2. **Toolbar UI**:
   - Abrir um mapa, conferir 3 botoes na size toolbar (Rectangular/Circular/Line).
   - Clicar em Line — botoes Rectangular e Circular devem desativar; Line fica pressionado.
   - Hover deve mostrar tooltip "Line Brush".
3. **Wall brush**:
   - Selecionar uma wall brush.
   - Shape = Line, size = 0.
   - Shift+drag de A para B horizontal — deve gerar parede reta horizontal, com bordering correto na pos-acao.
   - Diagonal (45 graus) — deve gerar uma linha em escada de Bresenham; cada wall vai se realinhar conforme `WallBorderCalculator`.
4. **Ground brush**:
   - Selecionar grass (ou outra ground).
   - Size 2 — deve gerar uma "estrada" de 5 tiles de largura (span_x=5) seguindo a linha.
   - Verificar que autoborder ainda funciona corretamente nas extremidades.
5. **RAW brush**:
   - Selecionar um item raw (ex. tronco 1234).
   - Size 0, Line — deve colocar um item por tile ao longo da Bresenham.
6. **Single undo**:
   - Apos pintar uma linha, Ctrl+Z deve desfazer toda a linha em uma unica operacao.
   - Ctrl+Y deve refazer toda a linha.
7. **Preview overlay**:
   - Durante shift+drag, ver tiles destacados em translucido ao longo da Bresenham.
   - Para Wall: cor de wall brush; para Ground: cor de brush; para RAW: sprite do item em 160 alpha.
8. **Outros brushes**:
   - Selecionar Carpet/Table/Doodad/Spawn com shape=Line. Comportamento esperado: usa o branch antigo (carpet com retangulo, doodad single-click, spawn radius) — Line nao se aplica. Verificar que isso e claro para o usuario (Fase 2 expandira).
9. **Lua bindings**:
   - No console Lua: `app.brushShape = "line"` deve ativar o botao.
   - `print(app.brushShape)` deve imprimir `"line"`.

## Risks & Considerations

1. **Sprite placeholder**: `wxArtProvider::wxART_NORMAL_FILE` nao representa visualmente "linha". Tarefa de follow-up: criar `data/png/line_4_small.png` (16x16) e adicionar a `IMAGE_MANAGER` como `IMAGE_LINE_4_SMALL`. Por enquanto o placeholder e suficiente para teste funcional. O usuario pode preferir um asset proprio antes do merge — confirmar.
2. **Ordem do enum BrushShape**: adicionar `BRUSHSHAPE_LINE` apos `BRUSHSHAPE_SQUARE` mantem os valores 0 e 1 estaveis. Se algum local persiste `BrushShape` em settings (verificar `g_settings`), o valor 2 fica reservado para LINE.
3. **Footprint diagonal**: com size > 0 em diagonal, tiles adjacentes na Bresenham podem sobrepor footprint e gerar duplicatas. Usamos de-duplicacao para evitar `editor.draw` processar o mesmo tile multiplas vezes (mais barato e evita race em bordering).
4. **Preview overhead**: durante drag rapido com size=11 e linhas de ~50 tiles, sao ~50 * (23*23) = ~26k iteracoes de footprint por frame. Em hardware moderno e aceitavel, mas se causar lag pode-se aplicar throttling (so recalcular se ponto B mudou). Manter simples na Fase 1.
5. **WallBrush footprint**: walls historicamente ignoram size > 0 no codigo atual de Wall+drag rectangle. Aqui, com LINE, vamos respeitar o footprint mesmo para wall (size > 0 produz wall de grossura > 1). Isso e uma melhoria; documentar no PR.
6. **Floors diferentes em A e B**: Drawing usa o mesmo z durante todo o drag (`view.floor`). `GetLineTiles` assume `a.z`. Nao ha risco de mismatch.
7. **HandleClick com `canDrag()`**: o drag so inicia se `brush->canDrag() == true`. Wall (`wall_brush.h:45`), Ground (`brush.h:227` via TerrainBrush) e RAW (`raw_brush.h:39`) ja retornam true, entao o drag se inicia normalmente — sem mudanca necessaria.
8. **`editor.undraw(tilestodraw, tilestoborder, alt)` com Ctrl**: igual ao branch atual de SQUARE/CIRCLE, suportamos Ctrl+Shift+drag para apagar linha. Util e gratis.
9. **Compatibilidade settings antigos**: `Settings` armazena `BRUSH_SHAPE` (verificar `g_settings`); se for int, valor 2 nao quebra leitura de configs antigos (que so tem 0 ou 1). Nada a migrar.
10. **Live editing (multiplayer)**: `editor.draw(tilestodraw, tilestoborder, alt)` no servidor live ja propaga via NetworkedAction. Como reusamos a mesma chamada, Line Tool funciona em live sem trabalho extra.
