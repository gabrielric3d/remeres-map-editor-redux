# BORDAS — A ESPECIFICAÇÃO COMPLETA (ler ANTES de encostar numa borda)

> Fechado com o mapper em 22/11/2026, depois de um dia inteiro de correções.
> Palavra dele: **"é um código, não é arte, são regras"**. Tudo aqui é contável.
> Se ele apontar algo que eu não consigo medir, é porque me falta a regra: PERGUNTAR qual é.

## 0. Vocabulário
- Peças de um conjunto de borda: `n` `e` `s` `w` (RETAS), `dnw` `dne` `dsw` `dse` (DIAGONAIS),
  `cnw` `cne` `csw` `cse` (QUINAS). O nome diz **onde está o chão dono**.
- **"PINO" / "PININHO"** na fala do mapper = a QUINA pequena (`cnw` `cne` `csw` `cse`).
  Palavra dele: *"os pininhos, que são as quinas pequenas que ligam as coisas"*.
  "Pino no lugar de reta" = pus quina onde ia reta. **Os pininhos não são enfeite: são
  obrigatórios e calculáveis — ver R10.**
- "PININHOS" no R6 = o caso proibido de **quatro** deles num quadrado 2x2.
- **RETA HORIZONTAL** = `n` ou `s`. **RETA VERTICAL** = `e` ou `w`.

## 1. AS REGRAS (todas verificáveis por código)
Para cada tile VAZIO que encosta na mancha:

**R1 — reta + reta.** Duas retas da MESMA ORIENTAÇÃO encostadas. Não precisa ser a mesma peça:
`s`+`n` lado a lado já é erro; `e`+`w` uma embaixo da outra também.
`if (n|s em x,y) and (n|s em x+1,y)` → erro. `if (e|w em x,y) and (e|w em x,y+1)` → erro.

**R2 — QUADRADO = quina + duas retas perpendiculares.**
- `cse`(x,y) com `s`(x+1,y) e `e`(x,y+1)
- `csw`(x,y) com `s`(x-1,y) e `w`(x,y+1)
- `cne`(x,y) com `n`(x+1,y) e `e`(x,y-1)
- `cnw`(x,y) com `n`(x-1,y) e `w`(x,y-1)

**R3 — HEXÁGONO = reta entre duas diagonais** (a ponta em V simétrica).
- `s`(x,y) com `dse`(x-1,y+1) e `dsw`(x+1,y+1)
- `n`(x,y) com `dne`(x-1,y-1) e `dnw`(x+1,y-1)
- `w`(x,y) com `dsw`(x-1,y-1) e `dnw`(x-1,y+1)
- `e`(x,y) com `dse`(x+1,y-1) e `dne`(x+1,y+1)

**R4 — DUAS DIAGONAIS IGUAIS com a quina no meio.**
- `cse`(x,y) com `dse`(x,y+1) e `dse`(x+1,y)
- `csw`(x,y) com `dsw`(x,y+1) e `dsw`(x-1,y)
- `cne`(x,y) com `dne`(x,y-1) e `dne`(x+1,y)
- `cnw`(x,y) com `dnw`(x,y-1) e `dnw`(x-1,y)

**R5 — ESCADINHA / MINI ESCADA.** A mesma RETA repetida **3 vezes** com passo constante
(±2,±1) ou (±1,±2). Exemplo que ele pegou: `w` em 1290,1250 · 1289,1252 · 1288,1254
(motivo `dnw`+`cnw`+`w` descendo três vezes).
Eu acrescentei por conta própria os passos (±1,±1), (±2,0) e (0,±2) — 3 retas iguais
alinhadas e igualmente espaçadas numa beirada plana (`n dnw n dne n`) são a mesma repetição
mecânica. **ISSO É INFERÊNCIA MINHA, ELE AINDA NÃO CONFIRMOU — PERGUNTAR.** Custa pouco:
com esses passos proibidos ainda sobram ~1,5% das formas sorteadas.

