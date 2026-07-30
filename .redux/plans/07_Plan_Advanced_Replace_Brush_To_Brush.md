# Plan: Advanced Replace Tool — Brush→Brush Replacement + Undo

## Overview

Extend the Advanced Replace Tool (`source/ui/replace_tool/`) so a rule can replace an entire **brush** with another brush, mapping each map item to the item playing the **same role** in the destination brush (ground center, border direction, wall segment, carpet alignment). In the same pass, make the whole Advanced Replace execution **undoable** by routing it through `editor->actionQueue` instead of mutating tiles in place.

## User Request

Adicionar substituição brush→brush no Advanced Replace Tool. Hoje as regras são item→item (`ReplacementRule.fromId` → `targets[].id`). Quero regras que troquem um brush inteiro por outro, mapeando cada item para o item equivalente no brush destino.

Decisões já tomadas (não reabrir):
1. Estratégia = **equivalência de papel**, NÃO repintar com `doBorders`. GroundBrush (ground → `getRandomGroundItemId()`; borda → mesma direção 0-12 no AutoBorder correspondente via `g_brushes.findAutoBordersByBorderItem` + `AutoBorder::getRandomTileId`), WallBrush (por segmento, preservando portas/janelas), CarpetBrush (alinhamento por direção). **Sem Table nem Doodad nesta fase.**
2. UI = cada slot do card ganha modo "brush"; clique abre picker filtrável modelado em `RebuildGeneralGroundBrushList`/`RebuildWallBrushList` (`dungeon_preset_editor_dialog.cpp:987-1043`).
3. Tornar o Advanced Replace undoável via `actionQueue` (`createAction` + `TileOperations::deepCopy` + `Change`), como `ReplaceItemsDialog::OnExecuteButtonClicked` (`replace_items_window.cpp:378-437`).

## Analysis

- **New Files**: Yes
  - `source/ui/replace_tool/brush_mapping_service.h` / `.cpp` — serviço de equivalência de papel brush→brush
  - `source/ui/replace_tool/brush_picker_dialog.h` / `.cpp` — diálogo picker filtrável de brushes
- **Modified Files**: Yes
  - `source/ui/replace_tool/rule_manager.h` / `.cpp` — modelo de regra (item vs brush) + persistência JSON retrocompatível
  - `source/ui/replace_tool/replacement_engine.h` / `.cpp` — avaliação de regras de brush + refactor de undo
  - `source/ui/replace_tool/rule_builder_panel.h` / `.cpp` — hit-test, clique no slot, modo brush
  - `source/ui/replace_tool/rule_card_renderer.h` / `.cpp` — desenho dos slots em modo brush
  - `source/ui/replace_tool/replace_tool_window.cpp` — validação de regras completas (auto-add) + **fix de erro de sintaxe pré-existente**
  - `source/CMakeLists.txt`
- **CMakeLists Update**: Yes — 2 novos `.h` (após linha 393) e 2 novos `.cpp` (após linha 821)
- **Menu/Toolbar Wiring**: No — a janela já existe e é aberta pelos caminhos atuais
- **Action System**: **Yes** — `ExecuteReplacement` passa a construir `BatchAction(ACTION_REPLACE_ITEMS)` com `TileOperations::deepCopy` + `Change`
- **Data Files (XML/TOML)**: No

## Contexto de código verificado

| Item | Local |
|---|---|
| `ReplacementRule` (`fromId`, `targets[]`, `offsetX/Y`), `TRASH_ITEM_ID=0xFFFF` | `rule_manager.h:8-28` |
| `to_json`/`from_json` (offsets já opcionais via `j.value(...)`) | `rule_manager.cpp:10-36` |
| Motor: `ruleMap` keyed por `fromId`, `PendingMove`, drain de offset | `replacement_engine.cpp:54-59, 65-71, 94-109, 165-195` |
| `HitResult` enum | `rule_builder_panel.h:25-41` |
| Hit-test do slot Source / badge de offset / targets | `rule_builder_panel.cpp:436-483` |
| `OnMouse` (LeftDown → EditOffset, Source, AddTarget…) | `rule_builder_panel.cpp:327-394` |
| `ShowOffsetDialog` (modelo de modal local) | `rule_builder_panel.cpp:36-63` |
| `DrawRuleCard` / badge de offset / `DrawRuleItemCard` | `rule_card_renderer.cpp:88-224, 138-163, 279-359` |
| Padrão do picker (itera `g_brushes.getMap()`, `as<GroundBrush>()`, `MakeItemBitmap`) | `dungeon_preset_editor_dialog.cpp:987-1041` |
| Padrão de undo (`createAction` + `deepCopy` + `transformItem` + `addChange`) | `replace_items_window.cpp:414-426` |
| `g_brushes.getMap()`, `findAutoBordersByBorderItem(id, hint)`, `getBorders()` | `brush.h:90-100` |
| `AutoBorder::tiles` = `std::array<std::vector<BorderItemChance>,13>`, `getRandomTileId(dir)`, `containsItemInDirection` | `auto_border.h:72-94` |
| `GroundBrush::getRandomGroundItemId()`, `border_items`, `borders[]` (`BorderBlock.owned_autoborder`/`autoborder`, `outer`, `to`) | `ground_brush.h:88, 144-181` |
| `Item::getGroundBrush/getCarpetBrush/getDoorBrush` | `item.cpp:308-345` |
| `WallBrush::items` (público), `hasWall(Item*)`, `getDoorTypeFromID(id)` | `wall_brush.h:36-52` |
| `WallBrushItems`: `getRandomWallId(align)`, `hasWall(id,align)`, `hasDoor(id,align)`, `getDoorItems(align)` (`{type,id,locked}`), 17 alinhamentos | `wall_brush_items.h:35-63` |
| `CarpetBrushItems`: `getGroups()` = `array<CarpetGroup,14>`, `getRandomItem(BorderType)` | `carpet_brush_items.h:21-48` |
| `CarpetBrush::m_items` é **protected** (getter público ausente) | `carpet_brush.h:70` |
| BorderType 0-12 + `CARPET_CENTER=13`; `WALL_POLE=0 … WALL_UNTOUCHABLE=16`; `enum DoorType` | `brush_enums.h:46-112` |

