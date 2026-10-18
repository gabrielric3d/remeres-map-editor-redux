# Caverna de dragão, 1ª tentativa (interrompida)

Pedido do mapper: "uma caverna no andar 8, montanha com lava, caverna de dragão, a
estrutura deve ser em loop. Comece pela estrutura e só passe para os detalhes se achar
que a estrutura/grounding está bom o suficiente." O thinking estava **desligado** de
propósito, para comparar depois.

## Estrutura planejada

Três andares (8, 9, 10), cada um em loop, ligados por 4 rampas num loop maior:

```
8 ─S1 (leste)─ 9 ─S2 (sul)─ 10 ─S3 (oeste)─ 9 ─S4 (noroeste)─ 8
```

- Rampa: 8378 no andar de baixo, mais 8379 no tile ao sul. No andar de cima, o buraco 459
  no mesmo x,y, com o patamar em y-1.
- A área fica em torno de x 1000–1060, y 1000–1040 (`STAIRS` e `FLOORS` em `cave_gen.py`).

## Scripts

| Script | O que faz |
|---|---|
| `cave_util.py` | Lê grounds e bordas de `data/1098/*.xml`, tem a tabela de borda do editor (máscara de 8 vizinhos → peças) e um wrapper do MCP |
| `cave_gen.py preview [z]` | Desenho ASCII de um andar (ou de todos) |
| `cave_gen.py apply <z>` | Grounds + bordas + rampas do andar, em RAW. Grava `cave_z<z>.json` |
| `rme_mcp.py` | Cliente MCP (o mesmo da floresta) |

## Onde parou

- O `red stone mountain` usa itens que **não são ground**, e o `place` inteiro foi
  rejeitado (o `place` valida tudo antes de aplicar).
- Antes de gerar, testou de que lado fica a peça na borda interna da lava (set 18).
- O andar 8 saiu com contorno orgânico. O 9 tinha rios de lava cortando a montanha, e o
  10 ilhotas num lago de lava.
- **Os rios de lava saíram quadriculados**, e montanhas soltas (menos de 2 vizinhos
  montanha) apareciam como **pontos pretos**. A última correção, "derreter" esses tiles,
  foi recusada pelo mapper, que encerrou a tentativa e recomeçou numa conversa nova
  (`../caverna_dragao_v2/`). Essa correção **não está** no `cave_gen.py`.

O mapa usado era um `Untitled-1` novo e não foi salvo.
