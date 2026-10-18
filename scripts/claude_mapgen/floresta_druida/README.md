# Floresta com casa de druida

O primeiro mapa feito inteiro pelo Claude, todo em RAW. O mapper aprovou o resultado
depois de várias rodadas de correção (veja [`../historico.md`](../historico.md), seção 4).

## O resultado

- **Mapa:** [`mapa/claude_floresta.otbm`](mapa/claude_floresta.otbm), mais o
  `claude_floresta-waypoint.xml` (vazio).
- **Cliente:** 10.98 (OTBM v2, items 3.57). Abra com `File > Open` usando o cliente 10.98.
- **Área:** x 992–1055, y 992–1055, andar 7 (64x64). A casa fica no centro (~1024, 1022).
- **Conteúdo:**
  - manchas de forest floor na grama, com bordas entrelaçadas;
  - clareira com a casa do druida (anexo de ervas, nicho da cama, caldeirão, canteiros);
  - mata por clusters e tufos de grama;
  - um riacho atravessando de oeste a leste, com ponte de tábuas;
  - a estrada de terra da porta até a borda sul.

## Ordem de execução

Esta é a ordem em que os scripts foram aplicados para chegar ao `.otbm`. Todos aceitam
`preview` (imprime um desenho ASCII e não toca no mapa) e `apply`. Rode dentro desta pasta.

| # | Script | O que faz | Lê | Grava |
|---|---|---|---|---|
| 1 | `forest.py` | Chão: grama, lush/dark grass, manchas de forest floor, trilha de terra; bordas de cada ground calculadas por tile (ordem de altura `Z_ORDER`) e quebrador de retas | `grounds.json` | `forest_ground.json` (no `apply`) |
| 2 | `house.py` | Casa: piso, paredes (versão **errada**, corrigida nos passos 4–6), interior, caldeirão, canteiros | `forest_ground.json` | — |
| 3 | `trees.py` | Árvores e detalhes sorteados tile a tile (versão **descartada**: o passo 7 tira tudo) | `forest_ground.json` | — |
| 4 | `walls2.py` | Refaz as paredes como o wall brush: anel próprio, peça pelos vizinhos N/W | `house.py` | — |
| 5 | `walls3.py` | Tile de parede reta, janela ou porta volta a ter o chão **de fora** | `forest_ground.json` | — |
| 6 | `walls4.py` | Tile onde a parede vira, com a sala na diagonal SE, recebe o **piso** | — | — |
| 7 | `clusters.py` | Tira as árvores do passo 3 e põe copa densa + 13 tipos de cluster (Poisson). `reapply` pula a remoção | `trees.py` (recalcula) | — |
| 8 | `tufts.py` | Tufos de grama em manchas irregulares, nas transições. **Lê o mapa** para desviar das árvores | mapa | — |
| 9 | `tufts_swap.py` | Troca os tufos baixos por 6216–6219 com os pesos do mapper (não tem `preview`) | mapa | — |
| 10 | `weave.py` | Entrelaça: inverte o dono da borda em ~25% do contorno do forest floor (`apply [share]`, padrão 0.25) | `forest_ground.json` | `weave_flip.json` |
| 11 | `weave2.py` | Corners nos pontos de troca do entrelaçamento | `weave_flip.json` | — |
| 12 | `weave3.py` | Caso côncavo: canto do forest floor sobre a borda da grama de um trecho invertido | `weave_flip.json` | — |
| 13 | `weave4.py` | Fim da reta do outro dono: canto do forest floor para curvar | `weave_flip.json` | — |
| 14 | `stream.py` | Riacho, ponte e beira (3 `place` = **3 undos**). `fix` só limpa restos de versões antigas | `weave_flip.json`, mapa (`fix`) | `stream.json` (no `apply`) |
| 15 | `trail.py` | Refaz a estrada de terra com lados independentes, largura 2–4 e funil na ponte. **Lê o mapa** já no `preview` | `stream.json`, `weave_flip.json`, mapa | — |
| — | `runcheck.py` | Auxiliar vazio da medição de retas do riacho (ficou só o cabeçalho) | — | — |
| — | `rme_mcp.py` | Cliente MCP. CLI: `python rme_mcp.py <tool> '<json>'`; `--list` lista as tools | `config.toml` | `shot_*.png` |

Os scripts se importam uns aos outros (`forest` é a base; `house`, `trees`, `weave` e
`stream` são usados pelos seguintes). Por isso todos ficam na mesma pasta.

## Para reproduzir do zero

1. No RME, `File > New` com o cliente **10.98** e deixe a aba ativa. Ligue o servidor
   MCP no painel do Claude.
2. `python rme_mcp.py get_editor_state`: confira que é o mapa novo, **não** um mapa seu.
3. Rode a tabela acima em ordem: `preview` e depois `apply`. Os passos 3 e 7 só fazem
   sentido juntos (o 7 desfaz o 3).

O resultado não sai idêntico ao `.otbm`, por três motivos:
- vários scripts foram reaplicados durante a sessão com undo no meio;
- `tufts.py`, `tufts_swap.py`, `stream.py fix` e `trail.py` dependem do que já está no
  mapa;
- o riacho deixou restos de versões antigas, que o `fix` limpou.

O `.otbm` é a referência do resultado final.

## Ids usados (cliente 10.98)

| O quê | Ids |
|---|---|
| Grounds (com chances) | `grounds.json`, tirado de `data/1310/brushes/brushes.xml` (os ids batem com o 10.98) |
| Bordas de cada ground | `BORDERS` em `forest.py` (grass, dark grass, lush grass, forest floor, dirt…) |
| Água / margem / ponte | sea 4608–4619 / conjunto 5 (4644–4655) / tábuas 5768–5770 (são ground) |
| Beira do riacho | juncos 2773/2775/2779, taboas 2774/2776, vitórias-régias 2755–2758, pedras 3329–3333 |
| Paredes | `wood wall` (só h/v/corner/pole), portas e janelas em `house.py` / `walls2.py` |
| Tufos | 6216–6219 (pesos ~5/25/45/60); capim alto 10914–10916 na beira da mata |
| Árvores sem folha (evitar na mata verde) | 8313 birch, 8314 dead tree |

## O que dá para reaproveitar

- **Bordas com dono por tile** (`forest.borders_for`) e **entrelaçamento** (`weave*.py`):
  servem para qualquer par de grounds.
- **Margem por padrões que não se repetem** (`stream.py`, função das margens) e
  **caminho com lados independentes** (`trail.py`), ambos com medição de retas e várias
  sementes até sair uma versão sem defeito.
- **Clusters por Poisson** com template, espelhamento e distância mínima por tipo
  (`clusters.py`, `tufts.py`).
- **Paredes pela regra do wall brush** (`walls2.py`, com o chão certo de `walls3/4`).

Para virar biblioteca, faltam: tirar as coordenadas fixas (`X0, Y0, W, H, CX, CY`), passar
a área e os ids como parâmetros e trocar os JSONs intermediários por leitura do mapa.
