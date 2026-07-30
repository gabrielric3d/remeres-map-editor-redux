# Plan: Map Synthesizer - Painel de Controles de Silhueta

## Overview
Adicionar um painel de "Esculpir silhueta" ao Map Synthesizer (`scripts/map_synthesizer/main.lua`) com 4 grupos de controle (suavidade da borda, perfil de montanha, terraços/níveis, prévia ao vivo) que permitem o usuário moldar a forma gerada, substituindo os "dentes" verticais aleatórios atuais por formas controláveis. Mudança 100% Lua, sem rebuild C++.

## User Request
Adicionar um painel de controles de SILHUETA para o usuário esculpir o formato gerado. 4 grupos, todos solicitados, plano faseado:
- GRUPO 1 — Suavidade da borda (estende `roughPct`): slider Liso↔Dentado + Tamanho do recorte.
- GRUPO 2 — Perfil de montanha (algoritmo novo "subida e descida"): on/off, lado dos picos, espaçamento, altura, inclinação da rampa.
- GRUPO 3 — Terraços/níveis (alavanca os anéis de distância existentes): Nº de níveis (0 = um ground só; N = anéis concêntricos).
- GRUPO 4 — Prévia ao vivo (silhueta/máscara no diálogo enquanto mexe nos sliders), reusando blit em baixa resolução; fallback botão "Atualizar prévia".

## Analysis
- **New Files**: No.
- **Modified Files**: Yes — apenas `scripts/map_synthesizer/main.lua`.
- **CMakeLists Update**: No (Lua puro).
- **Menu/Toolbar Wiring**: No.
- **Action System**: No (a geração já usa `app.transaction`).
- **Data Files (XML/TOML)**: No.
- **Rebuild C++**: NÃO. Todas as APIs necessárias já existem no build atual (confirmado lendo `source/lua/lua_dialog.cpp`).

---

## DECISÃO CRÍTICA: WFC × Máscara (verificada no código)

**Pergunta:** É viável fazer os knobs dos Grupos 1-3 valerem também no modo WFC ("textura fiel"), interseccionando a saída do solver com a máscara da silhueta, sem quebrar a convergência?

**Resposta: SIM, é seguro — mas com escopo deliberadamente faseado.** Justificativa, com base no código real:

1. **A saída do WFC é uma grade de labels pós-decode** (`decode`, main.lua:563-582). Cada célula é um label (`>=1`), `EMPTY`, ou `nil` (contradição → "deixa sem pintar"). O `applyResult` (1364-1397) **ignora silenciosamente** células `nil`/`EMPTY` (`if li and li ~= EMPTY then ...`). Portanto, sobrescrever células FORA da máscara para `EMPTY` **depois** do decode é trivialmente seguro: não toca no solver, não afeta a convergência, só recorta a borda. O solver já rodou e convergiu antes de qualquer máscara entrar em cena.

2. **`filterShapes` (587-657) já reescreve `out` in-place** definindo `out[c[2]][c[1]] = EMPTY` para blobs descartados. Mascarar é exatamente a mesma operação (zerar células para `EMPTY`), só que por critério geométrico (dentro/fora da máscara) em vez de "blob pequeno". Logo o padrão de mutação já é usado e comprovadamente compatível com `applyResult`.

3. **Re-borderizar a borda limpa já é automático.** `applyResult` chama `t:borderize()` nos tiles pintados + vizinhos quando `opts.borderize` está ligado (1400-1421). Recortar o WFC contra a máscara cria uma borda nova e limpa, e o auto-border existente já a finaliza. Não é preciso código de re-borderização especial.

**Conclusão de fase:**
- **Grupos 1-3 são desenvolvidos e validados primeiro no modo Shape** (P1-P3), onde a máscara é o produto central — risco baixo, ganho imediato e visível.
- **A intersecção WFC×máscara fica numa fase própria (P3.5, opcional/curta) que reusa a MESMA função `buildSilhouetteMask`.** Como a operação é só "zerar fora da máscara após decode", o custo é baixo e o risco é controlado.
- **Fallback explícito (se P3.5 mostrar artefato visual):** manter os Grupos 1-3 como **exclusivos do modo Shape** e desabilitar/avisar os controles no modo WFC (tooltip "Só no modo Formato novo"). Isso preserva 100% do comportamento WFC atual. O plano marca P3.5 como ISOLADA e descartável sem afetar P1-P3.