---

## Tasks

### Phase 1: Modelo de dados e acesso a brushes

#### Task 1.1: Estender `ReplacementRule` com modo item/brush

- **Description**: Adicionar ao modelo a noção de slot "brush", mantendo 100% de retrocompat na persistência.
- **Files**: `source/ui/replace_tool/rule_manager.h`, `source/ui/replace_tool/rule_manager.cpp`
- **Details**:
  - Em `rule_manager.h`, antes de `ReplacementTarget`:
    ```cpp
    enum class SlotKind : uint8_t { Item = 0, Brush = 1 };
    ```
  - `ReplacementTarget` ganha:
    ```cpp
    SlotKind kind = SlotKind::Item;
    std::string brushName;   // válido quando kind == Brush
    ```
  - `ReplacementRule` ganha:
    ```cpp
    SlotKind fromKind = SlotKind::Item;
    std::string fromBrushName; // válido quando fromKind == Brush
    ```
  - Helpers inline em `ReplacementRule`:
    ```cpp
    bool isBrushRule() const { return fromKind == SlotKind::Brush; }
    bool hasSource() const { return isBrushRule() ? !fromBrushName.empty() : fromId != 0; }
    ```
  - **Persistência (`rule_manager.cpp`)** — escrever os campos novos **sempre**, ler com `j.value(...)` (mesmo padrão já usado para `offsetX/offsetY` em `:26-27`):
    - `to_json(ReplacementTarget)`: acrescentar `{"kind", (int)t.kind}, {"brushName", t.brushName}`
    - `from_json(ReplacementTarget)`: `t.kind = (SlotKind)j.value("kind", 0); t.brushName = j.value("brushName", std::string());`
    - `to_json(ReplacementRule)`: acrescentar `{"fromKind", (int)r.fromKind}, {"fromBrushName", r.fromBrushName}`
    - `from_json(ReplacementRule)`: `r.fromKind = (SlotKind)j.value("fromKind", 0); r.fromBrushName = j.value("fromBrushName", std::string());`
  - **Retrocompat**: JSONs antigos não têm `kind`/`fromKind` → default `0` = `SlotKind::Item` → comportamento idêntico ao atual. `id`/`fromId` continuam obrigatórios (`j.at`) e são preenchidos com `0` em regras de brush, o que é válido.

#### Task 1.2: Expor os itens do CarpetBrush

- **Parallel**: Yes (pode rodar com Task 1.1)
- **Description**: `CarpetBrush::m_items` é `protected` e não há getter; o serviço de mapeamento precisa lê-lo.
- **Files**: `source/brushes/carpet/carpet_brush.h`
- **Details**:
  - Na seção `public:` (junto de `getName()`, ~linha 60), adicionar:
    ```cpp
    const CarpetBrushItems& getItems() const { return m_items; }
    ```
  - Alteração mínima e aditiva; nenhum comportamento existente muda.

---

### Phase 2: Serviço de mapeamento brush→brush

#### Task 2.1: Criar `BrushMappingService`

