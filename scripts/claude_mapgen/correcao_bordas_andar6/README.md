# Correção de bordas num mapa do mapper (andar 6)

Foi a primeira tarefa real pelo MCP: corrigir manchas de **pedra cinza** (ground 45352)
sobre **lama** (`frozen mud`) que o Claude tinha feito antes no mapa `inferno.otbm`, andar 6.
O mapa é do mapper e **não está no repositório**. Aqui ficam os scripts e o que foi
aprendido, porque a técnica ("um dono por trecho") vale para qualquer mancha.

## Conjuntos de borda envolvidos

| Set | De quem | Papel |
|---|---|---|
| 250 (45353–45363) | pedra cinza | borda macia, vai **para fora**, por cima da lama |
| 46 (7079–7090) | lama | borda **grossa** ("thick border"): faz a lama parecer mais alta |
| 44 (7067–7078) | lama | borda normal da lama |

## Scripts

| Script | O que faz |
|---|---|
| `fix_patch.py plan\|apply` | **1ª tentativa** (mancha da esquerda, x 4097–4103, y 3915–3919): tapa o buraco, tira todas as peças dos sets 250/46 e recoloca a borda cinza completa. Mais o lado da fenda que veio de `argv[2]`. Ficou errada (veja abaixo) |
| `fix_patches.py x0 y0 x1 y1 [apply]` | **A técnica certa**, lida da versão do mapper (x 4102–4108, y 3899–3903): oeste/norte com a borda cinza 250 por cima da lama; leste com a borda **grossa** 46 da lama por cima da beira cinza; sul com a borda normal 44 da lama. Sem `apply`, só imprime o plano. Foi usada em 4 manchas |
| `rme_mcp.py` | Cliente MCP (o mesmo da floresta) |

## O que deu errado e o que ficou certo

1. A 1ª versão pôs a borda cinza em volta de tudo e a fenda (46) no lado errado. O mapper
   respondeu: "a borda deveria ir para fora… a borda da montanha no lado direito deve
   manter para criar profundidade".
2. Pôr a borda grossa da lama por cima **sem tirar** a cinza daquele trecho fez as duas se
   cortarem (traço solto e vão).
3. O mapper mostrou a versão dele ao lado. Lida com `read_region raw=true`, ela tinha **um
   dono por trecho**: onde a lama manda, a borda cinza não existe. O mesmo ajuste foi
   feito à mão com um `place`:

   ```json
   {"z": 6, "description": "transicao do marrom no lado leste (como o mapper)", "tiles": [
     {"x":4101,"y":3915,"remove":[45355]}, {"x":4102,"y":3915,"remove":[45359]},
     {"x":4102,"y":3916,"remove":[45363]}, {"x":4103,"y":3917,"remove":[45356]},
     {"x":4102,"y":3918,"remove":[45361]}, {"x":4103,"y":3918,"remove":[45357]},
     {"x":4100,"y":3919,"remove":[45353]}, {"x":4101,"y":3919,"remove":[45353]},
     {"x":4102,"y":3919,"remove":[45357]},
     {"x":4102,"y":3917,"add":[7079,7090]}, {"x":4099,"y":3918,"add":[7074]},
     {"x":4100,"y":3918,"add":[7069]},      {"x":4099,"y":3919,"add":[7078]}]}
   ```
4. **Corner no fim da linha grossa:** no tile de lama à esquerda da ponta, o canto grosso
   `cne` (7084) é empilhado por cima da borda cinza, e o canto cinza sai. Sem isso, a linha
   "termina seca".
5. A mancha em x 4086–4088, y 3930–3933 "corrigida" ficou sem a borda de baixo, com um
   tile de lama preso num canto e com forma ruim (haste de 1 tile + "T"). Os erros foram
   apontados, mas **ela não foi refeita**: a sessão passou para a floresta. Mesmo assim o
   `check_region` tinha dito "limpo". Ele só olhava se havia alguma peça no tile. Depois disso passou a exigir que
   **o lado do encontro** esteja coberto (`n` cobre o lado de cima, `dse` cobre baixo e
   direita, canto pequeno não cobre lado nenhum).
6. Bônus: 6 caixões de pedra estavam pela metade (1410 + 1411, 1414...). Item grande é
   composto e precisa de todas as peças.
