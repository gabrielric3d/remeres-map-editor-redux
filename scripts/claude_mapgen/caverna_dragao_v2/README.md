# Caverna de dragão, 2ª tentativa (interrompida no grounding)

Mesmo pedido da v1, numa conversa nova: "uma caverna no underground, então toda a área em
volta é montanha; 3 andares, todos conectados fazendo um loop; montanha com lava; caverna
de dragão. Comece pela estrutura…". O thinking estava desligado.

Esta versão é a melhor base para continuar a caverna: a estrutura dos 3 andares ficou
aprovada no esquema e foi colocada no mapa. Ela parou no meio do grounding.

**Dependências:** `numpy`, `scipy`, `Pillow`. **Mapa:** um `Untitled-1` novo (cliente
10.98), não salvo.

## Pipeline

```
fetchsets.py   →  sets.json            (peças de ~46 conjuntos de borda, via list_borders; precisa do RME)
run_layout.py  →  grid{8,9,10}.npy, marks{z}.json, shots/layout{z}.png
                  (layout.build + jag.jaggedize em cada andar)
build_tiles.py <z> [place]   →  tiles_struct<z>.json  (+ coloca no mapa com "place")
grounding.py   <z> [place]   →  tiles_ground<z>.json  (+ coloca no mapa com "place")
```

Crie a pasta `shots/` antes de rodar o `run_layout.py`.

| Script | O que faz |
|---|---|
| `rme.py` | Cliente MCP com sessão, leitura do `config.toml` e imagens salvas em `shots/`. CLI: `python rme.py <tool> '<json>' [nome_da_imagem]` |
| `borders.py` | A tabela do auto-border do RME (`ground_brush_arrays.cpp`) em Python: máscara de 8 vizinhos → nomes das peças (`n`, `cnw`, `dse`…) |
| `noise.py` | Value noise + fbm |
| `fetchsets.py` | Baixa as peças dos conjuntos de borda candidatos para `sets.json` |
| `layout.py` | **Estrutura:** grade 96x76 (+ margem de montanha de 12) em coordenadas locais (origem global 952, 962). Chão/montanha/lava por andar, corredores, pontos de conexão e rampas do loop |
| `jag.py` | Quebra retas de contorno (≥ MAXRUN+1 tiles) com dentes e entalhes de 2 tiles, sem quebrar as conexões |
| `run_layout.py` | Roda layout + jag nos 3 andares, salva as grades e a imagem de conferência |
| `build_tiles.py` | Grade → tiles do `place`: grounds + bordas + rampas vermelhas (8372–8379, uma por direção) e buracos "filled" (8559–8562, que leem como degrau descendo) por cima das bordas |
| `grounding.py` | **Grounding:** divide o chão em caverna / faixa de pé / rocha quente / manchas de terra / ninhos, entrelaça o dono das manchas e emite todas as peças de borda |
| `preview.py`, `preview2.py`, `patchtest.py`, `scratch_holes.py` | Mostruários temporários no andar 7 (x≈1900, y≈1900) para escolher sprites de montanha, lava, chão, rampas e buracos. Foram **desfeitos** depois |

## Onde parou

- A estrutura dos 3 andares foi colocada e conferida com grid.
- Corredores com no mínimo 3 tiles de largura. A ponte principal do andar 9 foi alargada.
- O andar 9 chegou a perder a ligação NW→WH, porque a regra 3x3 cortava um trecho
  estreito. Isso foi resolvido com um núcleo de corredor sem ruído em cada aresta.
- **Grounding começou:** a rocha quente quase sumiu (a faixa de 1 tile era removida pela
  regra 2x2) e foi alargada. O mapper interrompeu nesse ponto. Ele já tinha pedido para
  acelerar ("sem mais testes de amostra").
- **Não chegou aos detalhes:** fogo na margem da lava, pedras, ossos, ninhos, dragões.
  Para isso, o guia `estrutura.md` (lava em fendas estreitas, fogo só na margem) e o
  `detalhes.md` (clusters) já têm as regras.
