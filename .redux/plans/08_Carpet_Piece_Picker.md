# Plan: Seletor de peças de borda (Alt+RMB) + faixa de peças no palette

> **Status (2026-10-17):** implementado (T1–T7), aguarda build/teste do usuário.
> Diferenças em relação ao texto abaixo:
> - A faixa usa um registro estático (`CarpetPieceStrip::SyncAll` / `SetAllVisible`) em vez de procurar o painel do Terrain via `PaletteWindow`.
> - `InvalidateContents` chama `strip->Reload()`, que relê o brush na mão via `CallAfter`.
> - O seletor se fecha sozinho (`CheckStillValid`) quando o brush na mão deixa de ser daquele carpet. Isso protege contra reload de brushes com o seletor aberto.
> - Pedido do usuário depois do 1º teste: escolher uma **peça** no seletor também a **coloca no tile onde o seletor abriu** (`MapCanvas::ConfirmPiecePicker`). O efeito é o de um clique normal com a peça: um tile, um undo, sem Alt. A célula AUTO e a faixa só selecionam.
> - Segundo pedido: a faixa saiu de dentro do `BrushPalettePanel` e virou um **painel AUI próprio** (sem caption), encaixado entre o palette e o mapa (`CarpetPieceStrip::DockPane`). A T4 abaixo descreve a versão antiga, dentro do palette.
> - Terceiro pedido: o seletor **apaga as peças que não continuam as vizinhas**. As peças apagadas seguem clicáveis. O teste casa as bordas dos sprites entre o tile clicado e as peças do mesmo carpet nos 4 vizinhos ortogonais (`CarpetPieces::ReadNeighbourEdges` / `FitsNeighbours`).

## Overview

Com um **carpet brush** na mão (todas as "tiny borders" são carpet brushes), o mapper segura **Alt + botão direito** no mapa e aparece, em volta do cursor, um seletor com **todas as peças do brush arrumadas pela direção** (N em cima, S embaixo, cantos nos cantos, diagonais por fora). Soltar em cima de uma peça (ou clicar nela) deixa **só aquela peça** na mão como RAW, **sem trocar a aba nem o tileset do palette**: o Terrain continua em "Nature - Tiny Borders" com o brush destacado.

No mesmo trabalho entra uma **faixa vertical** à direita do grid do Terrain, com o brush automático no topo e todas as peças abaixo. Clicar numa peça faz o mesmo que o seletor; o destaque acompanha a peça que está na mão.

Referência: o vídeo e os prints do usuário (editor "Tibia Map Editor"), em que o seletor aparece num 5x5 em volta do tile e a faixa fica colada no palette.

## User Request

> Quando você seleciona uma borda que possui brush (tiny border) e segura um modificador + clique no chão, aparecem as opções da borda, todas as direções. Clico na pole e ela é selecionada "como RAW", mas o palette continua ali no Tiny Borders. Na próxima eu quero uma vertical: mesma coisa. E no palette, uma barra vertical mostrando todas as bordas daquele brush.

Decisões já tomadas (não reabrir):
1. **Gatilho padrão = Alt + botão direito** (o botão de "properties"; com `SWITCH_MOUSEBUTTONS` ele segue esse botão). Alt+LMB **não** muda: continua sendo o ground replace e, com peça RAW, o "empilhar borda" (`RAW_LIKE_SIMONE`, ligado por padrão). O modificador fica configurável em Preferences > Hotkeys > Drawing Modifiers, com a opção "Disabled".
2. **Faixa = controle wx dentro do `BrushPalettePanel` do Terrain**, à direita do choicebook. Não é overlay no canvas. Liga e desliga pelo menu View.
3. **Escopo v1 = só `CarpetBrush`.** Ground brush (borda da variante ativa) e wall brush (16 segmentos + portas) ficam para depois. O desenho do serviço deve deixar fácil plugar esses dois.

## Analysis