**R6 — PININHOS.** O arranjo EXATO do catálogo dele em `1268,1251`:
`cse`(x,y) + `csw`(x+1,y) + `cne`(x,y+1) + `cnw`(x+1,y+1) — os quatro bicos convergindo no
MESMO vértice (x+1,y+1), formando um quadradinho.
**CUIDADO, ERREI ISSO:** eu tinha escrito "4 quinas quaisquer num 2x2" e isso é muito mais
severo — travou o solver em configurações perfeitamente válidas por horas. Duas, três quinas
num 2x2 são normais.
E note: **pela R10 esse arranjo é impossível**. As quatro quinas viriam todas do vértice
(x+1,y+1), e a R10 só põe quina em quadrante NÃO encostado por segmento; para os quatro
ficarem livres o vértice teria de ter zero segmentos, e aí a R10 nem dispara. Ou seja: quem
deriva os pininhos pela R10 nunca cria pininhos. A regra só existe para quem põe quina à mão.

**R7 — COBERTURA.** Todo lado em que o vazio encosta na mancha precisa de peça que cubra
aquele lado (`n`/`dnw`/`dne` cobrem o norte, etc.). **0 falhas, sempre.**

**R8 — sem PINO DE CHÃO** (tile vazio com mancha em 3 lados ou em lados opostos) e
**nada fino** (língua/cabo de 1-2 tiles).

**R9 — A LINHA TEM QUE CASAR NA DIVISA (a regra que faltava, ensinada em 22/11 à noite).**
Cada peça desenha linha em lados inteiros do seu tile:
`n`=cima · `s`=baixo · `e`=direita · `w`=esquerda · `dnw`=cima+esquerda · `dne`=cima+direita ·
`dsw`=baixo+esquerda · `dse`=baixo+direita · **as QUINAS (`c...`) não desenham lado nenhum**
(são só o pingo do canto).
**Entre dois tiles VAZIOS vizinhos, ou os dois desenham a linha daquela divisa, ou nenhum.**
- `arm(A,direita) ~= arm(B,esquerda)` com A e B vazios → ERRO.
- `arm(A,baixo) ~= arm(B,cima)` → ERRO.
Medido: **0 violações** no gabarito 1170,1242 dele (e as 4 do bloco 1131,1243 são peças da
receita do pico). É a regra mais quebrada por mim: deu **8 erros** no meu primeiro bloco e é
a explicação dos TRÊS jeitos que ele usou para me mostrar o problema:
- **"você esqueceu algumas bordas"** = pus a quininha onde o vizinho já tinha braço apontando
  para mim; a quina não fecha a linha. (1309,1249 · 1311,1252 · 1318,1253 — ele desenhou por
  cima, em *teal leaves*, a peça certa: `dse`, `e`, `w`.)
- **"pirulito"** (ele marca com o doodad 46821, candia lolipop) = a linha sai para o vazio e
  **morre**: um toco de borda pendurado. Mesmo erro, visto de fora.
- **"duas retas seguidas"** = o conserto ÓBVIO do R9 ali criaria R1 (ele desenhou os dois `w`
  e os dois `s` em teal para mostrar). Ou seja: aquele trecho é beco sem saída, resolve-se com
  DIAGONAL (ex.: `dsw`+`dnw` em vez de `w`+`w`) ou mexendo no chão.

**R10 — OS PININHOS SÃO CALCULADOS, NÃO ESCOLHIDOS (ensinado em 22/11, ele mesmo pôs os 14
que faltavam no meu bloco).** Depois que as RETAS e DIAGONAIS estão postas, elas desenham
segmentos nas divisas. Em cada **vértice** do grid (canto onde 4 tiles se encontram):
- segmentos incidentes: N = divisa esquerda do tile (vx,vy-1) · S = divisa esquerda de (vx,vy) ·
  O = divisa de cima de (vx-1,vy) · L = divisa de cima de (vx,vy).
- quadrante **encostado**: NO se (N ou O) · NE se (N ou L) · SO se (S ou O) · SE se (S ou L).
- **Se o vértice tem pelo menos um segmento, TODO quadrante VAZIO e NÃO encostado recebe a
  quina que aponta para o vértice**: tile a NO → `cse`, a NE → `csw`, a SO → `cne`, a SE → `cnw`.
Conferido por código: gabarito 1170,1242 → **27 quinas previstas, 0 faltando, 0 sobrando**;
meu bloco 1315,1248 depois da correção dele → **23/23**. (No 1131,1243 faltam 8, e são as
duas pontas de pico daquele bloco — ver abaixo.)
Consequências:
- Linha que termina no vazio (grau 1) tem DOIS quadrantes livres → leva **dois pininhos**,
  um em cada tile do outro lado da ponta. É isso que ele chama de "ligar as coisas".
- Canto de 90° da borda (grau 2 em L) tem UM quadrante livre → um pininho (é a quina do
  auto-border de sempre).
