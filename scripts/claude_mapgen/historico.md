# Histórico: a sessão de 28/09/2026

Num dia só, o Claude foi ligado ao editor, aprendeu o estilo do mapper e construiu um mapa
inteiro sendo corrigido ao vivo. Este arquivo conta a sessão na ordem em que aconteceu.
Cada bloco tem o pedido do mapper, o que o Claude fez, o que saiu errado, a correção e a
regra que nasceu dela, com o lugar onde a regra ficou escrita.

Vale ler inteiro antes de mexer na geração: quase todas as regras dos guias vieram daqui,
e o motivo de cada uma só aparece na correção que a gerou.

---

## 1. Infraestrutura (antes de construir)

- **MCP no RME:** as tools do editor foram expostas por MCP, com porta fixa 7777 e token,
  para que o Claude Code de fora também possa mexer no mapa.
- **Ver o Claude trabalhando:** o mapper quis ver a construção "item por item, movendo a
  câmera". Daí vieram a animação (`assistant_live.cpp`), a câmera que segue, o cursor
  "Claude" e a regra de que tudo de uma chamada vira **um** undo. O `run_lua` também ficou
  animado, porque o Claude construía salas com scripts Lua e elas apareciam de uma vez.
- **Decisão do mapper: tudo em RAW.** O auto-border não reproduz o estilo dele (bordas
  quebradas e empilhadas à mão, chão de cor parecida por baixo, nada paralelo). Daí
  nasceram o `place`, o `list_borders` e o `read_region raw=true`.
- **Primeira comparação (mapper × Claude, numa caverna):** a do Claude tinha borda
  faltando (o quadrado do tile aparecendo), mancha em forma de caixa, fenda atravessando o
  meio e rochas grandes no chão aberto. A causa principal: o screenshot de tela inteira,
  reduzido a ~14 px por tile, não deixava o Claude ver os próprios erros. Daí vieram o
  `screenshot` com `zoom` e recorte, o `border_pieces` e o `check_region`.

## 2. Correção de bordas num mapa do mapper (`correcao_bordas_andar6/`)

Mapa `inferno.otbm`, andar 6, manchas de pedra cinza (45352) sobre lama (`frozen mud`).
O mapper pediu para corrigir as manchas que o Claude tinha feito antes.

| Tentativa | Erro | Correção do mapper |
|---|---|---|
| Borda cinza (set 250) em volta de tudo, como *inner* | "a borda deveria ir para fora, por cima da montanha" | O chão que domina põe a borda para fora |
| Borda grossa da lama (46) por cima da beira cinza no lado leste | As duas bordas se cortavam: traço solto e vão | O mapper mostrou a versão dele lado a lado |
| Leitura da versão do mapper com `read_region raw` | Acertou | — |
| Fim da linha grossa | A linha "terminava seca" | Canto grosso (`cne` 7084) empilhado no fim da linha, e o canto cinza sai |
| Mancha em x 4086 "corrigida" | Borda de baixo inteira faltando, tile de lama preso, forma ruim, e o `check_region` disse "limpo" | O `check_region` passou a exigir que **cada lado** do encontro esteja coberto. A mancha em si não foi refeita (a sessão passou para a floresta) |

**Regras que ficaram:**
- **Cada trecho do contorno tem UM dono.** Num lado a borda da mancha vai por cima do
  chão em volta; no outro, o chão em volta põe a borda dele por cima da mancha, e a borda
  da mancha sai dali.
- **A borda grossa faz a altura.** O jogo é sprite, e a profundidade vem das bordas mais
  grossas do terreno mais alto.

Estão em `scripts/claude_style.md` e em `piso-e-bordas.md` (técnica de borda).

Também: 6 caixões de pedra estavam pela metade. **Item grande é composto, uma peça por
tile**, e a propriedade de uma peça não vale para o tile vizinho. Isso também está no
`claude_style.md`.

## 3. As 773 capturas viram guias

9 agentes analisaram em paralelo as capturas dos mapas do mapper (ingame, old, showoff,
cidade). As notas por imagem estão em `scripts/claude_style/analise/`, e os padrões viraram
`estrutura.md`, `piso-e-bordas.md` e `detalhes.md`. Uns 25% das capturas eram escuras
demais para julgar bordas, e as notas dizem isso.

## 4. Floresta com casa de druida (`floresta_druida/`)

Pedido: "em um novo mapa, uma floresta e no centro uma casa bonita, bem temática de casa
de um druida". O mapper abriu o mapa novo (as tools não criam mapa).

### Chão (`forest.py`)
Seis grounds com ordem de altura (dirt < dark dirt < dark grass < grass < lush grass <
forest floor), bordas calculadas por tile como o auto-border faria, mas colocadas em RAW.
Um "quebrador de retas" deixa o contorno irregular. Na primeira versão ele quase não
mudava nada, por um bug corrigido na hora.