- **New Files**: Yes
  - `source/rendering/ui/carpet_piece_service.h` / `.cpp`: modelo + seleção (resolve o carpet de origem, lista as peças, seleciona peça/brush, descreve a peça para status e tooltip)
  - `source/rendering/ui/carpet_piece_picker.h` / `.cpp`: o seletor no canvas (NanoVG, no molde do `RadialWheel`)
  - `source/palette/controls/carpet_piece_strip.h` / `.cpp`: a faixa (subclasse de `VirtualItemGrid`)
- **Modified Files**: Yes
  - `source/rendering/ui/map_display.h` / `.cpp`: membro do picker, hooks de mouse/roda/overlay, helper genérico de modificador
  - `source/rendering/ui/keyboard_handler.cpp`: ESC fecha o picker, demais teclas bloqueadas enquanto ele estiver aberto
  - `source/palette/panels/brush_palette_panel.h` / `.cpp`: insere a faixa (só no Terrain), invalida no reload, mostra e esconde
  - `source/palette/controls/virtual_brush_grid.cpp`: clicar de novo no brush destacado volta ao brush automático
  - `source/ui/gui.cpp`: `emitBrushChangeIfNeeded` avisa a faixa
  - `source/app/settings.h` / `.cpp`: `PIECE_PICKER_MODIFIER`, `PALETTE_PIECE_STRIP`
  - `source/app/preferences/hotkeys_page.h` / `.cpp`: linha "Border Piece Picker Modifier"
  - `data/menubar.xml`, `source/ui/main_menubar.h` / `.cpp`, `source/ui/menubar/menubar_action_manager.cpp`, `source/ui/menubar/view_settings_handler.h` / `.cpp`: toggle View > "Border Piece Strip"
  - `source/ui/tips_window.cpp`: linha do atalho
  - `source/CMakeLists.txt`
- **CMakeLists Update**: Yes, 3 `.h` + 3 `.cpp`
- **Menu/Toolbar Wiring**: Yes, um item check no menu View
- **Action System**: No. Selecionar peça não mexe no mapa; a pintura da peça usa o `RAWBrush` existente (undo normal).
- **Data Files (XML/TOML)**: só `data/menubar.xml`

## Contexto de código verificado