- Trecho reto (grau 2 alinhado), grau 3 e grau 4 → nenhum pininho.
- **A RECEITA DO PICO SAI DAQUI**: `cse`(-1,-2) e `csw`(0,-2) da ponta norte são exatamente os
  dois pininhos que tampam a ponta da linha vertical formada por `dsw`(0,-1)+`e`(-1,-1).
  Não é fórmula decorada, é R9 + R10.

**O que NÃO é regra (conferido, não inventar de novo):**
- quina colada em reta (`csw`+`w`, `cse`+`e`, `s`+`csw`) **PODE** — o gabarito dele tem 24 casos.
- "ponta solta" da linha **existe** no trabalho dele (18 vértices de grau ímpar em 1170,1242):
  são as receitas de pico. Não é erro por si só.

**O QUE PODE (não confundir):** `reta + DIAGONAL + reta` pode. Duas quinas coladas podem.
Diagonais simplesmente encostadas podem (ele tem dezenas). Peça apontando para o vazio pode
(ele tem 31 num bloco) — desde que a cobertura esteja completa e a peça não fique isolada.

## 1.5 A LIBERDADE QUE EU NÃO TINHA ENTENDIDO (descoberto 22/11, conferido no gabarito)
**A PEÇA NÃO É FUNÇÃO DA FORMA.** Eu passava o dia tentando consertar a FORMA porque achava
que cada tile vazio só podia receber a peça que o auto-border poria. Errado. Para cada tile
vazio eu ESCOLHO a peça; a única obrigação é a COBERTURA (R7):
- contato ao NORTE → cobre `n` **ou** `dnw` **ou** `dne`  (as 3 valem, escolha livre)
- contato ao SUL   → `s` | `dsw` | `dse`
- contato a LESTE  → `e` | `dne` | `dse`
- contato a OESTE  → `w` | `dnw` | `dsw`
- dois contatos perpendiculares (N+O, N+L, S+O, S+L) → **forçado** na diagonal correspondente
- contato só na diagonal (os dois lados vizinhos vazios) → a quina (`cnw`...), **ou** uma peça
  que encoste naquele canto (ele usa `e` para um contato SE, `dsw` para um SW, `n` para um NE:
  1169,1237 · 1174,1238 · 1167,1240 no gabarito).
É assim que ele quebra uma beirada reta: a borda de baixo do bloco dele em y1246 é
`dnw dne dnw n dne` — diagonais apontando para o vazio, uma reta só. Chão reto, borda
nenhuma reta repetida. **Com essa liberdade, achar zero erros é um problema de escolha de
peça, não de redesenhar a forma.**

### COMO A R9 AMARRA AS ESCOLHAS (os padrões dele saem sozinhos)
- **Beirada reta** (ex.: chão em baixo, fila de tiles em cima, todos com contato S):
  R1 proíbe duas retas coladas, e a R9 obriga a diagonal a ter par. Sai o padrão dele:
  **`dse dsw` (par de diagonais se olhando) + `s` + `dse dsw` + `s` ...** — no máximo 1/3 de
  retas. Exatamente a fila `dnw dne dnw n dne` de 1168-1172,1246.
- **Beirada vertical** (contato a leste): a corrente é **`e` → `dse` → `dne` → `e` → ...**
  (`e` não tem braço, `dse` passa o braço de baixo para o `dne` de baixo, que fecha).
  Nunca `e` em cima de `dne`: o braço de cima do `dne` fica sem par.
- **Onde sobrevive a quina**: num contato só diagonal, a quina só pode ficar se NENHUM dos
  dois vizinhos do canto tiver braço virado para ela (para um contato `se`: o de cima tem de
  ser `s` ou `dse`, o da esquerda `e` ou `dse`). Por isso o chão dele tem tanto degrau de 2:
  cada degrau grande gera um tile de contato só diagonal, e é lá que moram as 41% de quinas.
- Histograma dele (1170,1242, 66 peças): as 12 peças aparecem quase igualmente
  (cne 6, cnw 7, cse 7, csw 7, dne 5, dnw 6, dse 6, dsw 6, e 4, n 5, s 4, w 3).
  Se a minha solução der 50%+ de diagonais, a FORMA está lisa demais (poucos degraus de 2).

### A GRAMÁTICA DO CONTORNO (regra do chão, demonstrada pelo solver em 23/11)
Descreva a beirada como uma sequência de valores (uma coluna por linha, ou uma linha por
coluna) e olhe os PASSOS entre vizinhos. Vale:

> **Um passo de ±1 tem de ficar ISOLADO: o passo anterior e o seguinte precisam ser 0 ou de
> sinal oposto. Dois passos não-nulos seguidos no mesmo sentido só valem se os DOIS forem ≥2.**

Por quê (e não tem escapatória): onde a beirada anda para fora, o tile do canto de dentro
fica com dois contatos perpendiculares → **diagonal FORÇADA**. Se o passo seguinte no mesmo
sentido for de 1, a próxima diagonal forçada cai na diagonal da primeira, e o pininho entre
as duas é forçado pela R10 → **R4, sempre**. Com passo ≥2 elas ficam separadas e dá certo.
Confirmado no gabarito 1170,1242: oeste `-2,+1,0,-1,0,-2,+1,0` · leste `0,0,+1,0,+1,0,-1` —
**todo ±1 isolado**, e os ±1 colados em outro passo são sempre de sinal oposto (isso é o
pico de 2 tiles dele).

**O FIM DA MANCHA CONTA COMO PASSO.** Na última linha (ou primeira) o chão de fora fecha por
baixo, e o tile da quina fica com diagonal forçada do mesmo jeito. Então **um passo de ±1
para dentro da última linha é proibido**. No código: acrescente um passo virtual de ±3 no
fim (e no começo) da sequência antes de rodar a gramática.

## 1.6 O SISTEMA FECHADO (é só isto, nesta ordem)
1. **Forma** do chão (livre, mas sem pino de chão nem língua fina — R8).
2. **Retas e diagonais**: uma escolha por tile de contato, cobrindo todo contato (R7) e
   casando em toda divisa vazio-vazio (R9). É AQUI que mora a única liberdade real.
3. **Pininhos**: saem sozinhos do passo 2 pela R10. Nada de escolher.
4. **R1-R6**: as regras de gosto por cima (reta+reta, quadrado, hexágono, 2 diagonais,
   escadinha, 4 pininhos num 2x2). Se alguma bater, volta ao passo 2 (outra escolha) ou ao 1.
Com isso o verificador dá zero por construção; sobra o que é gosto: a SILHUETA.

## 1.7 CHÃO CONTRA CHÃO (mais de um dono) — começado em 23/11
Mapa "estudo de bordas do claude.otbm", andar 7, o **pathing longo** dele: chão de x1336-1552
/ y1050-1276 com três chãos e **nenhuma** borda posta:
- `mushroom sponge` (rosado, ids 18053-18055) → borda **165**, "outer, to all"
- `mold floor` (escuro, ids 17647-17652) → borda **330**, "outer, to all"
- `unknown5` (verde, ids 23324-23326) = **o pathing, 2572 tiles, NÃO TEM conjunto de borda**

**Quem borda quem:** "outer" = as peças caem nos tiles do VIZINHO. Como o path não tem
conjunto, ele nunca é dono: as peças dos dois vizinhos caem **em cima do path**. Então, para
cada par, o modelo é o mesmo de sempre — "chão" = o dono, "vazio" = quem recebe:
- 165: chão = mushroom, recebe = path
- 330: chão = mold, recebe = path
Cada conjunto é resolvido SEPARADO (R1-R10 valem dentro de um conjunto só); um tile de path
encostado nos dois carrega peça dos dois, e cada linha termina no encontro triplo capeada
pelos seus próprios pininhos (R10). Ficou bom: ver x1444-1480, y1130-1157.

**QUEM É O DONO SE DECIDE OLHANDO O SPRITE DA PEÇA — a lição mais cara do dia (23/11).**
Eu pus **mold como dono** sobre o mushroom (peças 330 nos tiles rosados) e o resultado foi
*"100% errado e muito feio"*, com razão: **as peças da 330 são DECAL DE TILE INTEIRO** —
cobrem o chão rosado por completo, então a divisa vira uma franja escura mastigada de 1 tile
e parece que o mold cresceu, não que existe uma borda. Já as peças da **165 (mushroom) são um
FIO FINO**: põem só um contorno escuro em volta do vizinho e não comem nada.
> **Antes de escolher o dono, olhe o que a peça desenha.** Conjunto de fio fino → pode ser o
> dono de tudo. Conjunto de tile cheio → só use onde o vizinho aguenta ser coberto (ou não
> use; prefira que o OUTRO lado seja o dono).
No canvas de 1226,1281 o certo ficou: **mushroom dono sobre mold E sobre path** (165 contorna
os dois), e a 330 só onde não há escolha — na divisa mold↔path, porque o path não tem
conjunto nenhum. Conferido com os dois olhos, lado a lado.