### Casa e paredes (`house.py` → `walls2.py`, `walls3.py`, `walls4.py`)
| Erro | Correção do mapper | Script |
|---|---|---|
| Corner em cima, pole embaixo, paredes N/W **em cima do piso** | "está bem errada a estrutura das paredes, veja pela brush WALL" | `walls2.py`: anel próprio de tiles em volta do piso; peça pelos vizinhos N/W como o wall brush (`wall_border_calculator.cpp`) |
| Piso de madeira debaixo da parede aparecendo pela janela | "o piso nunca vai no mesmo tile da parede quando é fora da casa" | `walls3.py`: trecho reto, janela ou porta fica com o chão **de fora** |
| Grama aparecendo dentro da sala nos cantos | "DENTRO DA CASA o PISO vai por baixo da parede" | `walls4.py`: tile onde a parede vira, com a sala na diagonal SE, fica com o **piso** |

A parte descoberta do tile de parede é a de baixo e a da direita. O mapper também lembrou
da exceção: parede quebrada com a grama invadindo a casa. Isso ficou só como proposta,
não foi feito. A regra está no `claude_style.md` (paredes).

### Vegetação (`trees.py` → `clusters.py`)
- **Erro:** árvores sorteadas tile a tile com a mesma chance e a regra "sem árvore
  vizinha". O resultado foi um padrão de repetição e distância visível.
- **Correção do mapper:** mostrou os clusters dele do mapa *Satan Citadel*, "imagina o
  jogador vendo essa repetição".
- **`clusters.py`:**
  - tira as árvores do `trees.py` (o toco fica);
  - copa densa no forest floor com a densidade oscilando pelo ruído, sem bloco 2x2;
  - 13 grupos compostos espalhados por Poisson, com raio pela densidade;
  - o mesmo grupo nunca se repete a menos de 11 tiles, e cada cópia sai espelhada ou
    girada, com ~15% das peças tiradas.
- **Detalhe:** 8313 (birch) e 8314 (dead tree) são **sem folha** e destoam da mata verde,
  então foram trocados por élficas e sycamore.

Está em `detalhes.md` (seção Clusters).

### Tufos de grama (`tufts.py` → `tufts_swap.py`)
`tufts.py` faz manchas irregulares (tamanho, alongamento e ângulo sorteados, mais densas
no meio), concentradas nas transições (beira da mata, trilha, pé de parede). Depois o
mapper pediu a família dele, **6216–6219, com o menos visível mais frequente** (6219 ~60,
6218 ~45, 6217 ~25, 6216 ~5). A troca foi feita no próprio mapa, nos mesmos tiles
(`tufts_swap.py`).

### Entrelaçamento de bordas (`weave.py` → `weave2.py` → `weave3.py` + `weave4.py`)
- **Erro:** "a borda do forest floor está completamente predominante". Ele era o chão mais
  alto, então ganhava em todo o contorno.
- **`weave.py`:** em ~20–25% do contorno (pedaços de 3–8 tiles, escolhidos por ruído) o
  dono se inverte e a borda da grama vai por cima.
- **"Faltou completar o entrelaçamento usando CORNERS"** → `weave2.py`: em cada ponto de
  troca, o canto da grama no tile vizinho, o canto do forest floor que invadia sai, e o
  canto da grama é empilhado sobre a borda do forest floor.
- **"Ali poderia adicionar um corner da borda do forest floor"**, num print com o shader
  Flat Colors ligado → `weave4.py` (fim da reta do outro dono, 8 pontos + o marcado) e
  `weave3.py` (caso inverso, 13 pontos).
- **Nasceu aqui a tool `set_display`:** o Claude liga sozinho a "grounding view" (Flat
  Colors + só grounds), porque sem ela as árvores escondiam as bordas.

Está em `piso-e-bordas.md`.

### Riacho e ponte (`stream.py`)
| Erro | Correção |
|---|---|
| A ponte virou "jangada" (barranco por cima das tábuas das pontas) | Tábuas 5768–5770 são ground; o riacho passa reto por baixo e as pontas não têm barranco |
| "Muito reto" | **Nunca mais de 2 peças retas seguidas** |
| Canto empilhado sobre a reta da margem (`s` + `cse`) | "isso nunca pode acontecer": vira lasca de barranco solta na água. A quebra vem **só da forma da água** (dente de 1 tile) |
| Trocar a reta por diagonal sem mudar a água | Fica emenda seca (testado e descartado) |
| "Essa parte do rio está muito igual" (escadinha diagonal regular) | Margem composta por padrões curtos que não se repetem em seguida: patamar, entalhe, baía pequena, baía, ponta, zigue-zague |
| Sortear baías e remendar | Alargava o riacho até virar lagoa. Baía só onde o riacho tem até 2 tiles e nunca junto com uma da outra margem |
| Depois de vários `undo 3` + reaplicar, sobraram 51 tiles de água e 27 peças de barranco | Causa não reproduzida. `stream.py fix` varre e limpa os restos |