- **Depends On**: Task 1.1, Task 1.2
- **Description**: Serviço puro (sem UI, sem mutação de mapa) que, dado um item do mapa e um par (brush origem, brush destino), devolve o item de mesmo papel no destino.
- **Files**: `source/ui/replace_tool/brush_mapping_service.h` (novo), `source/ui/replace_tool/brush_mapping_service.cpp` (novo)
- **Details**:
  - **Header** — API pública:
    ```cpp
    #ifndef RME_BRUSH_MAPPING_SERVICE_H_
    #define RME_BRUSH_MAPPING_SERVICE_H_

    #include <cstdint>
    #include <string>

    class Brush;
    class Item;

    class BrushMappingService {
    public:
        // Resultado da tentativa de mapear um item.
        struct MapResult {
            bool matched = false;   // o item pertence ao brush de origem?
            bool resolved = false;  // achamos um equivalente no destino?
            uint16_t newId = 0;
        };

        // Resolve o brush pelo nome no registry global; nullptr se ausente.
        static Brush* FindBrush(const std::string& name);

        // Os dois brushes são compatíveis para troca por papel?
        // (mesma família: Ground↔Ground, Wall↔Wall, Carpet↔Carpet)
        static bool AreCompatible(const Brush* from, const Brush* to);

        // Mapeia um item concreto do mapa. Não muta nada.
        static MapResult MapItem(const Item* item, Brush* fromBrush, Brush* toBrush);

        // Item representativo para preview/ícone do brush no card.
        static uint16_t GetPreviewItemId(const Brush* brush);
    };
    #endif
    ```
  - **`AreCompatible`**: `true` apenas se ambos `is<GroundBrush>()`, ou ambos `is<WallBrush>()` (inclui `WallDecorationBrush`, que herda de `WallBrush`), ou ambos `is<CarpetBrush>()`. Qualquer outra combinação → `false`. `TableBrush`/`DoodadBrush` → `false` (fora de escopo).
  - **`MapItem` — GroundBrush** (`fromBrush->as<GroundBrush>()`, `toBrush->as<GroundBrush>()`):
    1. **Ground center**: se `item->getGroundBrush() == fromGround` → `matched=true`, `newId = toGround->getRandomGroundItemId()`, `resolved = (newId != 0)`.
    2. **Border**: obter `id = item->getID()`. Descobrir a direção de origem:
       - Ler o hint de alinhamento do próprio item quando disponível (`ItemAttributeKey::BorderAlignment`, setado por `AutoBorder::load()` — ver `border-system.md`), senão `BORDER_NONE`.
       - `auto borders = g_brushes.findAutoBordersByBorderItem(id, hint);`
       - Para cada `const AutoBorder* ab` retornado, verificar se `ab` pertence ao **fromGround** (comparar contra os `AutoBorder*` alcançáveis a partir de `fromGround`); se sim, achar `dir` em `[1..12]` tal que `ab->containsItemInDirection(id, dir)`.
       - Como `GroundBrush::borders` é `protected` e `BorderBlock` é um tipo protegido, **não** acessá-lo diretamente. Usar os acessores públicos já existentes em `ground_brush.h:101-118`: `getFirstOuterAutoBorder()`, `getFirstInnerAutoBorder()`, `getFirstAutoBorder()`. Estratégia:
         - Construir a lista de candidatos do origem = `{getFirstOuterAutoBorder(), getFirstInnerAutoBorder()}` (descartando `nullptr` e duplicatas).
         - Se o `ab` casado for o **outer** do origem → usar o **outer** do destino (`toGround->getFirstOuterAutoBorder()`); se for o **inner** → usar o **inner** do destino. Fallback em cascata: se o correspondente do destino for `nullptr`, cair para `toGround->getFirstAutoBorder()`.
       - `newId = destBorder->getRandomTileId(dir)`; `resolved = (newId != 0)`.
       - Se `dir` não for encontrado ou nenhum `ab` pertencer ao origem → `matched=false` (item não é deste brush).
    3. Se nada casar → `MapResult{}` (tudo `false`).
  - **`MapItem` — WallBrush**:
    - `WallBrush* fromWall`, `toWall`. Percorrer `align` de `0` a `WallBrushItems::WALL_ALIGNMENT_COUNT - 1` (17):
      - **Parede simples**: se `fromWall->items.hasWall(id, align)` → `matched=true`; `newId = toWall->items.getRandomWallId(align)`.
      - **Porta/janela**: se `fromWall->items.hasDoor(id, align)` → `matched=true`; obter `DoorType dt = fromWall->items.getDoorTypeFromID(id)`; procurar em `toWall->items.getDoorItems(align)` a primeira entrada com `type == dt` (preferindo mesma flag `locked` quando existir; senão qualquer `type == dt`) → `newId = entry.id`. Isso **preserva portas e janelas** como pedido: uma porta trancada vira a porta trancada equivalente, uma janela vira janela.
      - Fallback quando o destino não tem aquele `DoorType` no mesmo `align`: usar `toWall->items.getRandomWallId(align)` (vira parede sólida) e ainda assim `resolved=true` — melhor que deixar um item órfão do brush antigo.
    - `resolved = (newId != 0)`. Primeiro `align` que casar vence (os alinhamentos são disjuntos por id na prática).
  - **`MapItem` — CarpetBrush**:
    - `fromCarpet->getItems().getGroups()` (getter da Task 1.2) é `array<CarpetGroup,14>`, indexado por `BorderType` (0-12 + `CARPET_CENTER=13`). Achar `dir` tal que algum `CarpetItem.id == id` no grupo `dir` → `matched=true`.
    - `newId = toCarpet->getItems().getRandomItem((BorderType)dir)` — usar a variante const dos grupos + `CarpetBrushItems::pickFromGroup` se `getRandomItem` não for const; caso não seja, chamar `pickFromGroup(toCarpet->getItems().getGroups()[dir])`, que **é** estático e público (`carpet_brush_items.h:45`).
    - Fallback: se o grupo destino estiver vazio, tentar `CARPET_CENTER`, depois `getGroups()[0]`; se ainda vazio, `resolved=false`.
  - **`GetPreviewItemId`**: `GroundBrush` → `getFirstGroundItemId()`; `WallBrush` → `items.getRandomWallId(WALL_HORIZONTAL)` com fallback `WALL_VERTICAL` e `WALL_POLE` (mesmo padrão de `dungeon_preset_editor_dialog.cpp:1027-1028`); `CarpetBrush` → `pickFromGroup(getItems().getGroups()[CARPET_CENTER])` com fallback varrendo os grupos; fallback final `brush->getLookID()`.
  - **Determinismo**: `getRandomGroundItemId` / `getRandomTileId` / `getRandomWallId` já randomizam internamente por peso — é o comportamento desejado (variação natural), coerente com o `ResolveReplacement` probabilístico já existente.
  - **Sem `doBorders`**: o serviço nunca chama `GroundBrush::doBorders`, `WallBrush::doWalls` nem `CarpetBrush::doCarpets`. Substituição é 1:1 por papel, preservando exatamente o layout existente.