**REFINAMENTO (canvas de lava, 1222,1314):** decal de tile cheio não é sempre ruim — depende
do CONTRASTE. Testei as duas variantes lado a lado no mesmo canvas:
- `red spikes` (156) como dono sobre o dirt: tufos escuros bonitos nas peças horizontais, mas
  as peças verticais (`e`/`w`) são um **fio fino azulado** que lê como costura/rachadura. A
  mancha fica com cara de retângulo costurado.
- `dirt mountain` (125) como dono sobre os spikes: decal de tile cheio, mas como as duas cores
  são escuras ele **derrete** a mancha no terreno. Ficou muito melhor.
> Regra prática: decal de tile cheio **sobre chão de cor parecida = ótimo** (vira degradê);
> **sobre chão de cor contrastante = péssimo** (parece que comeram o chão).
> E olhe as peças RETAS e as VERTICAIS separado: um mesmo conjunto pode ter tufo bonito no
> `n`/`s` e um fio feio no `e`/`w`.

**Mancha estreita:** onde a mancha tem só 2-3 tiles de largura e recebe decal dos dois lados,
ela some (a lava ficou estrangulada e pareceu partida em duas). Mancha que vai receber borda
de tile cheio precisa de **4+ tiles de largura**.

**E a estrutura "um dono por trecho" está confirmada no mapa dele:** nas divisas
`sand|jungle grass` (x1010-1166, y975-1229), de 423 cruzamentos **407 têm peça de um lado
só**; e o dono TROCA ao longo do contorno (140 trechos de um lado, 267 do outro).

**ESTADO EM 23/11:** área inteira bordada e verificada — x1336-1552 / y1050-1276,
**1966 peças, 0 erros** nos dois conjuntos (R1-R10). Anéis: 165 = 361 tiles (path encostando
em mushroom), 330 = 933 tiles (path + mushroom encostando em mold).

## 1.8 A SILHUETA DELE, MEDIDA (canvas 1290,1283 — 23/11)
Ele refez o mesmo canvas à mão (51 tiles diferentes do original) e eu medi os passos das
beiradas, linha a linha. O vocabulário dele é:
- **passo 0 é o mais comum** (trechos retos de 2-4 linhas),
- **±1 é o passo de trabalho** (6 na mancha de mold, 6 na de path), sempre isolado entre zeros,
- **±2/±3 aparecem pouco** (1-2 por beirada),
- e **um ou dois saltos grandes por mancha** (+6, +9, −5): são as **línguas e os entalhes**,
  não erro — é o que dá caráter. Ex.: entalhe de 2 tiles de mushroom dentro do path em
  1284-1285,1285 e língua de mold de 2 tiles em 1305-1306,1280.
**Eu estava gerando forma errada**: enchia de ±2 em quase toda linha (escadinha de degrau 2),
que é legal pela gramática mas não é o traço dele. O certo é **reto, reto, um ±1, reto** e de
vez em quando um salto grande deliberado.
1. **Desenhar o chão.** Mexer à vontade: adicionar E remover tiles. *"não tem problema mexer
   no desenho do chão, isso é sempre bom"*. A ordem de prioridade dele:
   **REGRAS > silhueta > forma original.**
2. **Borda = auto-border como base**, mais a RECEITA DO PICO nas pontas. O mapper NÃO usa
   auto-border puro: *"sim eu não uso auto border, isso é ruim, fazemos tudo manual"*.
3. **Conserto à mão só pode ACRESCENTAR cobertura, nunca tirar.** Trocar reta por quina para
   "zerar" é ERRO GRAVE: tira a peça que cobre o contato e abre buraco.
4. **Não entregar com erro.** Se travar, dizer "travei aqui" e pedir ajuda.