| Item | Local |
|---|---|
| Tiny borders são `type="carpet"` (ex.: `grass (tiny)`, 12 `align`, sem `center`) | `data/1098/doodads.xml:2009-2022`; tileset `data/1098/tilesets.xml:1344+` |
| Item de carpet pertence a **um só** brush (o loader recusa item que já tem dono) e grava `editorData.brush` | `carpet_brush_loader.cpp:85-91, 108-114` |
| `CarpetBrush::getItems().getGroups()` = `std::array<CarpetGroup,14>` indexado por `BorderType` (13 = `CARPET_CENTER`); `CarpetItem{chance,id}`; `getLookID()` | `carpet_brush.h:56-71`, `carpet_brush_items.h:30-42` |
| `BorderType` 1..12 + `CARPET_CENTER=13` | `brush_enums.h:46-62` |
| Nomes prontos: `carpetAlignToString` ("n", "cse"), `carpetAlignLabel` ("South edge", "Northwest diagonal (inner corner)"), `CARPET_ALIGN_ORDER` | `ui/dialogs/carpet_editor_panel.h:61-65`, `.cpp:43-98` |
| RAW de um item: `g_item_definitions.editorData(id).raw_brush` (criado para todo item em `createOtherTileset`; ainda assim checar null) | `game/materials.cpp:234-288` |
| `g_gui.SelectBrush(b, pt)` → `PaletteWindow::OnSelectBrush` **troca a página** (RAW iria para a aba RAW) | `gui.cpp:365`, `brush_manager.cpp:86-100`, `palette_window.cpp:328-416` |
| `g_gui.SelectBrushInternal(b)` **não mexe no palette**: seta brush, status "Selected brush: …", `SetDrawingMode`, tool options, evento Lua | `gui.cpp:396-402`, `brush_manager.cpp:102-129` |
| Hook global de troca de brush (roda em `SelectBrush` e `SelectBrushInternal`) | `gui.cpp:79-95` (`emitBrushChangeIfNeeded`) |
| Grid do palette só reseleciona se `index != selected_index` | `virtual_brush_grid.cpp:769-785` |
| Construtor do `BrushPalettePanel` (choicebook na linha 73), `InvalidateContents`, `SelectBrush` | `brush_palette_panel.cpp:57-153, 159-167, 274-316` |
| Loop por todos os palettes | `brush_palette_panel.cpp:457-459` (`g_palettes.GetPalettes()`) |
| `VirtualItemGrid` (NanoVG, `GetItemCount/GetItem/GetItemName`, `OnItemSelected`, tooltip, scroll, cache de textura). Exemplo de subclasse: `BorderScanGrid` | `util/virtual_item_grid.h:13-67`, `ui/dialogs/border_scan_dialog.h:45-71` |
| `RadialWheel` (molde: Open/Close/Confirm/UpdateMouse/Draw em NanoVG) | `rendering/ui/radial_wheel.h:38-98` |
| Hooks do radial no canvas: move 661-666, LMB 819-824, overlay 407-410; teclado | `map_display.cpp`, `keyboard_handler.cpp:39-46` |
| RMB: `OnMousePropertiesClick` (troca para selection mode na 982-984) e `OnMousePropertiesRelease` (abre o menu de contexto na 1012-1013) | `map_display.cpp:972-1024` |
| Roda do mouse | `map_display.cpp:1026-1046` |
| Parser de modificador "Ctrl+Alt" (hoje preso a `SMART_BRUSH_MODIFIER`) | `map_display.cpp:99-128` |
| Linhas de "Drawing Modifiers" (choice + texto explicativo) e `Apply()` | `hotkeys_page.cpp:93-163, 254-299` |
| Sprite NanoVG de item: `NvgUtils::CreateItemTexture(vg, id)` (fundo = `ICON_BACKGROUND`, igual ao palette; o chamador faz o cache e o `nvgDeleteImage`) | `util/nvg_utils.h:109-123` |
| Padrão de cache por contexto NanoVG (limpa ao trocar `vg`) | `tooltip_drawer.cpp:117-136` |

---

## Tasks

### T1: Serviço de peças (`carpet_piece_service`)

Namespace `CarpetPieces`, sem estado próprio. O carpet de origem sai sempre do brush atual; como o item de carpet tem dono único, não precisa guardar nada.

```cpp
struct CarpetPiece {
	BorderType align;   // 1..12 ou CARPET_CENTER
	uint16_t itemId;
	int alternate;      // índice dentro do grupo (0 = primeiro)
};

// CarpetBrush direto; RAWBrush cujo editorData(id).brush é CarpetBrush → esse carpet; senão nullptr.
CarpetBrush* ResolveSource(Brush* current);

// Ordem: CARPET_ALIGN_ORDER (n, e, s, w, cnw, cne, cse, csw, dnw, dne, dse, dsw) e depois center.
// Todos os itens de cada grupo (alternativas inclusas).
std::vector<CarpetPiece> ListPieces(const CarpetBrush& carpet);

uint16_t FirstPiece(const CarpetBrush& carpet, BorderType align); // 0 se o grupo estiver vazio

// raw_brush do item → g_gui.SelectBrushInternal(raw) (palette intocado) → status = Describe(...).
// Retorna false se o item não tiver RAW brush.
bool SelectPiece(const CarpetBrush& carpet, const CarpetPiece& piece);

// Volta ao brush automático: g_gui.SelectBrush(carpet, TILESET_TERRAIN) (re-sincroniza o palette).
void SelectSource(CarpetBrush* carpet);

// "grass (tiny): 4544 — South edge (s)" / "grass (tiny) — auto brush"
wxString Describe(const CarpetBrush& carpet, const CarpetPiece* piece);

// Peça que está na mão (RAW cujo item é do carpet), ou nullopt.
std::optional<CarpetPiece> CurrentPiece(const CarpetBrush& carpet);
```

