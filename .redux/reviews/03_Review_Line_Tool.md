# Code Review: Line Tool (BRUSHSHAPE_LINE)

## Summary
- **Arquivos revisados**: 9
- **Issues encontradas**: 4 (0 criticos, 1 medio, 3 baixos)
- **Avaliacao geral**: APROVADO

A implementacao segue fielmente o plano `.redux/plans/03_Line_Tool.md`. Nenhum problema critico de memoria, uso-apos-move, leak ou crash foi encontrado. O codigo respeita convencoes do projeto (snake_case, raw pointers nao-owning, `g_gui.GetBrushFootprint()` cacheado fora dos loops, `brush->is<T>()/as<T>()`, includes hierarquicos). Ordem do enum mantida (CIRCLE=0, SQUARE=1, LINE=2). Branches LINE corretamente posicionados ANTES de WallBrush em `HandleRelease` e em `BrushOverlayDrawer::draw`. Toolbar `UpdateBrushSize` sincroniza os 3 botoes em todos os 3 ramos.

## Issues Criticos

Nenhum.

## Issues Medios

### [MEDIUM] Possivel UB em shift de inteiro negativo na chave de dedup
- **Arquivo**: `source/brushes/brush_utility.cpp:153-155`
- **Tipo**: Bug potencial / Comportamento indefinido
- **Descricao**: A funcao `makeKey` faz `(static_cast<int64_t>(y) << 32) | static_cast<uint32_t>(x)`. Quando `y` e negativo, o cast para `int64_t` mantem o sinal (sign-extension), gerando bits superiores `0xFFFFFFFF`. Deslocar um `int64_t` negativo a esquerda e **comportamento indefinido em C++17 e anteriores** (well-defined apenas a partir de C++20, e mesmo assim apenas para tipos sem overflow no resultado). Em pratica em x86/MSVC/GCC isso funciona como esperado, mas e tecnicamente UB.
- **Impacto**: Em C++17 (provavel padrao do projeto), compiladores podem otimizar de forma inesperada. Tambem, posicoes com coordenadas grandes ou negativas podem colidir na chave se o shift for tratado como saturado.
- **Fix sugerido**:
```cpp
const auto makeKey = [](int x, int y) -> int64_t {
    return (static_cast<int64_t>(static_cast<uint32_t>(y)) << 32)
        | static_cast<uint32_t>(x);
};
```
Cast para `uint32_t` primeiro elimina sign-extension e o shift de unsigned e sempre well-defined. Como esperado, a chave continua unica para todos os pares `(x,y)` validos de int32.

## Issues Baixos

### [LOW] Icone placeholder do botao Line
- **Arquivo**: `source/ui/toolbar/size_toolbar.cpp:32-34`
- **Tipo**: Quality / UX
- **Sugestao**: O placeholder `wxArtProvider::GetBitmap(wxART_NORMAL_FILE, ...)` ja esta documentado com TODO inline. Criar `data/png/line_4_small.png` (16x16) e adicionar `IMAGE_LINE_*_SMALL` ao `image_manager.h/cpp` antes do merge final. Risco baixo porque o tooltip "Line Brush" deixa a funcao clara.

### [LOW] Dedup poderia usar `std::unordered_set<Position>` se Position tiver hash
- **Arquivo**: `source/brushes/brush_utility.cpp:150-155`
- **Tipo**: Quality / Style
- **Sugestao**: O codigo usa `unordered_set<int64_t>` com chave bitshift. Funciona perfeitamente para z fixo (a linha esta toda no mesmo floor), mas se algum dia LINE for estendido para multi-floor, a chave precisara incluir z. Documentar inline ou usar a chave bitshift de forma defensiva (ja e razoavel para Fase 1). Sem impacto funcional na Fase 1.

### [LOW] `start.z` em `HandleRelease` usa `mouse_map_pos.z` em vez de `canvas->last_click_map_z`
- **Arquivo**: `source/rendering/ui/drawing_controller.cpp:312`
- **Tipo**: Quality / Style
- **Sugestao**: O start da linha usa `mouse_map_pos.z` (floor no momento do release). Se o usuario trocar de floor durante o drag (raro), o behavior fica um pouco inconsistente. Comportamento ja e identico aos ramos SQUARE/CIRCLE do codigo, entao mantem consistencia com o que ja existe — sem necessidade de mudar. Apenas registrado como nota.

## Verificacoes Aprovadas