**Por que NÃO semear a máscara como constraint inicial do solver:** seria muito mais arriscado (pode impedir a convergência, já que o solver tenta 8 vezes e pode falhar — ver 1562-1574). A intersecção pós-decode é estritamente mais segura e atinge o objetivo visual (recortar a borda). Decisão: **intersecção pós-decode, nunca constraint pré-solve.**

---

## FUNÇÃO UNIFICADORA: `buildSilhouetteMask`

Extrair, do miolo de `synthesizeShape` (1219-1330), uma função compartilhada:

```
buildSilhouetteMask(W, H, opts, baseR, target, fringeDepth, nDetail, neededDist, setStatus)
  -> mask (grade booleana), placedCount
```

Ela encapsula o loop de colocação de shapes (linhas 1219-1330), agora aplicando os knobs novos via os parâmetros derivados de `opts`. `synthesizeShape` passa a chamá-la; o "vestir por anéis" (1332-1354) permanece em `synthesizeShape` mas ganha o suporte a níveis do Grupo 3. No modo WFC, P3.5 chama `buildSilhouetteMask` para obter a máscara de recorte.

> NOTA de compatibilidade: o modo `path` (rio) NÃO entra na máscara nova de blob — continua usando `generatePath`. Os Grupos 1-2 só fazem sentido para blobs; o Grupo 3 (níveis) vale para ambos. A UI deve refletir isso nos tooltips.

---

## Tasks

### Phase 1 (P1): Grupo 1 — Suavidade da borda

Objetivo: substituir os parâmetros internos `nDetail`/`roughPct`/`addDetail` por dois controles intuitivos, mantendo o comportamento atual como default.

#### Task P1.1: Parametrizar `addDetail` por tamanho de recorte
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - `addDetail` (870-910) hoje sorteia o raio da mordida em `rr = max(1.5, baseR*(0.12 + rand*0.22))`. Adicionar parâmetro `biteScale` (default `1.0` = comportamento atual): `rr = max(1.5, baseR*(0.12 + rand*0.22) * biteScale)`. Valores `<1` = mordidas pequenas (1 tile); `>1` = lobos grandes.
  - Assinatura nova: `addDetail(mask, W, H, count, baseR, biteScale)`. Chamada em 1259 passa `biteScale`.

#### Task P1.2: Mapear sliders → `nDetail` e `biteScale`
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - Em `synthesizeShape`, a derivação atual (1199-1203):
    - `rough = opts.roughPct or 1`
    - `nDetail = floor((6 + 18*max(0,targetC-1)) * rough)`
  - Introduzir `smoothness` (0..100, Grupo 1 slider "Liso↔Dentado", default 100 = recortado como hoje). Mapear:
    - `local jag = (opts.smoothness or 100) / 100`  (1 = dentado, 0 = liso)
    - Multiplicar o `nDetail` derivado por `jag`: `nDetail = floor(nDetail * jag)`. Com `jag=0`, `nDetail=0` → nenhuma mordida → bordas lisas (só o `smoothMask` arredondado). Com `jag=1`, idêntico ao atual.
    - Quando `jag` for baixo (< 0.35), aplicar uma passada extra de `smoothMask` no candidato após `addDetail` (1259) para garantir borda visivelmente lisa.
  - `biteScale = opts.biteScale or 1` vindo do slider "Tamanho do recorte" (mapear 0..100 → 0.4..2.5, com 50 ≈ 1.0). Passar para `addDetail`.
  - IMPORTANTE: manter `roughPct` existente funcionando (ele ainda controla `targetC` e o `rough` base na 1201-1203). O slider novo de suavidade é um multiplicador adicional; assim opções antigas (sem `smoothness`/`biteScale`) reproduzem exatamente o comportamento atual via defaults.