---

### Phase 3: Motor — regras de brush + undo

#### Task 3.1: Refatorar `ExecuteReplacement` para usar o action system

- **Depends On**: Task 2.1
- **Description**: Substituir a mutação direta de tiles por `BatchAction`/`Action`/`Change` com cópias profundas, tornando o Advanced Replace undoável (Ctrl+Z).
- **Files**: `source/ui/replace_tool/replacement_engine.cpp`, `source/ui/replace_tool/replacement_engine.h`
- **Details**:
  - Includes novos: `editor/action.h`, `editor/action_queue.h`, `ui/replace_tool/brush_mapping_service.h`, `brushes/brush.h`.
  - **Reestruturação central**: hoje `tileProcessor` (`:77-142`) muta o `Tile*` vivo. Passa a operar sobre uma **cópia**:
    ```cpp
    auto batch = editor->actionQueue->createBatch(ACTION_REPLACE_ITEMS);
    auto action = editor->actionQueue->createAction(batch.get());
    // por tile visitado:
    std::unique_ptr<Tile> newTile = TileOperations::deepCopy(tile, editor->map);
    bool changed = /* aplicar regras sobre newTile->ground / newTile->items */;
    if (changed) {
        TileOperations::update(newTile.get());
        action->addChange(std::make_unique<Change>(std::move(newTile)));
    }
    // no fim:
    batch->addAndCommitAction(std::move(action));
    editor->addBatch(std::move(batch));
    ```
    Remover as chamadas `tile->modify()` sobre o tile vivo — o commit do `Change` faz o swap e marca sujo.
  - **`PendingMove` precisa mudar de forma**: hoje guarda `Tile* sourceTile` + `Item* sourceItem` (`:65-71`) e apaga do tile vivo em `:167-181`. Com cópias, os ponteiros de item da cópia não são estáveis entre fases. Substituir por:
    ```cpp
    struct PendingMove {
        Position sourcePos;
        int sourceIndex = -1;   // índice no vetor de items; -1 => ground
        Position targetPos;
        uint16_t newId = 0;     // TRASH_ITEM_ID => só remove
    };
    ```
    - **Fase A (scan)**: ao decidir relocar, gravar `sourcePos`/`sourceIndex` e **não** aplicar nada no tile copiado nessa passada.
    - **Fase B (aplicar remoções)**: agrupar `pendingMoves` por `sourcePos`; para cada posição, um único `deepCopy` do tile, remover por índice em **ordem decrescente** (para não invalidar índices), `TileOperations::update`, `addChange`.
    - **Fase C (aplicar criações)**: agrupar por `targetPos`; `Tile* dst = editor->map.getTile(pos)`; se `nullptr`, criar via `editor->map.getOrCreateTile(pos)` e então `deepCopy`; `dst->addItem(Item::Create(newId))` sobre a cópia; `addChange`.
    - **Cuidado com colisão de fases**: se uma posição é ao mesmo tempo source e target, ela precisa de **um único** `Change` combinado. Manter um `std::map<Position, std::unique_ptr<Tile>>` de tiles pendentes: `deepCopy` na primeira vez que a posição é tocada (por remoção **ou** criação), acumular todas as edições nela e emitir **um** `Change` por posição no final. Isso substitui o `std::set<Tile*> dirtyTiles` atual (`:71`, `:180`, `:187`, `:192-195`) e é obrigatório, já que dois `Change` para o mesmo tile no mesmo `Action` fariam o segundo sobrescrever o primeiro.
  - Ao final, substituir `editor->map.doChange(); g_gui.RefreshView();` (`:197-198`) por `editor->addBatch(std::move(batch));` seguido de `g_gui.RefreshView();` (o `addBatch` já dispara commit e marca o mapa como alterado).
  - Se o batch não acumulou nenhuma mudança, **não** chamar `addBatch` (evita entrada vazia no histórico) — `action->size() == 0` é o teste (`empty()` não existe; ver `action-system.md`).