| Checklist | Resultado |
|-----------|-----------|
| Memory management (sem `new`/`delete` raw para Actions/Changes/Tiles/Items) | OK (Tudo via `editor.draw` que ja batch interno) |
| `std::move()` para unique_ptr transfers | OK (N/A — nao ha unique_ptr novo) |
| `Theme::Get(Theme::Role::X)` para cores | OK (overlay reusa `brushColor` ja calculado) |
| `brush->is<T>()/as<T>()` template | OK em todos os locais |
| Include paths hierarquicos | OK (`brushes/brush_utility.h`, `brushes/ground/ground_brush.h`) |
| `editor->map / selection / actionQueue` | OK (reuso de `editor.draw/undraw`) |
| `CMakeLists.txt` registrado | OK — nenhum arquivo novo |
| Null checks antes de deref | OK (`brush` validado antes do branch LINE) |
| Sem use-after-move | OK |
| Event handlers wx properly bound | OK (`SizeToolBar::OnToolbarClick` registrado/desregistrado) |
| Sem cores hardcoded | OK |
| Estilo consistente | OK (snake_case, indentacao com tabs) |
| Ordem do enum BrushShape mantida | OK (CIRCLE=0, SQUARE=1, LINE=2) |
| Toolbar `UpdateBrushSize` sincroniza os 3 botoes | OK (3 ramos: CIRCLE, LINE, SQUARE; cada um toggla os outros 2 para false) |
| Branch LINE ANTES de WallBrush em `HandleRelease` | OK (linha 305) |
| Branch LINE ANTES de WallBrush em `BrushOverlayDrawer::draw` | OK (linha 134) |
| Bresenham trata A==B | OK (loop aplica footprint pelo menos uma vez antes do break) |
| Bresenham horizontal/vertical/diagonal | OK (algoritmo padrao com `err = dx + dy`, `dy = -abs(...)`) |
| `g_gui.GetBrushFootprint()` cacheado fora do loop | OK (linha 147 — fora do while Bresenham) |
| `containsOffset` aceita LINE | OK (`brush_footprint.cpp:60`) |
| `view.floor` em preview vs `mouse_map_pos.z` em release | Consistente (ambos representam floor atual) |

## Positive Notes

- **Reuso da infraestrutura existente**: a chamada unica `editor.draw(tilestodraw, tilestoborder, alt)` cria automaticamente um `BatchAction`, garantindo single-undo sem codigo adicional.
- **Deduplicacao bem implementada**: usar `unordered_set<int64_t>` evita o trabalho de `editor.draw` processar tiles duplicados nas sobreposicoes de footprint diagonal.
- **Comentario Phase 1 inline**: documentacao em `drawing_controller.cpp:307-309` deixa claro o escopo limitado de Fase 1.
- **Compatibilidade Lua mantida**: getter/setter de `app.brushShape` foram estendidos preservando comportamento existente; nada quebra para scripts que so usam "circle"/"square".
- **`canDrag()` ja satisfeito**: Wall/Ground/RAW retornam true, entao `ASSERT(brush->canDrag())` em `BrushOverlayDrawer::draw` continua valido.
- **Codigo defensivo**: o ramo LINE em `HandleRelease` valida explicitamente `WallBrush || GroundBrush || RAWBrush` antes de aplicar — outros brushes (carpet, table, doodad, spawn) continuam funcionando pela logica original.
- **Footprint compartilhado entre release e preview**: `GetLineTiles` e chamado dos dois lados (release e overlay), garantindo que o WYSIWYG funcione corretamente.

## Recomendacoes

1. **Aplicar o fix do shift de signed (MEDIUM)** antes do merge para evitar UB tecnico — patch e mecanico e nao altera comportamento observavel.
2. **Criar asset de icone proprio** em fase de polimento (issue baixa, ja com TODO no codigo).
3. **Testar manualmente** conforme passos no plano (`.redux/plans/03_Line_Tool.md` secao "Testing Notes"):
   - Linha horizontal/vertical/diagonal com Wall (size 0 e size > 0)
   - Linha com Ground (size 2 — confirma "estrada" 5 tiles de largura)
   - Linha com RAW (sprite por tile)
   - Single undo (Ctrl+Z desfaz toda a linha)
   - Ctrl+Shift+drag = undraw
   - Trocar para Square/Circle e confirmar que comportamento existente nao mudou
   - Lua: `app.brushShape = "line"` e `print(app.brushShape)`

## Notas adicionais

- Outras mudancas no `git status` (`CMakeLists.txt`, `gui_ids.h:MAP_PROPERTIES_COPY_FROM_MAP`, `cemetery.cpp`, `merge_maps_minimap_window.*`) sao de outras features em desenvolvimento e nao fazem parte deste plano — nao afetam a revisao do Line Tool.
- A duplicacao `#include "brushes/waypoint/waypoint_brush.h"` em `brush_overlay_drawer.cpp:42,47` e pre-existente — nao introduzida por este plano. Pode ser limpa em refactor separado.