#### Task P1.3: UI do Grupo 1 + `readOpts`
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - Criar uma nova `dlg:box({ orient="vertical", label="Esculpir silhueta" })` logo após o box "Tamanho e contorno" (após 2100), antes de "Quantidade e distribuicao".
  - Subgrupo Grupo 1 (`dlg:wrap`/`groupIcon("svg/solid/wave-square.svg")` ou ícone existente; fallback itemid como nos outros):
    - `dlg:slider({ id="smoothness", label="Liso <-> Dentado", min=0, max=100, value=100, tooltip="0 = bordas lisas; 100 = recortado como as amostras." })`
    - `dlg:slider({ id="biteScale", label="Tamanho do recorte", min=0, max=100, value=50, tooltip="Mordidas pequenas (1 tile) a lobos grandes." })`
  - Em `readOpts` (2204-2223), adicionar:
    - `smoothness = dlg.data.smoothness or 100,`
    - `biteScale = 0.4 + (dlg.data.biteScale or 50)/100 * 2.1,`  (0..100 → 0.4..2.5)
- **Teste manual P1**:
  1. Modo "Formato novo", selecionar uma área, Gerar com sliders no default → resultado idêntico ao de antes (dentado).
  2. Smoothness → 0: borda lisa/arredondada, sem dentes.
  3. biteScale alto: mordidas grandes (lobos); baixo: mordidas de 1 tile.
  4. Reroll continua funcionando.

---

### Phase 2 (P2): Grupo 2 — Perfil de montanha (algoritmo novo)

Objetivo: modelar a borda da silhueta como rampas diagonais formando picos/vales no(s) lado(s) escolhido(s), substituindo o ruído radial simétrico quando ligado.

#### Task P2.1: Função `applyMountainProfile(mask, W, H, opts)`
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - Nova função pura, posta perto de `breakStraightRuns` (~1100). Recebe a máscara booleana já preenchida e ESCULPE a face escolhida:
    - Parâmetros de `opts.mountain` (tabela): `enabled`, `sides` ("N"/"S"/"L"/"O"/"todos"), `spacing` (tiles entre picos), `height` (amplitude do pico em tiles), `slope` (tiles para subir 1 degrau — controla a inclinação da rampa).
  - Algoritmo "subida e descida" por coluna/linha da face:
    - Para a face N (norte): para cada coluna `x` dentro do bbox, calcular a borda superior atual (primeira célula `true` de cima). Gerar um perfil de altura via onda triangular/serrote: `peaks` ao longo de `x` com período `spacing`, amplitude `height`, e as bordas da onda inclinadas conforme `slope` (rampa de `slope` tiles por degrau → inclui jitter leve por `math.random` para não ficar perfeitamente periódico). REMOVER células acima do perfil (recorta vales) e/ou ADICIONAR para formar picos, limitado ao bbox e às margens (`x>2 ... y<H-1`).
    - Faces S/L/O por simetria (varrer linhas/colunas na direção correspondente). "todos" aplica nas 4 faces; um único lado aplica só naquela.
  - Usar uma função triangular determinística por seed + jitter: `profileAt(t) = height * tri(t/spacing + phase)` onde `tri` sobe/desce linearmente (a inclinação efetiva já é `height/spacing`; `slope` reescala para "tiles por degrau" discretizando em patamares de `slope`).
  - Após esculpir: chamar `fillHoles`, `despeckle` para limpar artefatos. NÃO chamar `breakStraightRuns` aqui (as rampas diagonais já quebram retas; deixar `synthesizeShape` decidir).

#### Task P2.2: Integrar no pipeline do modo Shape
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - Em `buildSilhouetteMask` (a função extraída — ver "Função unificadora"), após colocar cada shape e antes do `breakStraightRuns` (1283), inserir:
    ```
    if opts.mountain and opts.mountain.enabled then
        applyMountainProfile(cand, W, H, opts)
    end
    ```
  - Quando `mountain.enabled`, REDUZIR/ANULAR o `addDetail` radial para esse candidato (o perfil de montanha é a fonte de irregularidade da face escolhida): se `mountain.enabled`, usar `nDetail` reduzido (ex.: `nDetail = floor(nDetail * 0.3)`) para não competir com as rampas. Documentar no tooltip.