#### Task 3.2: Avaliar regras de brush no motor

- **Depends On**: Task 3.1
- **Description**: `ruleMap` é `std::map<uint16_t, const ReplacementRule*>` keyed por `fromId` (`:54-59`) e não serve para regras de brush. Adicionar uma lista separada avaliada quando não houver match item→item.
- **Files**: `source/ui/replace_tool/replacement_engine.cpp`, `source/ui/replace_tool/replacement_engine.h`
- **Details**:
  - Na montagem inicial, separar:
    ```cpp
    std::map<uint16_t, const ReplacementRule*> ruleMap;      // regras de item
    struct BrushRule { const ReplacementRule* rule; Brush* from; Brush* to; };
    std::vector<BrushRule> brushRules;                        // regras de brush, pré-resolvidas
    ```
    Para cada regra com `isBrushRule()`: resolver `from = BrushMappingService::FindBrush(rule.fromBrushName)`. O destino vem do **primeiro target com `kind == SlotKind::Brush`** — na prática uma regra de brush tem exatamente um target de brush (ver Task 4.2, que impede múltiplos). Resolver `to = FindBrush(target.brushName)`. Descartar (com `wxLogWarning`) se `from`/`to` forem `nullptr` ou se `!AreCompatible(from, to)`. Pré-resolver **uma vez**, fora do loop de tiles — evita lookups por item.
  - Dentro do lambda `apply` (hoje `:83-109`), após o `ruleMap.find` falhar, em vez de `return false`:
    ```cpp
    for (const auto& br : brushRules) {
        auto res = BrushMappingService::MapItem(item, br.from, br.to);
        if (!res.matched) continue;
        if (!res.resolved) return false;   // pertence ao brush mas sem equivalente: preserva o item
        newId = res.newId;
        rule = br.rule;                    // para herdar offset
        goto haveReplacement;              // ou refatorar em função auxiliar
    }
    return false;
    ```
    Preferir extrair numa função auxiliar `bool ResolveForItem(const Item*, uint16_t& newId, const ReplacementRule*& outRule)` em vez de `goto`, mantendo o estilo do arquivo.
  - **Precedência**: regras de item→item têm prioridade sobre regras de brush (o `ruleMap.find` roda primeiro). Isso permite ao usuário fazer uma troca de brush inteira e ainda sobrescrever ids específicos com regras pontuais.
  - **Interação com offset**: o bloco de offset em `:94-104` já lê `rule->offsetX/offsetY` — como a regra de brush é a mesma struct `ReplacementRule`, o offset funciona sem alteração. O offset se aplica a **todos** os itens mapeados por aquela regra de brush (comportamento consistente: o brush inteiro é deslocado). Manter o guard de bounds e o fallback in-place existentes.
  - **`ResolveReplacement` e probabilidade**: para regras de brush o resultado não vem de `targets[].id`, então **não** chamar `ResolveReplacement`. Se o único target de brush tiver `probability < 100`, respeitar com um roll simples (`uniform_int_distribution<int>(1,100) <= probability`) antes de aplicar; probabilidade 100 (default da Task 4.2) aplica sempre.
  - **TRASH em regra de brush**: se o target de uma regra de brush for `TRASH_ITEM_ID` (usuário clicou em REMOVE), remover todo item pertencente ao brush de origem. Detectar via `MapItem(...).matched` e tratar `newId = TRASH_ITEM_ID` pelo caminho já existente.

---

### Phase 4: UI — picker, card e hit-test

#### Task 4.1: Criar `BrushPickerDialog`

- **Depends On**: Task 2.1
- **Description**: Diálogo modal filtrável para escolher um brush, modelado em `RebuildGeneralGroundBrushList`/`RebuildWallBrushList`.
- **Files**: `source/ui/replace_tool/brush_picker_dialog.h` (novo), `source/ui/replace_tool/brush_picker_dialog.cpp` (novo)
- **Details**:
  - API:
    ```cpp
    class BrushPickerDialog : public wxDialog {
    public:
        // familyFilter: quando não vazio, restringe à família do brush informado
        // (usado no slot de destino para só listar brushes compatíveis com a origem).
        BrushPickerDialog(wxWindow* parent, const std::string& initialSelection,
                          const Brush* familyFilter = nullptr);
        std::string GetSelectedBrushName() const;
    };
    ```
  - Layout: `wxTextCtrl` de busca (com `SetHint("Search...")`) + `wxListCtrl` em `wxLC_REPORT | wxLC_SINGLE_SEL` com `wxImageList` de ícones + `CreateButtonSizer(wxOK | wxCANCEL)`.
  - `RebuildList()` — cópia adaptada de `dungeon_preset_editor_dialog.cpp:987-1012`:
    ```cpp
    for (const auto& [name, brush] : g_brushes.getMap()) {
        if (!brush->is<GroundBrush>() && !brush->is<WallBrush>() && !brush->is<CarpetBrush>()) continue;
        if (familyFilter && !BrushMappingService::AreCompatible(familyFilter, brush.get())) continue;
        if (!filter.empty() && wxString(name).Lower().Find(filter) == wxNOT_FOUND) continue;
        entries.push_back({name, BrushMappingService::GetPreviewItemId(brush.get())});
    }
    std::sort(entries.begin(), entries.end());
    ```
    Nota: `getMap()` é um `multimap` — um mesmo nome pode aparecer mais de uma vez; deduplicar por nome ao montar `entries`.
  - Coluna extra "Type" mostrando `Ground` / `Wall` / `Carpet` para o usuário distinguir brushes homônimos de famílias diferentes.
  - Bind: `wxEVT_TEXT` na busca → `RebuildList()`; `wxEVT_LIST_ITEM_ACTIVATED` (duplo clique) → `EndModal(wxID_OK)`.
  - **Hotkeys**: seguir `CreateSearchField` (`dungeon_preset_editor_dialog.cpp:969-985`) — `g_hotkeys.DisableHotkeys()` em `wxEVT_SET_FOCUS` e `EnableHotkeys()` em `wxEVT_KILL_FOCUS`, senão digitar na busca dispara atalhos do editor.
  - **Theme**: cores via `Theme::Get(Theme::Role::X)`; ícones via o mesmo helper usado no dialog de referência (`MakeItemBitmap`); se for `static` local àquele `.cpp`, replicar o helper localmente em vez de exportá-lo.