Ids: água = sea 4608–4619; margem = conjunto 5 (4644–4655); beira = juncos 2773/2775/2779,
taboas 2774/2776, vitórias-régias 2755–2758, pedras 3329–3333. Está em `estrutura.md`
(Água).

### Estrada de terra (`trail.py`)
- **Pedido:** "está muito reto (as verticais e horizontais), lembre-se da regra".
- **Como saiu certo:** cada lado é gerado linha a linha e muda de coluna a cada 1–2
  linhas, com ritmo e direção próprios. A largura varia de 2 a 4 tiles, e a estrada abre
  em funil nas cabeceiras da ponte.
- **Erros no caminho:** a abertura na ponte virou uma "barra" atravessada, e corrigir uma
  reta criava uma ponta de 1 linha (também é defeito).
- **A saída:** gerar várias versões e ficar com a primeira sem nenhum defeito (foi a 8ª),
  em vez de remendar.

Está em `piso-e-bordas.md` (Caminhos).

## 5. Caverna de dragão, 1ª tentativa (`caverna_dragao_v1/`)

Pedido: "caverna no andar 8, montanha com lava, caverna de dragão, estrutura em loop;
comece pela estrutura e só passe aos detalhes se o grounding estiver bom; THINKING OFF
para comparar depois".

- **Estrutura:** 3 andares (8, 9, 10), cada um em loop, ligados por 4 rampas num loop
  maior. Rampa 8378 + 8379 no andar de baixo, buraco 459 no de cima.
- **Problemas:**
  - o `red stone mountain` usa itens que não são ground, e o `place` inteiro foi
    rejeitado;
  - os rios de lava saíram quadriculados;
  - montanhas soltas apareciam como pontos pretos.
- **Interrompida** pelo mapper no meio de uma correção.

## 6. Caverna de dragão, 2ª tentativa (`caverna_dragao_v2/`)

Mesmo pedido, numa conversa nova: "toda a área em volta é montanha, 3 andares conectados
fazendo um loop".

- **Ferramentas:** layout com numpy/scipy (`layout.py`), quebra de retas com dentes de 2
  tiles (`jag.py`), tabela de borda igual à do RME em Python (`borders.py`).
- **Mostruários temporários** (desfeitos depois) para escolher os sprites de montanha,
  lava, chão, rampas e buracos.
- **Estrutura dos 3 andares aprovada no esquema e colocada.**
- **Grounding** (`grounding.py`) começou: chão de caverna, faixa de pé, rocha quente,
  manchas de terra, ninhos. A rocha quente quase sumiu (a faixa de 1 tile era removida
  pela regra 2x2) e foi alargada.
- **Interrompida** no grounding. O mapper tinha pedido para acelerar ("sem mais testes de
  amostra"). Não chegou aos detalhes (fogo, pedras, ossos, dragões).

## 7. As regras, em uma tabela

| Regra | Onde está escrita |
|---|---|
| Tudo em RAW; `paint` só se pedirem | `SYSTEM_HEADER`, `claude_style.md` |
| Ler o mapper em `read_region raw` antes de construir algo parecido | `claude_style.md` |
| Um dono por trecho de contorno; borda grossa = altura | `piso-e-bordas.md` |
| Corner no fim de toda linha (nunca termina seca) | `piso-e-bordas.md`, `claude_style.md` |
| Entrelaçar: inverter o dono em ~20–25% do contorno, com corners nas trocas | `piso-e-bordas.md` |
| Nunca mais de 2 peças retas seguidas; sem escadinha regular; sem ponta de 1 linha | `piso-e-bordas.md` (Caminhos), `estrutura.md` (Água) |
| A quebra vem da forma do chão, nunca de canto empilhado sobre a reta | `estrutura.md` (Água) |
| Parede = anel próprio; peça pelos vizinhos N/W; chão de fora no reto, piso no canto | `claude_style.md` (paredes) |
| Vegetação e detalhes por clusters, densidade variando, sem repetir perto | `detalhes.md` (Clusters) |
| Tufos 6216–6219, o menos visível mais frequente | `detalhes.md` |
| Item grande = uma peça por tile, sempre todas | `claude_style.md` |
| Conferir com `check_region` + close-up em zoom 1 antes de dizer "pronto" | `SYSTEM_HEADER` |