- Nomes vêm de `carpetAlignLabel` / `carpetAlignToString` (incluir `ui/dialogs/carpet_editor_panel.h` só no `.cpp`).
- O status vai para o campo 0, o mesmo que `SelectBrushInternal` usa (`g_status.SetStatusText`), sobrescrevendo o "Selected brush: …".
- Tamanho do brush: **não mexer**. A peça herda o tamanho atual (o `SelectBrushInternal` não restaura tamanho de palette).

### T2: Seletor no canvas (`CarpetPiecePicker`)

Classe nova no molde do `RadialWheel` (sem herdar dele).

**Layout 5x5** em volta do ponto clicado. Diagonais (cantos internos) ficam por fora, como na referência:

```
 dnw   .    .    .   dne
  .   cnw   n   cne   .
  .    w  [AUTO]  e   .
  .   csw   s   cse   .
 dsw   .    .    .   dse
```

- O centro mostra o sprite do `look_id` do carpet com uma etiqueta "AUTO" pequena. Escolher o centro chama `SelectSource` e volta ao brush automático.
- Direção sem item: célula vazia, tracejada e esmaecida, sem clique.
- Grupo com várias alternativas: mostra a primeira e um badge "+N" no canto. (Trocar de alternativa no seletor fica para depois; na faixa todas aparecem.)
- A peça `CARPET_CENTER`, se existir (carpets "de verdade"), não tem lugar no 5x5. Na v1 ela só aparece na faixa.

**Tamanho e posição**
- Célula = `clamp(TILE_SIZE / zoom, 32, 56)` px lógicos (NanoVG trabalha em pixel lógico, ver `TextRenderer::BeginFrame` em `map_display.cpp:308`).
- O bloco é centrado no cursor e empurrado para dentro do canvas com margem de 8 px.
- Embaixo do bloco vai um rótulo com `Describe(...)` da célula sob o cursor.

**Visual (NanoVG)**
- Fundo escuro translúcido arredondado atrás do bloco.
- Sprites via `NvgUtils::CreateItemTexture`, com cache `std::unordered_map<uint16_t,int>` que é limpo (com `nvgDeleteImage`) ao trocar de `NVGcontext`, como em `tooltip_drawer.cpp:122-136`.
- Hover: contorno branco de 2 px. Peça que já está na mão: contorno amarelo.

**API**

```cpp
bool Open(CarpetBrush* carpet, int mouse_x, int mouse_y, int canvas_w, int canvas_h, double zoom);
void Close();
bool IsOpen() const;
void UpdateMouse(int x, int y);          // hover + marca "moved" se passou de 6 px do ponto de abertura
bool Confirm();                          // seleciona a célula sob o cursor; false se não havia célula
bool HasHoveredCell() const;
bool HasMovedSinceOpen() const;
wxString HoveredDescription() const;
void Draw(NVGcontext* vg, int canvas_w, int canvas_h);
```

O picker guarda o `CarpetBrush*` só enquanto está aberto e fecha em qualquer reload de brushes (ver T4).

### T3: Ligações no `MapCanvas`