#### Task 4.2: Modo brush nos slots do card (hit-test + interação)

- **Depends On**: Task 1.1, Task 4.1
- **Description**: Cada slot (Source e Target) ganha uma affordance para alternar para modo brush e escolher o brush.
- **Files**: `source/ui/replace_tool/rule_builder_panel.h`, `source/ui/replace_tool/rule_builder_panel.cpp`
- **Details**:
  - Em `HitResult::Type` (`rule_builder_panel.h:26-40`), acrescentar **ao final do enum** (a ordem importa: `rule_card_renderer.cpp:131, 175, 190` compara `dragHoverType` contra os literais `1`, `2`, `3`, `8` — inserir no meio quebraria o desenho):
    ```cpp
    ToggleSourceKind,   // pequeno badge "ITEM/BRUSH" no slot source
    ToggleTargetKind,   // idem no slot target
    PickBrush,          // clicar no corpo de um slot que já está em modo brush
    ```
    **Recomendação adicional**: nesta mesma passada, trocar os literais mágicos de `rule_card_renderer.cpp` por `RuleBuilderPanel::HitResult::Source` / `::Target` / `::AddTarget` / `::DeleteTarget`, eliminando a fragilidade.
  - **Hit-test** (`rule_builder_panel.cpp:436-483`): dentro do retângulo do slot source (`:439-441`) e de cada target (`:457-466`), reservar uma faixa inferior de ~14px como toggle de tipo:
    ```cpp
    // badge de tipo na base do card do slot
    if (localY >= slotY + ITEM_HEIGHT - 14 && localY <= slotY + ITEM_HEIGHT) {
        return { HitResult::ToggleSourceKind /* ou ToggleTargetKind */, i, t };
    }
    ```
    Manter os retornos atuais (`Source`, `DeleteTarget`) para o resto da área, mas quando o slot já estiver em modo brush, o corpo retorna `PickBrush` em vez de `Source`/`DeleteTarget`.
  - **`OnMouse`** (`:327-394`), novos ramos em `LeftDown`:
    - `ToggleSourceKind`: alterna `rule.fromKind` entre `Item` e `Brush`. Ao virar `Brush`, zerar `fromId` e abrir imediatamente o `BrushPickerDialog`; ao virar `Item`, limpar `fromBrushName`. Depois `OnRuleChanged()` + `LayoutRules()` + `Refresh()`.
    - `ToggleTargetKind`: idem para `targets[t].kind` / `brushName` / `id`.
    - `PickBrush`: abre `BrushPickerDialog`; no slot de **target**, passar como `familyFilter` o brush já escolhido no source (se houver) para só listar compatíveis. Gravar `brushName` no slot e persistir.
  - **Regra de cardinalidade**: quando `rule.fromKind == SlotKind::Brush`, a regra aceita **um único** target. Em `ItemDropTarget::OnDropText` (`:102-118`) e no ramo `AddTarget` de `OnMouse` (`:345-355`), rejeitar adicionar um segundo target se `isBrushRule()` e `targets.size() >= 1`. Consequentemente `DistributeProbabilities` (`:288-311`) manterá o único target em 100%.
  - **Drop de item num slot em modo brush**: em `OnDropText`, se o slot alvo estiver em modo `Brush`, **rejeitar** o drop (`return false`) em vez de gravar um `fromId` que seria ignorado — evita estado inconsistente e silencioso.
  - `ApplyItemAsSource` (`:187-201`) / `ApplyItemAsTarget` (`:203-227`): ao reutilizar a última regra, pular regras em modo brush (criar uma regra nova de item em vez de contaminar a de brush).

#### Task 4.3: Desenho dos slots em modo brush