### RECEITA DO PICO (relativa ao TILE DE CHÃO da ponta, em (0,0))
Ponta de 1 tile:
- norte: `dsw`(0,-1) `e`(-1,-1) `cse`(-1,-2) `csw`(0,-2)
- leste: `dnw`(1,0) `s`(1,-1) `csw`(2,-1) `cnw`(2,0)
- sul:   `dne`(0,1) `w`(1,1) `cnw`(1,2) `cne`(0,2)
- oeste: `dse`(-1,0) `n`(-1,1) `cne`(-2,1) `cse`(-2,0)
Ponta de 2 tiles (norte, chão em (0,0) e (1,0)): mesmas 4 peças da receita norte; o `s` e a
`csw` da direita ficam como o auto-border põe. Girar os nomes junto com as posições —
**errei isso duas vezes pondo a borda ao contrário**.
Ponta boa é de **2 tiles**; ponta que não fecha, **corta fora** (foi o que ele fez).

### QUANDO UM CONSERTO ESTRAGA O VIZINHO
Resposta dele: **mexe mais no chão, adicionando e removendo**, e redesenha a borda em volta.
E a dica que eu ainda não usei direito: *"você poderia ter ADENTRADO um pouco no lugar de
ter feito várias retas"* — quando um lado vira um amontoado de retas, **abrir um entalhe
para dentro da mancha** em vez de emendar reta.

## 2.5 O MOTOR ESTÁ SALVO EM CÓDIGO — NÃO REESCREVER DO ZERO
`claude_style/bordas_lib.lua` tem tudo: verificador, solver com R9/R10 por dentro, conserto
de chão (armadilhas/pinos/picos), gramática do contorno em DP, gravação e limpeza de peças.
Carregar no começo da sessão (o `_ENV` é obrigatório):
```lua
local src = app.readFile("claude_style/bordas_lib.lua")
load(src, "bordas_lib", "t", _ENV)()
```
Depois é só `B.defSet(...)` para cada chão, `B.load`, `B.cleanGround`, `B.writeGround`,
`B.solveSet`, `B.clearPieces`, `B.apply`, `B.verify`. O cabeçalho do arquivo tem a receita.

## 3. O MÉTODO QUE DÁ ZERO (resolvido em 22/11 — USAR SEMPRE ESTE)
Guloso, annealing e basin hopping travam em 1-4 erros: todos remendam uma forma pronta.
**O certo é construir.** Receita, toda em `run_lua`, NADA é colocado antes de dar zero:

1. **Verificador** (R1..R8) sobre as peças, como função pura de `S` (chão) e `A` (peças).
   Calibrar rodando nos gabaritos dele: **tem de dar 0** em 1170,1242 e 1131,1243, e tem de
   ACUSAR na cópia não corrigida ao lado (1186-1195,1242: R4 + escadinha de 4 `n`).
2. **Solver exato de peças**: lista de tiles vazios com contato, cada um com suas opções
   (seção 1.5), backtracking na ordem row-major; depois de atribuir um tile, checar só as
   instâncias de regra que CONTÊM aquele tile (3x3 em volta para R1-R4/R6 + os 16 vetores de
   passo para R5 + as 4 divisas para **R9**). Dá ~2 ms por forma; budget de 4000-6000 nós
   corta os casos explosivos. **R9 só pode ser checada quando os DOIS tiles da divisa já
   estão decididos** (atribuído, ou fora do anel e portanto sem peça nenhuma).
   Num contato só diagonal as opções são 4: a quina, as duas retas daquele canto e a diagonal
   (ex.: contato `se` → `cse` | `e` | `s` | `dse`) — ele usa todas (tabela medida no gabarito).
3. **Busca de forma**: sortear o contorno como l(y)/r(y) (passos -2..+2, largura 4..9), jogar
   fora as que têm pino de chão, e deixar o solver JULGAR. Em 2892 formas sorteadas, **46
   aceitam borda com zero** (~1,5%) — não adianta insistir numa forma: troca-se de forma.
4. Escolher entre as viáveis pela silhueta (mais passos ≠ 0 = contorno irregular, área
   parecida com a do bloco dele) e, entre as soluções de peça, a que tem a **proporção de
   peças dele: 24% retas / 35% diagonais / 41% quinas** (medido em 1170,1242, 66 peças).
5. Só então colocar, e rodar o verificador DE NOVO lendo o mapa (não a memória do solver).

### CONSERTAR UM CONTORNO JÁ DESENHADO (o pathing dele, 23/11)
Quando ele entrega um desenho pronto ("a silhueta está OK, mexa no chão"), não redesenhe:
**programação dinâmica**. Sequência original da beirada → DP sobre (valor, último passo) que
obedece a gramática acima e minimiza |novo − original| (peso 2) + penalidade para zigue-zague
(4) e para padrão repetido (3-5). Resultado no piloto: desvio máximo de **2 tiles**, só
**5 tiles de chão trocados**, silhueta preservada e as duas bordas fechando em zero.
Se mesmo assim o solver travar, ele diz em QUE tile travou → é ali que a gramática ainda
está errada (foi assim que eu descobri as duas regras de passo).

