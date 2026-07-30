# Summary: Map Synthesizer - Painel de Controles de Silhueta

**Plan**: `.redux/plans/06_Map_Synthesizer_Silhouette_Controls.md`
**Executed**: 2026-06-11
**Status**: Completed (P1, P2, P3, P3.5, P4 — todas as fases)

## Changes Made

| File | Action | Description |
|------|--------|-------------|
| scripts/map_synthesizer/main.lua | Modified | Painel "Esculpir silhueta" (Grupos 1-4), refactor `buildSilhouetteMask`/`deriveShapeParams`/`dressMaskByRings`, `applyMountainProfile`, previa ao vivo, novos campos em `readOpts` |

Mudanca 100% Lua. Nenhum rebuild C++. CMakeLists nao tocado.

## Tasks Executed

- **P1 (Grupo 1 — Suavidade da borda)** — Done
  - `addDetail` ganhou parametro `biteScale` (linha do raio: `... * biteScale`), default 1.0 = comportamento atual.
  - `deriveShapeParams` deriva `nDetail` e aplica `jag = smoothness/100` (multiplica `nDetail`); quando `jag < 0.35` aplica 1 passada extra de `smoothMask` (2 passadas se `jag < 0.15`).
  - `readOpts`: `smoothness` (default 100) e `biteScale` mapeado por `biteScaleFrom` (piecewise) com **slider 50 -> 1.0 exato** (corrigi a formula do plano que dava 1.45 em 50, para garantir regressao zero).
- **P2 (Grupo 2 — Perfil de montanha)** — Done
  - Refactor central (comportamento-neutro): extraidos `deriveShapeParams(profile,opts,W,H)`, `buildSilhouetteMask(W,H,opts,p,setStatus)` e `dressMaskByRings(...)` do miolo de `synthesizeShape`. A sequencia de RNG entrando no loop de shapes e identica a anterior -> geometria identica nos defaults.
  - `applyMountainProfile(mask,W,H,opts)`: funcao pura que esculpe a(s) face(s) (N/S/L/O/todos) por onda triangular discretizada em degraus de `slope`, com jitter leve. `app.yield()` por face. Limpa com `fillHoles`+`despeckle`. Quando ligado, `deriveShapeParams` reduz `nDetail *= 0.3`.
- **P3 (Grupo 3 — Terracos/niveis)** — Done
  - Em `dressMaskByRings`: quando `opts.levels > 0`, divide `[1..dM]` em N faixas concentricas; cada nivel pega o label aprendido em profundidade representativa (`ringLabel` amostrado). `levels=0` mantem EXATAMENTE o caminho atual.
- **P3.5 (WFC x mascara — ISOLADA)** — Done, **LIGAVEL** (default OFF)
  - Em `synthesize`, apos `decode` e antes de `filterShapes`: se `opts.useSilhouetteMask` e nao for `path`, gera `buildSilhouetteMask` e zera celulas fora da mascara (`out=EMPTY`). Re-rotula por niveis se `levels>0`. Seguro (applyResult ignora EMPTY).
- **P4 (Grupo 4 — Previa ao vivo)** — Done
  - `previewMask` (cap 64x64, downscale, seed estavel) + `renderMaskPreview` (blocos `Image.blank(px,px,cor)` em cache + `:blit`, sem set-pixel). Paleta fixa por nivel, tudo sob `pcall`/`hasPreview`.
  - **Mecanismo primario = botao "Atualizar previa"** (garantido). **Ao vivo (melhoria opcional) ligado apenas nos controles discretos** `mtnOn` (check) e `mtnSides` (combobox), via `onchange`. **Sliders NAO disparam previa ao vivo** (evita re-render por tick de arrasto) — usuario mexe e clica no botao.

## UI

Novo box `dlg:box({orient="vertical", label="Esculpir silhueta (modo Formato novo)"})` inserido entre "Tamanho e contorno" e "Quantidade e distribuicao". Controles: smoothness, biteScale (sliders), mtnOn (check), mtnSides (combobox), mtnSpacing/mtnHeight/mtnSlope (sliders), levels (slider), wfcMask (check), botao "Atualizar previa" + imagem `silPreview`. Icones: `wave-square.svg`, `mountain.svg`, `layer-group.svg`, `eye.svg` (todos existem em `assets/svg/solid/`), com fallback itemid.

## readOpts (novos campos, defaults reproduzem o atual)

```
smoothness = dlg.data.smoothness or 100,
biteScale  = biteScaleFrom(dlg.data.biteScale or 50),   -- 50 -> 1.0
levels     = math.floor(dlg.data.levels or 0),
useSilhouetteMask = dlg.data.wfcMask or false,
mountain   = { enabled=false, sides="N", spacing=10, height=6, slope=2 } (por default)
```

## Regressao (zero por design)

- `smoothness=100` (jag=1.0), `biteScale=1.0` (slider 50), `mountain.enabled=false`, `levels=0`, `useSilhouetteMask=false` -> caminho identico ao original em Shape e WFC.
- `deriveShapeParams` reproduz `targetC` e `nDetail` originais; `buildSilhouetteMask` e copia fiel do loop antigo; `dressMaskByRings` com `levels=0` e byte-identico ao "vestir por aneis" antigo.
- Reroll e "Gerar p/ Paste" passam por `readOpts` -> herdam os novos campos automaticamente.
- Modo `path` (rio): Grupos 1-2 nao se aplicam (`generatePath` intacto); Grupo 3 (niveis) vale pelo dressing.

## Testing Instructions

Carregar o script no editor sobre um mapa com perfil que tenha amostras (idealmente com camadas/aneis para o Grupo 3). Para cada controle: gerar com defaults (deve bater com o resultado anterior), depois variar:
1. Smoothness 0 = bordas lisas; 100 = recortado. biteScale baixo = mordidas 1 tile; alto = lobos.
2. Perfil de montanha ligado (lado Norte) = serra na face norte; "Todos" = 4 faces; Altura/Espacamento/Inclinacao conforme descrito.
3. Niveis=3 com perfil em camadas = 3 aneis concentricos.
4. WFC + "Aplicar silhueta no WFC" = textura WFC recortada na forma. Se a borda ficar feia, deixar o check OFF (ja e o default).
5. "Atualizar previa" mostra a silhueta colorida por nivel; mexer em mtnOn/lado atualiza ao vivo; sliders requerem clicar no botao.

## Notes / Caveats

- **P3.5 ficou LIGAVEL** (nao desabilitada): a intersecao pos-decode e segura. Default OFF conforme plano; se em teste o recorte WFC ficar visualmente feio, basta nao marcar o check (fallback ja e o estado padrao).
- Previa ao vivo deliberadamente NAO ligada aos sliders continuos (risco de travar a UI por tick). So controles discretos atualizam ao vivo.
- A previa mostra apenas a FORMA da silhueta (nao roda o solver WFC ao vivo — caro demais), conforme o plano.
- Sem interpretador Lua no ambiente; validei por revisao manual + checagem heuristica de balanceamento de blocos (box/endbox 9/9, wrap/endwrap 22/22, nenhum `end` prematuro). Ordem de definicao dos novos locals respeita o escopo lexico.
- Nao toquei nas funcoes de doodad/stamp (feature em pausa).
```