- **Depends On**: Task 4.2
- **Description**: Renderizar slots de brush com ícone representativo, nome do brush e o badge de tipo.
- **Files**: `source/ui/replace_tool/rule_card_renderer.h`, `source/ui/replace_tool/rule_card_renderer.cpp`
- **Details**:
  - Nova assinatura ao lado da existente (`rule_card_renderer.h:26`), sem quebrar a atual:
    ```cpp
    static void DrawRuleBrushCard(NanoVGCanvas* canvas, NVGcontext* vg,
                                  float x, float y, float w, float h,
                                  const std::string& brushName,
                                  bool highlight, bool showDeleteOverlay,
                                  int probability = -1);
    static void DrawSlotKindBadge(NVGcontext* vg, float x, float y, float w, float h,
                                  bool isBrush, bool hovered);
    ```
  - `DrawRuleBrushCard`: mesmo fundo/borda de `DrawRuleItemCard` (`:280-293`); ícone via `canvas->GetOrCreateItemImage(BrushMappingService::GetPreviewItemId(brush))`; rótulo = nome do brush via `nvgTextBox` (mesmo bloco de `:326-330`). Se o brush não for encontrado no registry, desenhar o nome em `Theme::Role::Error` para sinalizar regra quebrada.
  - `DrawSlotKindBadge`: pílula de 14px na base do slot com texto `ITEM` ou `BRUSH`; preenchida com `Theme::Role::Accent` quando `isBrush`, apenas contornada com `Theme::Role::CardBorder` quando `Item` — exatamente o padrão visual já usado pelo badge de offset (`rule_card_renderer.cpp:138-163`), garantindo consistência.
  - Em `DrawRuleCard` (`:88-224`), despachar por `kind`: source usa `DrawRuleBrushCard` quando `rule.fromKind == SlotKind::Brush`, senão `DrawRuleItemCard`; idem por target. Desenhar `DrawSlotKindBadge` sobre cada slot depois do card.
  - **Geometria**: usar as mesmas constantes cruas (não-DPI) que o hit-test usa, seguindo o comentário já presente em `:144` ("raw constants so HitTest matches on any DPI"). Qualquer constante nova (altura do badge = 14) deve existir em **um só lugar** — declarar em `RuleCardRenderer` como `static const int KIND_BADGE_H = 14;` e consumir também no `HitTest`.
  - Ajustar `GetRuleHeight` (`rule_builder_panel.cpp:233-257`) se o badge não couber em `ITEM_H = 110` — o rótulo já ocupa até `y+84` (`rule_card_renderer.cpp:337`), então 14px na base cabem sem aumentar a altura.

---

### Phase 5: Integração

#### Task 5.1: Registrar os novos arquivos no CMakeLists

- **Depends On**: Task 2.1, Task 4.1
- **Files**: `source/CMakeLists.txt`
- **Details**:
  - Headers, após a linha 393 (`visual_similarity_service.h`):
    ```
    ${CMAKE_CURRENT_LIST_DIR}/ui/replace_tool/brush_mapping_service.h
    ${CMAKE_CURRENT_LIST_DIR}/ui/replace_tool/brush_picker_dialog.h
    ```
  - Sources, após a linha 821 (`visual_similarity_service.cpp`):
    ```
    ${CMAKE_CURRENT_LIST_DIR}/ui/replace_tool/brush_mapping_service.cpp
    ${CMAKE_CURRENT_LIST_DIR}/ui/replace_tool/brush_picker_dialog.cpp
    ```

#### Task 5.2: Ajustar a janela ao novo modelo + corrigir erro de sintaxe pré-existente

- **Depends On**: Task 1.1
- **Files**: `source/ui/replace_tool/replace_tool_window.cpp`
- **Details**:
  - **BUG PRÉ-EXISTENTE (bloqueia o build)**: `replace_tool_window.cpp:430` contém uma barra invertida solta onde deveria haver um comentário:
    ```cpp
    \ Auto-add: if enabled, check if the last rule is complete and start a new one
    ```
    Corrigir para `// Auto-add: ...`. Sem isso o arquivo não compila.
  - Em `OnRuleChanged` (`:430-444`), a checagem de "regra completa" usa `lastRule.fromId != 0` e `targets[0].id != 0`. Passar a usar os helpers da Task 1.1:
    ```cpp
    bool hasSource = lastRule.hasSource();
    bool hasTarget = !lastRule.targets.empty() &&
        (lastRule.targets[0].kind == SlotKind::Brush
            ? !lastRule.targets[0].brushName.empty()
            : lastRule.targets[0].id != 0);
    ```
  - Em `OnAddVisibleTiles` (`:552-556`), `existingIds.insert(r.fromId)` deve pular regras de brush (`if (r.isBrushRule()) continue;`), senão todas elas colidem no id `0`.
  - Em `OnExecute` (`:447-452`), a validação `rules.empty()` continua válida; opcionalmente avisar se alguma regra de brush referencia um brush inexistente (usar `BrushMappingService::FindBrush` e listar os nomes quebrados num `wxMessageBox`) antes de executar.

---

## Execution Order