#### Task P2.3: UI do Grupo 2 + `readOpts`
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - Dentro do box "Esculpir silhueta", subgrupo Grupo 2:
    - `dlg:check({ id="mtnOn", text="Perfil de montanha (subida/descida)", selected=false, tooltip="Esculpe a face escolhida em picos e vales por rampas diagonais." })`
    - `dlg:combobox({ id="mtnSides", label="Lado dos picos", options={"Norte","Sul","Leste","Oeste","Todos"}, option="Norte" })`
    - `dlg:slider({ id="mtnSpacing", label="Espacamento dos picos", min=3, max=40, value=10 })`
    - `dlg:slider({ id="mtnHeight", label="Altura dos picos", min=1, max=20, value=6 })`
    - `dlg:slider({ id="mtnSlope", label="Inclinacao da rampa", min=1, max=8, value=2, tooltip="Tiles para subir 1 degrau. Maior = rampa mais suave." })`
  - Em `readOpts`, montar a tabela:
    ```
    mountain = {
      enabled = dlg.data.mtnOn or false,
      sides   = ({Norte="N",Sul="S",Leste="L",Oeste="O",Todos="todos"})[dlg.data.mtnSides] or "N",
      spacing = math.floor(dlg.data.mtnSpacing or 10),
      height  = math.floor(dlg.data.mtnHeight or 6),
      slope   = math.floor(dlg.data.mtnSlope or 2),
    },
    ```
- **Teste manual P2**:
  1. Ligar "Perfil de montanha", lado Norte → a borda norte vira serra de picos/vales; demais bordas orgânicas.
  2. Aumentar "Altura" → picos mais altos; "Espaçamento" → picos mais espaçados; "Inclinação" maior → rampas mais suaves (mais diagonais).
  3. "Todos" → 4 faces recortadas em serra.
  4. Desligar → resultado igual ao da fase P1 (sem perfil).

---

### Phase 3 (P3): Grupo 3 — Terraços/níveis

Objetivo: expor os anéis de distância (já calculados em 1340-1352) como N níveis concêntricos distintos, um label por nível. `0` = comportamento atual (label por anel aprendido).

#### Task P3.1: Vestir por níveis em vez de por anel aprendido
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - O bloco 1332-1354 hoje faz: `dist = distanceToEmpty(rows)`, `out[y][x] = ringLabel[min(d, dmax)]` (label dominante por distância da amostra).
  - Adicionar caminho alternativo quando `opts.levels and opts.levels > 0`:
    - Computar `dM` = distância máxima na máscara (já temos `dist`). Dividir `[1..dM]` em `N=opts.levels` faixas iguais → `levelOf(d) = min(N, ceil(d / dM * N))`.
    - Mapear cada nível a um label: reusar `ringLabel` amostrando em profundidades representativas — `levelLabel[k] = ringLabel[min(dmax, round((k-0.5)/N * dmax))]`. Assim cada anel concêntrico recebe o ground correspondente à camada aprendida naquela "altura" relativa, garantindo N grounds distintos quando as amostras têm camadas.
    - `out[y][x] = levelLabel[levelOf(d)]`.
  - Quando `opts.levels == 0` (ou nil), manter EXATAMENTE o caminho atual (1346-1348). Default = 0 → comportamento inalterado.
  - Observação: se as amostras só tiverem 1 label (sem camadas), todos os níveis cairão no mesmo label — comportamento aceitável (e documentado no tooltip: "precisa de amostras com camadas/anéis para níveis distintos").

#### Task P3.2: UI do Grupo 3 + `readOpts`
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - Subgrupo Grupo 3 no box "Esculpir silhueta":
    - `dlg:slider({ id="levels", label="Niveis (terracos)", min=0, max=6, value=0, tooltip="0 = um ground (como as amostras). N = aneis concentricos como terracos. Precisa de amostras com camadas." })`
  - Em `readOpts`: `levels = math.floor(dlg.data.levels or 0),`