1. **Helper de modificador.** Extrair o parser de `IsSmartBrushModifier` (`map_display.cpp:102-128`) para `static bool MatchesModifierSetting(const std::string& mod, bool shift, bool ctrl, bool alt)`. `IsSmartBrushModifier` passa a chamar esse helper (mesmo comportamento) e entra `IsPiecePickerModifier(...)`, que lê `Config::PIECE_PICKER_MODIFIER`. O valor "Disabled" retorna `false`.
2. **Membro.** `std::unique_ptr<CarpetPiecePicker> piece_picker;` em `map_display.h`, ao lado de `radial_wheel` (linha 220), criado junto com ele (cpp:200). Também `bool swallow_left_release = false;`.
3. **`OnMousePropertiesClick`**, no topo, antes de `SetSelectionMode`:
   - Picker aberto → `Close()`, `Refresh()`, `return` (RMB fora fecha).
   - `IsPiecePickerModifier(...)` e `CarpetPieces::ResolveSource(g_gui.GetCurrentBrush())` não nulo:
     - fecha o radial se estiver aberto;
     - `piece_picker->Open(...)`;
     - marca `picker_opened_by_press = true`;
     - `Refresh()` e `return`.
   - Senão segue como hoje.
4. **`OnMousePropertiesRelease`**, no topo:
   - Se o picker está aberto e foi aberto por esse press:
     - **Arrastou** (`HasMovedSinceOpen`): se há célula sob o cursor → `Confirm()`; se não há → `Close()`.
     - **Não arrastou** (toque rápido): o picker **fica aberto** em "modo clique".
     - Em qualquer caso: zera a flag, `Refresh()` e `return`. O menu de contexto não abre e o modo não muda para seleção.
5. **`OnMouseMove`**, no topo, ao lado do radial (661-666): picker aberto →
   - `UpdateMouse`;
   - `g_gui.SetStatusText(piece_picker->HoveredDescription())`;
   - `Refresh()` e `return`.
6. **`OnMouseActionClick`**, ao lado do radial (819-824): picker aberto →
   - `Confirm()` se há célula, senão `Close()`;
   - `swallow_left_release = true`;
   - `Refresh()` e `return`.
7. **`OnMouseActionRelease`**, no topo: se `swallow_left_release` → zera e `return`. Assim o release do clique que escolheu a peça não chega ao `DrawingController` (evita aplicar arrasto Shift, etc.).
8. **`OnWheel`**: picker aberto → `return` (sem zoom nem mudança de tamanho enquanto escolhe).
9. **`DrawOverlays`**, no bloco HUD (392-411), depois do `BorderVariantHUD` e antes do radial: `if (piece_picker && piece_picker->IsOpen() && !screenshot_controller->IsCapturing()) piece_picker->Draw(...)`.
10. **`KeyboardHandler::OnKeyDown`** (`keyboard_handler.cpp:39-46`): bloco igual ao do radial. ESC fecha e as outras teclas são bloqueadas.
11. **Exclusão mútua:** abrir o radial (`MainMenuBar::OnRadialWheel`, `main_menubar.cpp:849-860`) fecha o picker, e vice-versa.

### T4: Faixa no palette (`CarpetPieceStrip`)

Subclasse de `VirtualItemGrid` em `source/palette/controls/carpet_piece_strip.*`.

- **Itens:**
  - índice 0 = o carpet (sprite do `look_id`, clique = `SelectSource`);
  - depois, `ListPieces(carpet)`, com todas as alternativas.
- **Uma coluna.** `m_itemSize` = `Config::PALETTE_GRID_ICON_SIZE` (padrão 36). Largura fixa = slot + padding + scrollbar, via `SetMinSize` / `DoGetBestClientSize`. A rolagem vertical já vem do `VirtualItemGrid`.
- **`OnNanoVGPaint` próprio:**
  - só ícones (sem nome embaixo);
  - código curto da direção ("n", "cse") no canto do slot, como no carpet editor (`carpet_editor_panel.cpp:709`);
  - separador fino entre os grupos (auto | lados | cantos | diagonais | center);
  - slot da peça na mão com contorno de seleção.