### ÁREA GRANDE (o pathing dele, 19.580 tiles, 23/11)
O que fez a coisa virar viável:
1. **Ordem dos tiles no solver: por TRECHO CONECTADO, andando pelo contorno (BFS de 8
   vizinhos), nunca linha a linha.** Varrendo em row-major eu misturo pedaços de borda
   independentes e um conflito no fim desfaz tudo: **400.000 nós sem fechar**. Com a ordem
   por trecho: **2.328 nós, 0,5 s**, mesma área, mesmas regras. É a mudança mais importante
   de performance que eu fiz.
2. **Conserto do chão em massa, antes de resolver**: detector de armadilha (duas diagonais
   FORÇADAS do mesmo tipo vizinhas na diagonal) + pino de chão, com descida gulosa local
   (tenta virar cada tile de beirada num raio de 3-6 e fica com o que reduz as armadilhas na
   janela). Na área dele: **132 armadilhas + 3 pinos → 0**, mexendo só ~20 tiles no saldo
   (mush 11155→11177, mold 5732→5731, path 2693→2672). A silhueta não muda.
3. Quando o solver trava num ponto, teste a janela local isolada: se ela fecha sozinha, o
   problema é a EMENDA (ordem/faixa), não o chão.
4. **Não faça limpar+colocar de milhares de tiles numa chamada só**: o editor fica sem
   responder por mais tempo que o limite da ferramenta (a chamada termina, mas eu fico cego).
   Separe: resolver (guarda em global) → limpar → colocar → verificar.

### DUAS COISAS QUE O MEU CONSERTO DE CHÃO AINDA NÃO PEGA (23/11)
1. **Pico de 1 tile** (tile com um único vizinho ortogonal da mesma classe): o solver trava,
   porque a saída é a RECEITA DO PICO, que põe peça a 2 tiles de distância, fora do anel que
   eu monto. Enquanto eu não implementar o anel estendido, **detectar e remover picos de 1**
   (contar vizinhos ortogonais iguais; `<=1` = pico).
2. **Trecho reto de 6+** entre dois chãos: nem o solver nem o detector de armadilha reclamam,
   mas o `check_region` sim, e fica feio. Rodar `check_region` DEPOIS de resolver e quebrar os
   trechos variando a beirada (um ±1 isolado, ou um recuo de 2-3 tiles).
**Falso positivo do `check_region`:** ele chama de "floating border piece" os pininhos de
ponta da R10 (o par que tampa uma linha que morre no vazio). São CERTOS — foram esses que o
mapper pôs à mão no meu bloco de forest. Não "consertar".

### Armadilhas do `run_lua` (custaram uma rodada inteira)
- **Globais PERSISTEM entre chamadas de `run_lua`.** Instale a biblioteca uma vez
  (`_G.B = {...}` com regras, solver, verificador) e depois só chame funções: economiza
  muito e deixa o trabalho grande viável.
- O solver tem de checar a R10 **por dentro** (pininho derivado entra no R2/R4/R6). Checar só
  no fim não adianta: ele acha soluções que depois estouram. Com cache dos pininhos no 3x3
  em volta do tile atribuído, fica em ~0,15 ms por nó.
- `tile.ground = 0` **dá erro** ("must be between 1 and 65535"). Para apagar: `tile.ground = nil`.
- **Não chamar `app.transaction` dentro do `run_lua`**: a ferramenta já embrulha tudo. Se o
  código chama e um erro acontece lá dentro, a transação é desfeita **em silêncio** — o script
  "roda", imprime, e o mapa não muda nada.

## 4. MAPA DE REFERÊNCIA — "estudo de bordas do claude.otbm", andar 7
### [MAPPER] catálogos e trabalhos dele — referência, NÃO MEXER
- `1083,1196` — as 8 combinações proibidas (reta+reta e diagonal+quina+diagonal)
- `1084,1250` — escadinha e ondinha
- `1082,1264` — quadrado (reta+quina+reta), 5 exemplos
- `1081,1276` — jeitos elegantes de curva (borda 236 earth)
- `1161,1207` — o que ele NÃO faz: a ponta em V (hexágono)
- `1163,1257` · `1146,1263` · `1155,1263` — receitas de pico
- `1268,1251` — PININHOS (4 quinas em 2x2)
- `1218,1227` — invasão de areia na grama
- `1039,1044` — ilha grande dele (areia, borda 338)
- `1131,1243` e `1170,1242` — blocos MEUS corrigidos por ELE (dão 0 no verificador: usar
  como gabarito)