- **Teste manual P3**:
  1. Níveis = 0 → idêntico a P2.
  2. Níveis = 3 com um perfil que tenha camadas (ex.: grama→terra→pedra) → 3 anéis concêntricos visíveis do exterior ao centro.
  3. Combinar com Grupo 2 (montanha) → terraços recortados em serra.

---

### Phase 3.5 (P3.5 — ISOLADA, descartável): Knobs no modo WFC via intersecção pós-decode

Objetivo: fazer Grupos 1-3 valerem no modo WFC recortando a saída do solver contra `buildSilhouetteMask`. **Esta fase é independente e pode ser pulada** sem afetar P1-P3.

#### Task P3.5.1: Intersecção da saída WFC com a máscara
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - Em `synthesize` (1504-1582), após `decode` (1576) e antes do `filterShapes` (1579), quando `opts.useSilhouetteMask` (novo, ligado por um check):
    - Gerar `mask = buildSilhouetteMask(W, H, opts, ...)` (derivar `baseR/target/fringeDepth/nDetail` chamando `computeRingStats` como `synthesizeShape` faz em 1182-1203 — extrair esses cálculos para um helper `deriveShapeParams(profile, opts, W, H)` reusado por ambos os modos).
    - Para cada célula: `if not mask[y][x] then out[y][x] = EMPTY end`. (Recorta fora da máscara; seguro — `applyResult` ignora EMPTY.)
    - Se `opts.levels > 0`, re-rotular dentro da máscara por níveis (reaproveitar a lógica de P3.1 sobre `distanceToEmpty(out)`), opcional.
  - `filterShapes` roda depois normalmente. `borderize` no `applyResult` finaliza a borda nova.
- **Risco/fallback**: se a textura WFC ficar visualmente cortada de forma feia na borda, NÃO ligar por padrão. Manter o check desligado e/ou, no fallback, **não implementar P3.5** e em vez disso desabilitar os sliders dos Grupos 1-3 no modo WFC com tooltip "Só no modo Formato novo". P1-P3 não dependem disto.

#### Task P3.5.2: UI/`readOpts` para o toggle
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - `dlg:check({ id="wfcMask", text="Aplicar silhueta no WFC", selected=false, tooltip="Recorta a textura WFC com os controles de silhueta acima." })`
  - `readOpts`: `useSilhouetteMask = dlg.data.wfcMask or false,`
- **Teste manual P3.5**:
  1. Modo WFC sem o check → idêntico ao WFC atual.
  2. Com o check + smoothness/montanha/níveis → a textura WFC recortada na forma da silhueta. Verificar bordas (auto-border deve fechar). Se feio, deixar o check OFF como padrão.

---

### Phase 4 (P4): Grupo 4 — Prévia ao vivo da silhueta

Objetivo: mostrar a máscara/silhueta atualizando no diálogo conforme o usuário mexe nos controles, reusando blit de baixa resolução. Fallback: botão "Atualizar prévia".

#### Task P4.1: Renderizar a máscara como imagem (baixa resolução)
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - Nova função `renderMaskPreview(mask, W, H, levelsGrid?)` que produz um `LuaImage` ~`PREVIEW_W x PREVIEW_H`:
    - Calcular `px = max(1, min(floor(PREVIEW_W/W), floor(PREVIEW_H/H)))` (downscale: 1 px por tile no pior caso).
    - Construir via tiles de cor sólida com `Image.blank` + `:blit`. RESTRIÇÃO confirmada: `LuaImage` NÃO tem set-pixel; só `Image.blank/blit/resize/fromItemSprite`. Estratégia: criar **blocos pré-pintados reutilizáveis** — `Image.blank(px, px, r,g,b)` por cor de nível (cache por nível, no máx 7 cores) e blitar o bloco certo em cada célula preenchida; deixar o fundo (vazio) na cor base do canvas. Cores por nível: paleta fixa (ex.: tons do exterior→centro). Tudo sob `pcall` (a infra de preview já usa `hasPreview`).
    - Centralizar num canvas `PREVIEW_W x PREVIEW_H` como `renderPreviewCanvas` faz (1798-1806).
  - Custo: para área até ~140x140 e `px` pequeno, são milhares de blits de blocos pequenos. Aceitável para um clique; possivelmente pesado por tick de slider (ver P4.3).