- **Tooltip:** `GetItemName` → `Describe(...)`.
- **`OnItemSelected(i)`:** `SelectSource` ou `SelectPiece`.
- **`SyncWithCurrentBrush()`:**
  - `ResolveSource(g_gui.GetCurrentBrush())`. Se o carpet mudou, refaz a lista; senão só atualiza a seleção.
  - Sem carpet (ground brush, doodad etc.): estado vazio com um texto esmaecido "—". A largura fica a mesma para o grid não pular.
- **`ClearSource()`** zera o ponteiro do carpet (usado no reload de brushes).

**No `BrushPalettePanel`:**
- No construtor, só quando `category == TILESET_TERRAIN`, a linha 73 (`ts_sizer->Add(tmp_choicebook, 1, wxEXPAND)`) vira um `wxBoxSizer(wxHORIZONTAL)` com `[choicebook (1, wxEXPAND) | strip (0, wxEXPAND | wxLEFT, 2)]`. O pai da faixa é `ts_parent`.
- Visibilidade inicial = `Config::PALETTE_PIECE_STRIP`. Método `SetPieceStripVisible(bool)` com `Show` + `Layout()`.
- `InvalidateContents()` chama `strip->ClearSource()` (os ponteiros de brush morrem no reload).
- Método estático `BrushPalettePanel::SyncPieceStrips()`: percorre `g_palettes.GetPalettes()`, acha o painel do Terrain (seguir o acesso existente, ex.: `GetCameraPathPalette`, para criar um `GetTerrainPalette` se faltar) e chama `SyncWithCurrentBrush()`.

**Avisar a faixa:** em `gui.cpp:79-95` (`emitBrushChangeIfNeeded`), dentro do `if` de troca, chamar `BrushPalettePanel::SyncPieceStrips()`. Esse caminho cobre o picker, a faixa, o grid, o Q e o smart brush.

**Fechar o picker no reload:** no mesmo ponto em que a faixa é invalidada (reload de brushes), fechar o picker de todos os canvas. Na prática, `ResolveSource` + `IsOpen` no próximo evento já protege. O executor deve conferir se há um hook de "brushes recarregados" (ex.: o publish do Brushes Editor) e fechar ali também.

### T5: Grid do palette volta ao brush automático

Em `virtual_brush_grid.cpp:769`, trocar a condição para
`if (index != selected_index || GetEffectiveBrush(index) != g_gui.GetCurrentBrush())`,
mantendo o resto do bloco. Com uma peça na mão, clicar de novo no "grass (tiny)" destacado volta ao brush automático. O modo reorder e o arrasto não podem ser afetados: a captura de mouse para reorder acontece antes e não depende disso.

### T6: Settings, Hotkeys, menu View, dicas

- **`settings.h` / `.cpp`:**
  - `PIECE_PICKER_MODIFIER` → `String(..., "Alt")`, ao lado de `SMART_BRUSH_MODIFIER` (`settings.cpp:420`);
  - `PALETTE_PIECE_STRIP` → `Bool(..., true)`, ao lado de `PALETTE_TILESET_SHOW_LABELS` (`settings.h:247-249`, `settings.cpp:510-513`).
- **`hotkeys_page`:** nova linha depois da Smart Brush (depois da 163):
  - rótulo "Border Piece Picker Modifier:";
  - opções `Alt`, `Ctrl+Alt`, `Alt+Shift`, `Ctrl+Shift`, `Disabled`;
  - texto "(Hold + Right Click on the map with a carpet brush to pick a single piece)";
  - salvar em `Apply()`; membro no `.h`.
- **Menu View:** item check "Border Piece Strip" seguindo o roteiro do Carpet Fill Borders:
  - `data/menubar.xml` (View, linhas 166-184);
  - `ActionID` em `main_menubar.h`;
  - forward em `main_menubar.cpp`;
  - `MAKE_ACTION(..., wxITEM_CHECK, ...)` em `menubar_action_manager.cpp`;
  - handler em `view_settings_handler`: grava a setting e chama `SetPieceStripVisible` em todos os palettes;
  - estado inicial em `LoadValues()`.