- `1193,1222` — o bloco original dos exercícios

### CANVAS DE CHÃO CONTRA CHÃO (23/11)
- `1226,1281` [CLAUDE] — mushroom + mold + path. 0 erros. Dono: **mushroom** (fio fino)
  contornando mold e path; 330 só na divisa mold↔path.
- `1290,1283` [MAPPER] — o mesmo canvas refeito à mão por ELE, **só chão, sem borda**.
  É a melhor referência de SILHUETA que eu tenho (ver 1.8). Medi: está a **6 tiles** de
  fechar — 1291,1272 · 1292,1277 · 1302,1280 · 1281,1285 · 1301,1288 (todos ±1 não isolado
  ou ±1 na última linha) e 1288,1290 (pino de chão, língua de 1 tile). Com esses 6 trocados
  as duas bordas fecham em zero. **NÃO MEXI — é dele.**
- `1222,1314` [CLAUDE] — dirt mountain + red spikes + lava. 0 erros, check_region limpo.
  Dono: **dirt (125)** sobre tudo; 156 só onde a lava encosta nos espinhos.
- `1248,1314` — a cópia virgem do mesmo canvas, deixada para ele.
- `x1336-1552 / y1050-1276` — **o pathing grande. ESTÁ ERRADO E FEIO, NÃO USAR DE REFERÊNCIA.**
  As regras fecham (0 erros), mas escolhi o dono errado: a borda 330 (decal de tile cheio)
  ficou em cima do mushroom rosado e comeu o chão. Para consertar: inverter o dono
  (mushroom passa a contornar mold e path) e refazer — ~2 min de máquina com o motor.

### [CLAUDE] trabalhos meus — podem ter erro, NÃO usar como referência
- `1315,1248` — bloco de 22/11. 1ª versão: 0 em R1-R8 mas **8 erros de R9** (ele corrigiu:
  pirulitos em 1310,1246 e 1314,1252; bordas esquecidas em 1309,1249 · 1311,1252 · 1318,1253;
  duas retas em 1319,1246-47 e 1319-1320,1250). 2ª versão (a que está no mapa): forma nova
  `1245:1314-1315 | 1246:1314-1317 | 1247:1312-1318 | 1248:1310-1318 | 1249:1311-1318 |
  1250:1311-1316 | 1251:1312-1316 | 1252:1312-1314`, 44 tiles, 38 peças, **0 no verificador
  completo (R1-R8 + R9)**, proporção 26/50/24. Falta o aval dele.
  **COMO ELE MARCA O ERRO NO MAPA:** doodad `46821` (candia lolipop) = "aqui não tem conserto,
  tire tiles"; peças de *teal leaves* (borda 70: `20884 s` `20889 e` `20890 w` `20891 dse`)
  desenhadas por cima = "a peça que falta aqui é esta" / "estas duas são retas seguidas".
- `1147,1223` · `1148,1242` · `1172,1243` · `1192,1244` · `1214,1245` · `1235,1244` ·
  `1256,1247` · `1285,1246` (blocos de forest floor)
- `1082,1211` — ilha de grama no mar (o método bom de enumeração)
- x1082-1166, y1008-1192 — estudos de costa de 12/11 (quase tudo meu)

## 5. ERROS MEUS DE 22/11 (não repetir)
1. Inventei a regra "reta + diagonal + reta". Ele nunca disse isso. **Pode.**
2. Repintei a ilha por cima do gelo dele antes de entender — apaguei a correção.
   **NUNCA repintar em cima de uma correção dele.**
3. Troquei quina por reta para zerar o verificador → tirei a cobertura, abri buraco.
4. Pus `d` em tile que só toca a mancha pela diagonal → quadrado solto boiando.
5. Girei a receita do pico sem girar o NOME da peça → borda ao contrário.
6. Entreguei com erro dizendo "é o melhor que o meu método achou". **Não fazer.**
7. Medi "o estilo do mapper" em áreas que eram minhas. **Perguntar antes se é dele.**