1. **Phase 1**: Task 1.1 + Task 1.2 (paralelo)
2. **Phase 2**: Task 2.1 (após Phase 1)
3. **Phase 3**: Task 3.1 → Task 3.2 (sequencial; 3.2 depende do refactor de 3.1)
4. **Phase 4**: Task 4.1 (paralelo com Phase 3) → Task 4.2 → Task 4.3
5. **Phase 5**: Task 5.1 + Task 5.2 (paralelo, ao final)

Task 4.1 e Phase 3 são independentes e podem correr em paralelo após Phase 2.

## Testing Notes

Sem build automático — verificação manual após o rebuild do usuário:

1. **Retrocompat da persistência**: abrir um rule set salvo antes desta mudança (`%APPDATA%/.../replacer_rules/*.json`). Deve carregar idêntico, com todos os slots em modo ITEM. Salvar e reabrir: os campos novos aparecem no JSON sem alterar o comportamento.
2. **Undo (crítico, vale para regras de item também)**: executar qualquer replacement e apertar Ctrl+Z. O mapa deve voltar exatamente ao estado anterior, numa única operação de undo. Ctrl+Y deve refazer.
3. **Undo com offset**: regra com offset ≠ 0,0; conferir que o undo restaura **tanto** o tile de origem (item de volta) **quanto** o de destino (item removido).
4. **Ground→Ground**: pintar uma área de "grass" com bordas contra vazio; criar regra brush `grass` → `sand`; executar. Cada tile de ground vira ground de sand e **cada borda mantém a mesma direção** (norte continua norte, canto NW continua canto NW) — o contorno da área não pode mudar de forma.
5. **Wall→Wall**: desenhar paredes com cantos, T e um trecho com porta e janela. Trocar o brush. Segmentos preservam a orientação; a porta continua porta (e trancada continua trancada); a janela continua janela.
6. **Carpet→Carpet**: trocar um tapete com bordas; alinhamentos preservados.
7. **Incompatibilidade**: tentar escolher um WallBrush como destino de uma origem GroundBrush — o picker filtrado não deve nem listá-lo.
8. **Brush inexistente**: editar um JSON à mão pondo `fromBrushName` inválido; a regra deve ser descartada com aviso, sem crash.
9. **Precedência**: regra brush `grass`→`sand` + regra item `<id de uma borda de grass>`→`<id específico>`. A regra de item vence naquele id.
10. **Escopos**: repetir 4 e 5 nos três escopos (Selection, Viewport, All Map).

## Risks & Considerations

- **Maior risco: o refactor de undo (Task 3.1)**. A lógica de `PendingMove` muda de ponteiros vivos para posições+índices, e a colisão source/target exige um mapa de tiles pendentes por posição. Se um mesmo tile receber dois `Change` no mesmo `Action`, **o segundo sobrescreve o primeiro** e o usuário perde dados silenciosamente. É o ponto que mais merece revisão cuidadosa.
- **Performance no escopo All Map**: hoje o motor muta tiles direto; passar a fazer `TileOperations::deepCopy` de cada tile alterado aumenta uso de memória e tempo. Mitigação: só copiar tiles que **efetivamente** mudam. Ainda assim, num mapa grande com uma regra abrangente o histórico de undo pode estourar `Config::UNDO_MEM_SIZE` — o `ActionQueue` trata isso descartando entradas antigas, então é degradação graciosa, não crash.
- **Acesso ao `AutoBorder` do GroundBrush**: `borders` e `BorderBlock` são `protected`. O plano usa apenas os acessores públicos (`getFirstOuterAutoBorder`/`getFirstInnerAutoBorder`/`getFirstAutoBorder`), o que cobre o caso comum (um outer + um inner por brush), mas **brushes com múltiplos `<border to="X">` só terão o primeiro considerado**. Se na prática isso se mostrar limitante, a alternativa é adicionar um acessor público `std::vector<const AutoBorder*> getAllAutoBorders() const` em `ground_brush.h` — alteração aditiva e segura, preferível a `friend`.
- **Literais mágicos em `rule_card_renderer.cpp`**: `dragHoverType` é comparado contra `1`/`2`/`3`/`8` (`:131, 175, 190`), acoplado à ordem do enum `HitResult::Type`. Adicionar valores **no fim** do enum é seguro; a Task 4.2 recomenda substituir os literais pelos nomes do enum na mesma passada.
- **`getMap()` é multimap**: nomes duplicados são possíveis; deduplicar no picker e lembrar que `FindBrush`/`g_brushes.getBrush(name)` retorna apenas uma das entradas. Brushes homônimos de famílias diferentes são desambiguados pelo `AreCompatible` no momento da execução.
- **Escopo deliberadamente fora**: Table e Doodad não são suportados (decisão do usuário). O picker não deve listá-los, para não criar regras que silenciosamente não fazem nada.
- **Sem re-bordering**: por design não se chama `doBorders`/`doWalls`/`doCarpets`. Se o brush destino tiver um conjunto de bordas incompleto (falta alguma direção), aqueles tiles ficam sem substituição (`resolved=false`) e **preservam o item original** em vez de virar buraco — falha segura.
- **Build**: NÃO executar build; o usuário compila manualmente.