#### Task P4.2: Gerar a máscara de prévia barata (sem pintar o mapa)
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - Função `previewMask(profile, opts)` que:
    - Determina W,H da seleção atual (ou um quadrado fixo, ex. 48x48, se sem seleção) — reusar a lógica de bounds de `generate`/`generateToClipboard`.
    - **Limita o tamanho** para a prévia: se `W*H` grande, fazer downscale do alvo (gerar a máscara numa grade reduzida, ex. cap em 64x64) para manter barato. A prévia é indicativa, não precisa ser 1:1.
    - Chama `buildSilhouetteMask` (Shape) — ou, no modo WFC com `wfcMask`, ainda mostra só a MÁSCARA da silhueta (não roda o solver WFC na prévia; rodar WFC ao vivo é caro demais). Tooltip: "a prévia mostra a forma da silhueta; a textura final vem na geração".
    - Aplica níveis (P3) para colorir a prévia se `levels>0`.
  - Retorna `mask, W, H` para `renderMaskPreview`.

#### Task P4.3: Wiring ao vivo com fallback por botão
- **Files**: `scripts/map_synthesizer/main.lua`
- **Details**:
  - Adicionar um `dlg:image({ id="silPreview", image=Image.blank(...), smooth=false })` no box "Esculpir silhueta" (só se `hasPreview`).
  - Função `updateSilPreview()` (guardada por `busy` e `pcall`): `local m,w,h = previewMask(...); dlg:modify({ silPreview = { image = renderMaskPreview(m,w,h) } }); dlg:repaint()`.
  - **Estratégia de performance (decisão):** ligar `onchange` dos sliders/checks/combobox de silhueta a `updateSilPreview` é tentador, mas `onchange` do slider dispara A CADA tick de arrasto (wxEVT_SLIDER, confirmado em lua_dialog.cpp:918) → re-render por tick pode travar a UI. **Mitigações, nesta ordem:**
    1. **Debounce por contador/tempo:** em `updateSilPreview`, registrar `os.clock()` da última render; se a anterior foi há < ~150ms, agendar/pular (Lua não tem timer; usar guarda simples: marcar "dirty" e só re-renderizar quando o valor "assentar" — na prática, pular se `busy`).
    2. **Cap de tamanho agressivo** (P4.2 já limita a grade a 64x64) para baratear cada render.
    3. **FALLBACK PRINCIPAL (recomendado como default):** NÃO ligar `onchange` ao vivo. Em vez disso, um botão `dlg:button({ text="Atualizar previa", onclick=guarded(updateSilPreview) })`. O usuário mexe nos sliders e clica para ver. Simples, robusto, sem risco de travar.
    4. **Opção ao vivo (se a render se mostrar barata):** ligar `onchange` só nos controles "baratos de mudar" (combobox de lado, check on/off) e deixar os sliders contínuos no botão. Documentar a escolha no código.
  - DECISÃO do plano: **implementar o botão "Atualizar prévia" como mecanismo primário** (garantido). Tentar `onchange` ao vivo como melhoria; se em teste manual a UI travar com áreas grandes, manter só o botão. Ambos compartilham `updateSilPreview`.
- **Teste manual P4**:
  1. Clicar "Atualizar prévia" → aparece a silhueta colorida (por nível) no painel, condizente com os sliders.
  2. Mexer em smoothness/montanha/níveis e atualizar → a prévia muda de acordo.
  3. (Se ao vivo ligado) arrastar um slider em área grande → conferir se a UI não trava; se travar, reverter para só-botão.
  4. Gerar na seleção → o resultado real bate com a prévia.

---