- **`tips_window.cpp:173`:** "Alt + Right Click (carpet brush): pick a single border piece".

### T7: CMakeLists

Adicionar os 3 `.h` e os 3 `.cpp` novos nas listas explícitas de `source/CMakeLists.txt`, junto dos vizinhos de pasta (`rendering/ui/radial_wheel.*`, `palette/controls/virtual_brush_grid.*`).

---

## Fora de escopo (v2)

- **Ground brush:** peças da borda externa da variante ativa (`getActiveOuterAutoBorder`, que respeita o F3) + itens de ground. O serviço vira uma interface "PieceSource" com implementações Carpet/Ground.
- **Wall brush:** 16 segmentos + portas/janelas no layout em cruz do Wall editor.
- Trocar de alternativa (roda do mouse sobre a célula) dentro do seletor.
- Fazer o Q voltar para o brush automático: hoje ele alterna entre as duas últimas peças, o que é útil (horizontal ↔ pole), e fica como está.

## Riscos / pontos de atenção

- **Alt no Windows:** soltar o Alt pode ativar a barra de menu. O Alt já é usado no canvas (ground replace, Alt+roda) sem queixa, e `KeyboardHandler::OnKeyUp` não chama `Skip()`. Mesmo assim, testar Alt+RMB → soltar Alt.
- **Release perdido:** se o botão for solto fora do canvas, o release não chega e o picker fica em "modo clique". É aceitável; ESC, clique fora e RMB fecham.
- **Refresh de palette:** troca de aba de mapa ou load de mapa chama `RefreshPalettes`, que reseleciona o brush do palette. Uma peça na mão volta a ser o brush automático nesses casos. É comportamento esperado e não precisa de correção.
- **Radial wheel:** tem o mesmo problema de release do LMB não interceptado (`OnMouseActionRelease`). O `swallow_left_release` da T3 pode ser usado pelo radial também, mas só se for trivial.

## Teste manual (o usuário compila)

1. Terrain > "Nature - Tiny Borders" > `grass (tiny)`. Alt+RMB no mapa: o seletor abre no cursor com as 12 peças no 5x5, o menu de contexto **não** abre e o modo continua em desenho.
2. Segurar, arrastar até "s" e soltar: a 4544 fica na mão. O palette continua no mesmo tileset com `grass (tiny)` destacado, e o status mostra "grass (tiny): 4544 — South edge (s)".
3. Com a peça na mão, Alt+RMB de novo: abre o seletor do mesmo carpet, com a peça atual em contorno amarelo.
4. Toque rápido Alt+RMB: o seletor fica aberto. LMB numa célula escolhe a peça sem pintar nada. LMB fora, ESC ou RMB fecham sem pintar.
5. Célula central: volta ao `grass (tiny)` automático.
6. **Regressão:** Alt+LMB com a peça na mão continua empilhando a borda; Alt+LMB com ground brush continua fazendo ground replace.
7. Alt+RMB com ground brush, doodad ou sem brush: abre o menu de contexto como antes.
8. Preferences > Hotkeys > "Border Piece Picker Modifier" = Disabled: Alt+RMB volta ao menu de contexto. Com `SWITCH_MOUSEBUTTONS`, o gatilho segue o botão de properties.
9. **Faixa:**
   - aparece à direita do grid do Terrain com AUTO + 12 peças;
   - o hover mostra o tooltip;
   - o clique seleciona sem trocar a aba;
   - o destaque segue a peça escolhida pelo seletor;
   - fica vazia ("—") com ground brush;
   - View > "Border Piece Strip" esconde e mostra.
10. Com a peça na mão, clicar de novo no `grass (tiny)` destacado no grid: volta ao brush automático.
11. Publicar no Brushes Editor (reload de brushes) com a faixa visível e o seletor aberto: não pode crashar.
12. Q alterna entre as duas últimas peças, como hoje.