## Execution Order
1. **P1** (Grupo 1: suavidade) — Tasks P1.1 → P1.2 → P1.3. Gerador funcional ao fim.
2. **P2** (Grupo 2: montanha) — P2.1 → P2.2 → P2.3. Depende da extração de `buildSilhouetteMask` (fazer a extração no início de P2, pois P2.2 a usa; alternativamente extrair já em P1 se conveniente).
3. **P3** (Grupo 3: níveis) — P3.1 → P3.2.
4. **P3.5** (WFC×máscara, ISOLADA/opcional) — P3.5.1 → P3.5.2. Pode ser pulada.
5. **P4** (Grupo 4: prévia) — P4.1 → P4.2 → P4.3.

> Recomendação: extrair `buildSilhouetteMask` e `deriveShapeParams` como primeiro passo de P2 (refactor sem mudança de comportamento), pois P2/P3.5/P4 dependem dela. P1 pode ser feita antes da extração (mexe só em `addDetail`/derivação de `nDetail`).

## Testing Notes
- Não há build: testar carregando o script no editor (menu do Map Synthesizer) sobre um mapa aberto com um perfil que já tenha amostras aprendidas (idealmente com camadas/anéis para o Grupo 3).
- Para cada fase: gerar na seleção ANTES e DEPOIS, com os controles novos nos defaults → resultado deve ser idêntico ao comportamento atual (regressão zero). Depois variar cada controle e confirmar o efeito descrito.
- Reroll e "Gerar p/ Paste" devem continuar funcionando em todas as fases (ambos passam por `readOpts`, então herdam os campos novos automaticamente).
- Verificar `app.yield()` dentro de loops pesados novos (perfil de montanha, render de prévia) para não congelar a UI, seguindo o padrão já usado (1305, 747).

## Risks & Considerations
- **Compatibilidade (crítico):** `readOpts` DEVE usar `or <default>` em todos os campos novos, com defaults que reproduzem o atual: `smoothness=100`, `biteScale→1.0` (slider em 50), `mountain.enabled=false`, `levels=0`, `useSilhouetteMask=false`, `wfcMask=false`. Opções persistidas/antigas sem esses campos caem nos defaults e geram o resultado de hoje.
- **WFC×máscara (Grupo 1-3 no WFC):** decisão = intersecção pós-decode (segura, não afeta convergência — ver seção dedicada). Mantida em fase ISOLADA (P3.5), default OFF, com fallback de desabilitar no WFC se o recorte ficar feio. P1-P3 não dependem dela.
- **Prévia ao vivo (Grupo 4 — maior risco de UX):** `slider:onchange` dispara por tick de arrasto; re-render por tick pode travar a UI em áreas grandes. Mitigação: **botão "Atualizar prévia" como mecanismo primário garantido**; ao vivo só como melhoria opcional com cap de tamanho (64x64) e guarda `busy`. `LuaImage` não tem set-pixel — a prévia usa blocos `Image.blank(px,px,cor)` em cache + `:blit` (confirmado: única via). Tudo sob `pcall`/`hasPreview`, degradando para "(preview indisponível)" como já faz o preview de amostras.
- **Modo path (rio):** Grupos 1-2 não se aplicam a `path` (usa `generatePath`); a UI deve indicar isso por tooltip. Grupo 3 (níveis) pode valer para path também (o vestir por distância já roda). Não quebrar o caminho do rio.
- **Feature em PAUSA (doodads/stamp off-ground, "Abordagem B"):** este plano NÃO toca nas funções de doodad/stamp (`mergedDoodads`, blocos de doodads em `applyResult` 1426-1492). Sem conflito.
- **Refactor `buildSilhouetteMask`:** extrair o loop 1219-1330 deve ser comportamento-neutro. Validar com regressão (gerar com defaults antes/depois da extração e comparar). Risco baixo, mas é o ponto de maior superfície de mudança — fazer com cuidado e testar isoladamente.
- **`slider` sem readout numérico:** o widget não mostra o valor atual ao lado (confirmado em lua_dialog.cpp). Aceitável para esculpir (o efeito é visual). Se quiser número, exigiria um `dlg:label` extra atualizado por `onchange` — fora de escopo, opcional.
