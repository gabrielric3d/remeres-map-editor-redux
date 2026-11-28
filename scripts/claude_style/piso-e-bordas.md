# Piso e bordas

## PRIORIDADE MÁXIMA — as 2 combinações PROIBIDAS (bordas em sequência)
Ensinado pelo mapper em 22/11/2026 ("isso é o top prioridade de tudo"). Ele montou o catálogo
no mapa "estudo de bordas do claude.otbm", andar 7, a partir de **1083,1196 indo para a
direita**, com as 8 formas (4 retas + 4 diagonais) que NUNCA podem acontecer.

Palavras dele:
> 1 - reta + reta
> 2 - diagonal + quina + diagonal (se forem diagonais iguais, na mesma direção)

**1) Reta + reta** — duas peças retas IGUAIS encostadas seguindo a própria linha:
- `s` + `s` lado a lado — 1083,1196 e 1084,1196 (3304)
- `e` + `e` um embaixo do outro — 1087,1196 e 1087,1197 (3302)
- `n` + `n` lado a lado — 1090,1196 e 1091,1196 (3305)
- `w` + `w` um embaixo do outro — 1094,1196 e 1094,1197 (3303)

**2) Diagonal + quina + diagonal** — duas peças `d` IGUAIS em tiles diagonalmente vizinhos,
emendadas pela quina `c` da mesma direção no tile entre elas (a escada de 45°):
- `dse` 1100,1196 + `cse` 1099,1196 + `dse` 1099,1197
- `dsw` 1104,1196 + `csw` 1105,1196 + `dsw` 1105,1197
- `dne` 1108,1196 + `cne` 1108,1197 + `dne` 1109,1197
- `dnw` 1112,1196 + `cnw` 1112,1197 + `dnw` 1111,1197

Onde fica a quina (ela é sempre a que CONTINUA a linha; a outra tile entre as duas não serve):
- `dse` em (x,y) + `dse` em (x-1,y+1) → quina `cse` em (x-1,y);  + `dse` em (x+1,y-1) → `cse` em (x,y-1)
- `dsw` em (x,y) + `dsw` em (x+1,y+1) → `csw` em (x+1,y);  + `dsw` em (x-1,y-1) → `csw` em (x,y-1)
- `dne` em (x,y) + `dne` em (x+1,y+1) → `cne` em (x,y+1);  + `dne` em (x-1,y-1) → `cne` em (x-1,y)
- `dnw` em (x,y) + `dnw` em (x-1,y+1) → `cnw` em (x,y+1);  + `dnw` em (x+1,y-1) → `cnw` em (x+1,y)
Nessas posições a quina é OBRIGATÓRIA pela forma (o tile do meio sempre a recebe), então na
prática basta não deixar **duas `d` iguais em diagonal** nessas direções.

### O que isso obriga no DESENHO do chão (deduzido e verificado)
- Trecho reto de 2 colunas na borda norte/sul → dois `n` (ou dois `s`) colados = proibido.
  **A borda nunca anda 2 tiles na mesma linha.**
- Escada de 45° (degrau de 1 tile, duas vezes seguidas) → `dne`+`cne`+`dne` = proibido.
  **Degrau de 1 nunca vem duas vezes seguidas na mesma direção**: depois de um degrau de 1 o
  contorno tem de mudar (degrau de 2, ou virar para o outro lado).
- Degrau de 2 é seguro entre duas `d` (os tiles não ficam diagonais), mas cria um `e` (ou
  `w`/`n`/`s`) solto na parede do degrau; degrau de 3 faz dois `e` colados = proibido.
  **Degrau vertical máximo 2.**
- Continua valendo: 1 peça por tile (água em 3 lados ou em lados opostos = pino, proibido).

### VERSÃO FINAL DAS DUAS REGRAS (gelo que o mapper pôs na minha ilha, 22/11)
Ele marcou com gelo os pontos onde eu ainda errava, e nomeou assim:
> 1 - retas ou diagonais seguidas
> 2 - a reta vertical + horizontal seguidas que dão aspecto de quadrado

**REGRA 1 — nunca 3 peças seguidas da MESMA FAMÍLIA de canto.** Ao longo do contorno, as
peças de uma mesma direção se encadeiam alternando `d` e `c` (d, c, d, c...). Dois seguidos
pode; **três não**, nem `d c d` nem `c d c`. Ligações (quem encosta em quem na linha):
- `dse`(x,y) ↔ `cse`(x-1,y) e `cse`(x,y-1) | `cse`(x,y) ↔ `dse`(x,y+1) e `dse`(x+1,y)
- `dsw`(x,y) ↔ `csw`(x+1,y) e `csw`(x,y-1) | `csw`(x,y) ↔ `dsw`(x-1,y) e `dsw`(x,y+1)
- `dne`(x,y) ↔ `cne`(x-1,y) e `cne`(x,y+1) | `cne`(x,y) ↔ `dne`(x+1,y) e `dne`(x,y-1)
- `dnw`(x,y) ↔ `cnw`(x+1,y) e `cnw`(x,y+1) | `cnw`(x,y) ↔ `dnw`(x-1,y) e `dnw`(x,y-1)
Gelo dele na minha ilha: 1099,1219 (`dnw`+`cnw`+`cnw`), 1103,1227 (`cse`+`dse`+`cse`),
1111,1223 (`cne`+`dne`+`cne`). Isto ENGLOBA o antigo "diagonal + quina + diagonal".
Vale também para reta: `reta + reta` igual encostada continua proibido.

**REGRA 2 — ERRADA, EU INVENTEI. NÃO USAR.** Em 22/11 o mapper foi categórico:
*"reta -> diagonal -> reta ... isso não tem nada de errado e eu nunca te corrigi por isso"*.
Eu tinha deduzido isso do gelo que ele pôs na minha ilha e generalizei demais. Ele também
não reconheceu como problema o caso `dnw` + `cnw` + `cnw` ("só tem uma única diagonal, não
compreendi"). Conclusão: **vale o que o CATÁLOGO literalmente mostra** (a mesma peça duas
vezes em sequência), não as minhas deduções. Quando o mapper marcar um erro, perguntar
QUAL das peças está errada antes de virar regra.
O texto abaixo fica só como registro do que eu deduzi errado:
- `dnw`(x,y) com `n` em (x+1,y) e `w` em (x,y+1)   | `cnw`(x,y) com `n` em (x-1,y) e `w` em (x,y-1)
- `dne`(x,y) com `n` em (x-1,y) e `e` em (x,y+1)   | `cne`(x,y) com `n` em (x+1,y) e `e` em (x,y-1)
- `dsw`(x,y) com `s` em (x+1,y) e `w` em (x,y-1)   | `csw`(x,y) com `s` em (x-1,y) e `w` em (x,y+1)
- `dse`(x,y) com `s` em (x-1,y) e `e` em (x,y-1)   | `cse`(x,y) com `s` em (x+1,y) e `e` em (x,y+1)
Gelo dele: 1088,1226 (`w`+`dsw`+`s`), 1092,1218 (`n`+`dne`+`e`), 1091,1229, 1095,1230,
1101,1215 (`n`+`dnw`+`w`). Isto ENGLOBA o antigo "reta + quina + reta".

### Como GERAR uma costa que respeita as duas regras (método que funcionou)
Tentar por tentativa-e-erro (annealing) NÃO chega a zero — empaca em 12-13 violações.
O que funciona: o perfil da costa é uma sequência de deltas `d(x) = t(x+1)-t(x)`, com
|d| <= 2. As violações de um tile dependem só de `t[x-2..x+2]`, ou seja de 4 deltas seguidos.
Então dá para **enumerar**: das 625 quádruplas (d1,d2,d3,d4) possíveis, **264 dão zero
violações**. Com essa tabela, uma busca em profundidade escolhe a costa inteira sem nenhuma
violação, e ainda rejeita motivo repetido na hora. O miolo sai PERFEITO por construção.
As **pontas** (primeira e última coluna, onde o mar fica a oeste/leste) não entram nessa
tabela e têm de ser resolvidas à parte (força bruta nas 3 últimas colunas de cada lado);
ponta com altura 3 pede: a coluna vizinha mais alta nos dois lados (senão sai `dnw`+`n`+`w`).

### APROVADO pelo mapper (22/11)
"Essa parte das regras você entendeu perfeitamente." As REGRA 1 e REGRA 2 acima estão certas.
Ele avisou que ainda falta **mais uma regra de estética**, a ser ensinada num exercício.

## AS REGRAS, VERSÃO CERTA E CURTA (fechada em 22/11 com o mapper corrigindo meu bloco)
Ele marcou os erros no meu bloco (1147,1222) com peças de borda coloridas e disse qual
regra era cada cor. Isto aqui é o que vale; o resto do arquivo é histórico.

**0) VOCABULÁRIO: "PINO" = a QUINA pequena** (`cnw` `cne` `csw` `cse`). Quando o mapper diz
"pino no lugar de reta", quer dizer que naquele tile eu pus a quina onde tinha de estar a
peça reta. (22/11, ele apontou 5 desses no meu bloco: 1146,1236 · 1152,1239 · 1153,1242 ·
1150,1247 · 1142,1244.)

**1) `reta + reta` — duas retas da MESMA ORIENTAÇÃO encostadas** (não precisa ser a mesma
peça!). `n` e `s` são as duas HORIZONTAIS: `s`+`n` lado a lado já é reta+reta. `e` e `w` são
as duas VERTICAIS: `e`+`w` uma embaixo da outra já é reta+reta. Catálogo 1083,1196 mostra o
caso da peça igual; o mapper corrigiu em 22/11 mostrando que vale para a orientação:
*"ao corrigir o ponto três você acabaria com duas retas horizontais seguidas: 1151,1239"*
(lá tem `s` em 1151,1239 e eu ia pôr `n` em 1152,1239).

**2) `reta + QUINA + reta` = CANTO DE QUADRADO** — proibido. As duas retas perpendiculares
em tiles diagonais com a quina no meio. Catálogo 1082,1264. Ele marcou de ROSA (borda 208
krailos purple) as 4 do meu bloco: `s`1144,1218+`e`1143,1219 (quina `cse` 1143,1218) ·
`s`1150,1219+`w`1151,1220 (`csw` 1151,1219) · `n`1143,1226+`e`1142,1225 (`cne` 1142,1226) ·
`n`1150,1228+`w`1151,1227 (`cnw` 1151,1228).

**3) `diagonal + RETA + diagonal` = HEXÁGONO/ponta em V** — proibido. É a ponta de 1 tile
simétrica, com uma diagonal de cada lado da reta. Palavras dele: *"eu não deixei o ponto mais
alto de um local sendo uma reta, evitando um possível formato de hexágono"* — o catálogo de
**1161,1207 mostra o que ele NÃO faz**. Ele marcou de BRANCO (borda 203 krailos dirt) e
MARROM (borda 141 wasteland) as 5 do meu bloco: `dse`1145,1218+`s`1146,1217+`dsw`1147,1218 ·
`dse`1142,1222+`e`1141,1223+`dne`1142,1224 · `dsw`1151,1221+`w`1152,1222+`dnw`1151,1223 ·
`dne`1146,1227+`n`1147,1228+`dnw`1148,1227 · `dsw`1151,1224+`w`1152,1225+`dnw`1151,1226.
MEDIDO: o bloco dele = 0 ocorrências; a mancha de grama dele = 0; o meu = 5.

**4) `reta + DIAGONAL + reta` = PODE.** Ele foi categórico: *"isso não tem nada de errado e
eu nunca te corrigi por isso"*. Não confundir com a regra 2 (lá o do meio é QUINA).

**5) duas diagonais IGUAIS ligadas pela quina** (`d`+`c`+`d`) — proibido. Catálogo 1083,1196.

**6) escadinha / ondinha** (motivo repetido 3x no perfil) — proibido. Catálogo 1084,1250.
ATENÇÃO: isso NÃO quer dizer "3 retas iguais em degrau de 1x2". No bloco que ele corrigiu
(1131,1243) há `e` em 1130,1236 + 1129,1238 + 1128,1240 — três `e` em degrau de 1 para o
lado e 2 para baixo, e está certo. A escadinha proibida é o MOTIVO de 3 peças
(`cse`+`s`+`dse`) se repetindo, não a costa descendo em degraus de 2.

**7) A peça PODE apontar para o vazio.** No bloco corrigido dele há **31** peças cuja direção
não tem chão. O que não pode é **faltar cobertura**: todo lado onde a mancha encosta tem de
ter peça (ele: 0 faltando). Minha regra anterior ("a peça tem de apontar para chão real")
estava errada como regra geral — o que vale é: cobertura completa + o desenho ficar bonito
(olhar o screenshot). O fiapo solto acontece quando a peça fica ISOLADA, sem outras peças
encadeadas nela.

### COMO ELE RESOLVE O NÓ (resposta à minha pergunta, 22/11) — bloco em 1131,1243
Eu perguntei se, quando o conserto de um erro estraga o vizinho, ele mexe mais no chão ou
tem outra jogada. **Resposta: mexe no chão, adicionando E removendo tiles, e redesenha a
borda em volta.** No meu bloco ele fez 26 mudanças:
- **pico norte**: aplicou a receita do pico (`cse`+`csw` em cima, `e`+`dsw` embaixo) onde eu
  tinha deixado o `cse`+`s` do auto-border;
- **ombro leste**: **ADICIONOU chão** em (1134,1241) e redesenhou `cse`/`csw`/`dse`/`dsw`/
  `csw`/`dnw`/`cnw` em volta;
- **sudoeste** (onde eu tinha `n`+`n`): **ADICIONOU** chão em (1126,1245) e **REMOVEU** dois
  tiles em (1128,1245) e (1129,1245), abrindo um entalhe, e redesenhou com
  `dne`/`dnw`/`dne`/`n`/`w`/`e`/`cne`/`cnw`.
Resultado dele, medido: 0 reta+reta, 0 quadrado, 0 hexágono, 0 d+quina+d, 0 sem cobertura.

### BORDA AO CONTRÁRIO (erro meu, 22/11)
Ao espelhar/girar a receita do pico eu pus a peça virada: `s` em 1142,1244 (o chão estava ao
NORTE) e `n` em 1153,1242 (o chão estava ao SUL). Ao girar uma receita, girar também o NOME
da peça e conferir de que lado está o chão.

**7) nada fino** (língua/cabo de 1-2 tiles) e **sem pino de chão** (vazio em 3 lados).

**8) "PININHOS" = 4 QUINAS formando um 2x2** — proibido. Catálogo dele em **1268-1269,
y1251-1252**: `cse`+`csw` em cima, `cne`+`cnw` embaixo, as quatro quininhas apontando umas
para as outras. MEDIDO: **0 em todos os blocos dele** (1131,1243 · 1170,1242 · 1168,1224 ·
grama 1218,1227) e 1 no meu de 1256,1247. Checar: os 4 tiles de um quadrado 2x2 todos com
quina.
(Obs.: DUAS quinas coladas são normais — ele tem 9 a 13 por bloco. O proibido são as QUATRO.)

### "É matemática, não é arte" (ele, 22/11)
*"não é que eu bato o olho e acho que está feio, eu sei contar, é uma regra... é um código,
não é arte, são regras"*. Ou seja: TUDO que ele aponta é contável. Quando eu não consigo
medir algo que ele vê, é porque me falta a regra — PERGUNTAR qual é, não chamar de "gosto".

Resumindo o que fica estranho: **quina no meio de duas retas = quadrado; reta no meio de duas
diagonais = hexágono.** Diagonal no meio de duas retas é o certo.

### POR QUE O CHÃO SOZINHO NUNCA ZERA (provado em 22/11)
Num pico de 1 tile, o tile acima do pico é SEMPRE `s`. Daí:
- se os dois lados descem → `dse` + `s` + `dsw` = HEXÁGONO;
- se só um lado desce → a quina do outro lado vira `csw`(ou `cse`) com `s` e `w` ao lado = QUADRADO;
- se o topo tiver 2 tiles → `s` + `s`.
Ou seja: **toda ponta precisa de conserto à MÃO nas peças.** É por isso que o mapper não usa
auto-border.

### RECEITAS DE PICO DO MAPPER (ele desenhou em 22/11; conferidas = 0 violações)
Estão no mapa para copiar: **1163,1257** (correção dele), **1146,1263** e **1155,1263**.
O miolo das três é o mesmo: **o topo do pico são DUAS QUINAS LADO A LADO (`cse` + `csw`)**
uma linha acima, nunca um `s` sozinho. Abaixo delas vem `e` à esquerda e `dsw` no meio.

Modelo 1 — relativo ao `s` em (0,0) [exemplo real: `s` em 1165,1257]:
```
(-2,-1) cse  (-1,-1) csw
(-2, 0) e    (-1, 0) dsw   (0,0) s    (+1,0) csw
(-3,+1) cse  (-2,+1) dse   (0,+1) dnw (+1,+1) cnw
```
Modelo 2 — relativo ao `dsw` em (0,0) [exemplo real: 1146,1263]:
```
(-1,-1) cse  ( 0,-1) csw
(-1, 0) e    ( 0, 0) dsw   (+1,0) csw
(-2,+1) cse  (-1,+1) dse   (+1,+1) w
```
Modelo 3 — pico + ombro descendo, relativo ao `dsw` em (0,0) [exemplo real: 1155,1263]:
```
(-1,-1) cse  ( 0,-1) csw
(-1, 0) e    ( 0, 0) dsw   (+1,0) s     (+2,0) csw
(-2,+1) cse  (-1,+1) dse   (+2,+1) dsw  (+3,+1) s   (+4,+1) csw
(+3,+2) dnw  (+4,+2) cnw
(+3,+3) w
(+3,+4) cnw
```
Girar/espelhar para as outras três direções.

### O MOVIMENTO, APLICADO NUMA PONTA DE VERDADE (ele consertou a minha em 1147,1236)
Situação: o topo tem 2 tiles de chão lado a lado em (x,y) e (x+1,y); o auto-border poria
`s` em (x,y-1) e `s` em (x+1,y-1) → reta+reta. O que ele fez (exemplo real x=1147,y=1237):
- o `s` da ESQUERDA vira **`dsw`** → (1147,1236)
- à esquerda dele entra **`e`** → (1146,1236)
- uma linha ACIMA entram as duas quinas **`cse` + `csw`** → (1146,1235) e (1147,1235)
- o `s` da direita FICA → (1148,1236); a quina `csw` da direita fica → (1149,1236)
- a linha de baixo continua normal: `s`(1145,1237) `dse`(1146,1237) `cse`(1144,1237)
Efeito: a borda SOBE um tile naquela coluna e a ponta vira um bico, sem reta+reta, sem
quadrado e sem hexágono. Ele também ADICIONOU chão em (1148,1237) para o topo ter 2 tiles.
Resumo do movimento: **levantar a borda um tile, trocando o `s` por `dsw`+`e` e pondo
`cse`+`csw` em cima.**

### PEÇA SOLTA — o teste que eu tenho de fazer SEMPRE antes de dar por pronto
Uma peça desenha o chão na direção do nome dela. Se ali não houver chão de verdade, ela vira
um **fiapo boiando no vazio**. Aconteceu comigo duas vezes em 22/11 (a 1ª com `d` em tile que
só tocava na diagonal, a 2ª com `n`/`e` apontando para o nada), e nas duas vezes o mapper viu
na hora e minha conta dizia "zero violações".
Teste: para cada peça, as direções do nome TÊM de ser chão (`n`→(0,-1), `dnw`→(0,-1) e (-1,0),
`cse`→(+1,+1), etc.). ÚNICA exceção: as peças da corrente do PICO (o movimento acima), que
formam o bico e se encaixam umas nas outras — ali 4 peças por ponta apontam para o vazio e
fica bonito. Fora do pico, nunca.
**E sempre olhar com `screenshot` zoom 0.5 — zero na conta não quer dizer bonito.**

### ERRO GRAVE: trocar a RETA pela QUINA para "zerar" (NÃO FAZER)
Em 22/11 eu zerei o verificador trocando, em 5 ombros, a peça reta por uma quina pequena.
O mapper marcou os 5 com chão laranja (borda 206) e disse: *"você não arrumou nenhuma quina
onde é reta horizontal + vertical ou vice-versa e além disso quebrou o mapa deixando um
possível espaço para reta+reta... você fez alguma coisa de maluco usando as bordinhas menores
sem concluir o pathing do ground completo"*.
Por quê é errado: a reta `n` cobre o contato NORTE daquele tile. Trocar por `cne` (que só
cobre a diagonal) **deixa o contato descoberto** — um buraco na borda. O verificador zera
porque a peça sumiu, não porque o problema foi resolvido.
**REGRA: um conserto à mão só pode ACRESCENTAR cobertura, nunca tirar.** O movimento do pico
é válido (`s`→`dsw` continua cobrindo o sul, e as outras 3 peças são acréscimo em tiles
vazios). Trocar reta por quina NÃO é válido.
Os 5 lugares que ele marcou de laranja (ali a peça certa é a reta): 1145,1237 (`s`) ·
1152,1240 e 1152,1241 (`w`) · 1153,1243 e 1153,1244 (`w`) · 1143,1244 (`n`) ·
1145,1245 e 1146,1245 (`n`). Esses casos têm de ser resolvidos **no desenho do chão**.

### Resultado do exercício (22/11) — bloco em 1148,1242, andar 7
Chão retrabalhado + conserto à mão: **0 violações** de todas as regras, 0 pino, 0 peça solta
fora dos picos. Consertos à mão que foram necessários (13 peças): o pico norte (receita dele),
o pico sul (espelho), e 5 trocas de quina/reta nos ombros:
`cne`1143,1244 · `cse`1145,1237 · `e`1146,1245 · `csw`1152,1240 · `csw`1153,1243.
Como achar essas 5: para cada tile de borda, testar todas as 12 peças que apontam para chão
real e ficar com a que zera mais violações.

## (histórico, lido errado por mim) o ponto extremo
Palavras dele: *"eu não deixei o ponto mais 'alto' de um local sendo uma reta, evitando um
possível formato de hexágono"*. Catálogo dele em **1161,1207,7** (borda 137, peças soltas):
- pico (ponto mais ALTO): `dse`(1161,1207) + `s`(1162,1206) + `dsw`(1163,1207) — o topo é UM
  tile só, com as duas diagonais descendo dos lados.
- fundo (ponto mais BAIXO): `dne`(1167,1207) + `n`(1168,1208) + `dnw`(1169,1207).
- ponta para LESTE: `dsw`(1173,1205) + `w`(1174,1206) + `dnw`(1173,1207).
- entalhe vindo do OESTE: `dse`(1177,1205) + `e`(1176,1206) + `dne`(1177,1207).
Ou seja: **todo extremo do contorno (cima, baixo, esquerda, direita) é um tile só**. Se o
extremo for um trecho reto de 2+ tiles, a mancha fica com cara de hexágono/bloco.
Como achar a ponta no mapa (ancorada no tile de CHÃO em (x,y)):
- ponta norte: `s`(x,y-1) + `dse`(x-1,y) + `dsw`(x+1,y)
- ponta sul:   `n`(x,y+1) + `dne`(x-1,y) + `dnw`(x+1,y)
- ponta leste: `w`(x+1,y) + `dsw`(x,y-1) + `dnw`(x,y+1)
- ponta oeste: `e`(x-1,y) + `dse`(x,y-1) + `dne`(x,y+1)
A diagonal que ele deixou como "exemplo para orientar" no exercício (o `dnw` em 1151,1226)
era exatamente o pedaço de baixo de uma PONTA LESTE em 1151,1225 — era uma dica da forma.

### ERRO QUE EU COMETI: trocar a quina pela diagonal (NÃO FAZER)
Tentei zerar a última violação trocando o `c` do fim da diagonal pela `d` da mesma direção
(`cse`→`dse` etc.). Na conta zerava, **mas no mapa ficam QUADRADOS SOLTOS boiando**: o sprite
da `d` preenche o tile quase inteiro, e num tile que só encosta na massa pela diagonal ele
aparece como um bloco separado. O mapper viu na hora. Regra: **nunca pôr uma `d` num tile que
só toca a mancha pela diagonal** — ali é `c` (quina), que é um sprite pequeno de canto.
Antes de dar qualquer conserto por conta como "resolvido", OLHAR com `screenshot` zoom 0.5.

## O MAPPER NÃO USA O AUTO-BORDER (descoberto em 22/11, exercício dos dois blocos)
Comparei peça por peça o bloco dele (1168,1224) com o que o `border_pieces` do editor devolve
para o MESMO chão: **43 das 68 peças são diferentes**. Exemplos no pico de cima:
| tile | auto-border | o que ele pôs |
|---|---|---|
| 1165,1217 | `cse` | `e` (e `cse` subiu para 1165,1216) |
| 1166,1217 | `s`   | `dsw` |
| 1167,1217 | `csw` | `s` |
| 1167,1218 | `w`   | `dnw` |
| 1168,1218 | `cse` | `cse` + `cnw` (DUAS peças empilhadas) |
As peças dele não são a borda de NENHUMA forma (há contradições: 1165,1216 `cse` exige chão
em 1166,1217, que por sua vez tem peça). Ou seja: ele **desenha a borda à mão, como traço**,
escolhendo a peça pelo SPRITE para formar a silhueta — pontas e entalhes que o chão sozinho
não tem. O resultado dele tem espigões; o meu, feito só por auto-border, é um blob redondo.
A forma do chão dele também mudou MUITO em relação ao bloco original (~20 tiles), criando
pontas.
CONFIRMADO por ele: *"sim eu não uso auto border, isso é ruim, fazemos tudo manual"*.
Também: *"o que importa é a silhueta bacana, mas as regras são mais importantes, então se
precisar mudar um pouco o formato é de boa"* — ou seja, a ordem é: REGRAS > SILHUETA > forma
original. Mexer em ~20 tiles de um bloco de 8x8 para conseguir isso é normal.
(Obs.: a diferença em 1174,1222 era uma quina que ele tinha esquecido, não estilo; ele
corrigiu depois.)

## INVASÃO DE AREIA NA GRAMA (exemplo dele em 1218,1227,7)
Mancha de forest floor sobre `new sand` (44198), borda 137 da grama nos tiles de areia.
Por baixo/dentro da grama ele põe uma LÍNGUA de areia feita com a borda **17** (areia dona):
`cse`(1217,1226) `csw`(1218,1226) / `e`(1217,1227) `dsw`(1218,1227) `csw`(1219,1227) /
`w`(1219,1228) / `cnw`(1219,1229). As peças de areia ficam EMPILHADAS por cima das peças de
grama no mesmo tile (ex.: 1217,1227 = `n` da grama + `e` da areia).
Regras que ele deu:
- **a borda de areia só "acaba a invasão" depois de voltar para a areia de novo** — ela
  entra na grama e fecha já do lado de fora, no chão de areia. Se terminar dentro da grama,
  fica um corte brusco.
- **apagar a peça de grama que atravessa a areia**: no exemplo ele removeria o `cne` da
  grama em 1218,1228, que cruzava a língua de areia.

### ARMADILHA: obedecer as regras e cair na ONDINHA
Quando gerei a costa só por enumeração, ela ficou sem nenhuma violação das regras 1 e 2 e
MESMO ASSIM ficou ruim: virou um zigue-zague miúdo e regular, uma "lagarta". Diagnóstico do
mapper: *"o formato ficou ruim, porque você fez o esquema de ondinhas que nós tínhamos
estudado em 1115,1247"*. Ou seja, a ondinha não é só "3 motivos repetidos"; é o ASPECTO
GERAL de dentinhos miúdos de 1 tile subindo e descendo pela costa inteira.
Regra prática: passar nas regras 1 e 2 é condição NECESSÁRIA, não suficiente. A costa precisa
de trechos de escalas diferentes — enseadas largas, pontas compridas, trechos calmos — e não
de um dente a cada 2-3 tiles do começo ao fim.

O texto abaixo é a primeira versão que eu tinha escrito (caso particular, com os exemplos
do catálogo de 1082,1264):
**3) Reta + quina + reta** (catálogo de **1082,1264**, borda 9) — duas
retas PERPENDICULARES em tiles diagonalmente vizinhos, com a quina daquela direção no tile
entre elas. É o canto de 90° que o **auto-border do RME** produz; palavras do mapper:
"retas consecutivas não fica bacana, fica estranho, porque quando usam o RME com auto border
ele faz esse formato". As 4 formas:
- `cse` em (x,y) + `s` em (x+1,y) + `e` em (x,y+1)   — ex.: 1081,1264 / 1082,1264 / 1081,1265
- `csw` em (x,y) + `s` em (x-1,y) + `w` em (x,y+1)   — ex.: 1086,1264 / 1085,1264 / 1086,1265
- `cne` em (x,y) + `n` em (x+1,y) + `e` em (x,y-1)   — ex.: 1102,1266 / 1103,1266 / 1102,1265
- `cnw` em (x,y) + `n` em (x-1,y) + `w` em (x,y-1)   — ex.: 1104,1266 / 1103,1266 / 1104,1265
Os outros dois exemplos do catálogo (1096-1098 e 1102-1104, y1264-1266) são o mesmo encaixe
em volta de um DENTE DE 1 TILE — que já é proibido por ser fino / pino.

No chão isso quer dizer: num canto CÔNCAVO da terra (baía, entalhe), um dos dois lados tem
de dar o degrau logo no tile seguinte, em vez de seguir reto. Assim a reta vira `d` e o
encaixe some.

O que NÃO é erro: duas `d` DIFERENTES encostadas; a mesma `d` em diagonal na direção que não
encadeia; tile de contorno sem peça (o vizinho cobre o contato). O proibido é a mesma peça
repetida em sequência e o canto de 90° fechado por quina.

### Jeito elegante de fazer curva (catálogo do mapper em **1081,1276**, borda 236 earth)
Quatro desenhos feitos à mão, com peças soltas no vazio (NÃO MEXER). O que eles têm em comum:
a linha é feita de **diagonais (`d`) e quinas (`c`)**, e uma reta nunca puxa outra: ela vem
de uma `d` e MORRE numa `c`.
- 1080-1082, y1274-1277: `cse` / `cse`+`s`+`dse` / `cne`+`dne` / `cne` — a reta do meio tem
  uma `d` de um lado e uma `c` do outro.
- 1085-1089, y1273-1275: `cse`+`csw` / `e`+`dsw`+`s`+`csw` / `cse`+`dse`.
- 1090-1093, y1275-1279: `d` e `c` da MESMA direção lado a lado (`dnw`+`cnw`), repetidamente —
  onde o auto-border poria mais uma reta, ele põe a quina.
- 1100-1104, y1278-1280: `cne`+`dne` / `e`+`dnw`+`n`+`cnw` / `cne`+`cnw`.
Resumo prático: **quina no lugar da segunda reta.**

## PRIORIDADE MÁXIMA (2) — "escadinha" e "ondinha"
Ensinado em 22/11/2026, logo depois das 2 combinações acima. O mapper montou três exemplos
do que **não é bonito** a partir de **1084,1250** (andar 7, borda 9, peças soltas no vazio —
NÃO MEXER, é catálogo):

1. **Escadinha larga** — x1082-1090, y1247-1251: degrau "2 tiles para o lado, 1 para baixo"
   repetido 4 vezes; o mesmo motivo `cse` + `s` + `dse` sempre igual.
2. **Escadinha alta** — x1100-1104, y1242-1251: degrau "1 para o lado, 2 para baixo"
   repetido 5 vezes (`e`, depois `cse`+`dse`).
3. **Ondinha** — x1110-1123, y1246-1247: dente de 1 tile que sobe e desce com período fixo
   de 4 colunas, 3 vezes seguidas. Palavra do mapper: "as bordas não levam a lugar algum".

Essas três passam nas regras 1 e 2 (nenhuma peça repetida encostada) e mesmo assim estão
ERRADAS. O defeito é o **motivo repetido**, não a peça.

### Como medir (usar no gerador ANTES de pintar)
Pegue o perfil da costa: para um litoral norte/sul, `t(x)` = y do primeiro tile de terra em
cada coluna (e `b(x)` para o sul); para um litoral leste/oeste, o mesmo com x em função de y.
Calcule os deltas `d(x) = t(x+1) - t(x)`. **Proibido: qualquer bloco de período p (p = 1 a 5)
que se repita 3 vezes seguidas** (3·p deltas iguais ao padrão).
- exemplo 1 → d = `-1,0,-1,0,...` (p=2 repetido 4x)
- exemplo 2 → d = `-2,-2,-2,-2,-2` (p=1 repetido 5x)
- exemplo 3 → d = `-1,0,0,+1` repetido 3x (p=4)
Dois degraus iguais seguidos ainda vale (`-2,-2` pode); três não.

## Referência de verdade: a ilha grande DO MAPPER (1039,1044,7)
É dele (ele indicou em 22/11). Areia 231 com borda **338 como CHÃO** do tile de praia
(convenção: a peça fica no tile de TERRA e o nome diz onde está o mar). Colunas de terra
x1010 a x1065. Medido por mim, nela:
- **reta + reta = 0** → a regra 1 é absoluta, confirma.
- **diagonal + quina + diagonal = 16** (de 108 peças `d`, ~15%) — ou seja, na prática dele
  isso NÃO é zero. PERGUNTAR o alvo antes de otimizar para 0.
- **quina de 90° (uma `c` com as duas retas perpendiculares ao lado) = 26** → é comum na
  ilha dele, logo NÃO é isso que ele chama de "quadrado" nos exemplos de 1081,1264.
- **motivos repetidos (escadinha/ondinha) = 4** em 56 colunas → raro, mas não zero.
Qualquer métrica nova tem de ser conferida contra esta ilha antes de virar regra.

## Nada fino
"Coisas finas assim ficam bem feias" (22/11, sobre um cabo de 1 coluna x 2 tiles).
Língua/cabo/istmo de 1-2 tiles de largura: não. Na minha ilha de estudo passei a exigir
altura de coluna >= 4 no corpo e exatamente 3 nas duas pontas.

Tirado da análise de 773 capturas (notas em `analise/`) e das correções feitas junto com o
mapper no inferno.otbm (ver também `../claude_style.md`, seção "Manchas de chão").

## Regra técnica (do mapper, verificada no mapa)
- Tudo raw, borda à mão. Cada TRECHO do contorno tem UM dono: num lado a borda da mancha vai
  por cima do chão em volta; no outro, o chão em volta (o "mais alto") põe a borda DELE
  por cima da mancha — e ali a borda da mancha não existe. Nunca as duas no mesmo trecho.
- Borda grossa do terreno mais alto = degrau/profundidade. Borda normal = transição plana.
- Canto: onde a linha grossa termina, o tile vizinho recebe o canto pequeno grosso
  empilhado sobre a borda do outro dono.
- Todo lado onde dois chãos se encontram precisa de uma peça cobrindo aquele lado (canto
  pequeno não cobre lado). Conferir com `check_region` e screenshot `ground_only`.
- ENTRELAÇAR: uma borda não pode ser predominante no contorno inteiro (ex.: borda do forest
  floor por cima da grama em toda a volta da mancha). Em ALGUNS trechos (uns 20-25% do
  contorno, pedaços de 3-8 tiles, não em todo lugar) inverter o dono: ali a borda da grama
  vai por cima do forest floor e a do forest floor sai. Dá charme e quebra a repetição.
- Fechar o entrelaçamento com CORNERS em cada ponto de troca (a linha nunca termina seca).
  Ex.: trecho invertido F (grama dona, grama ao sul) e ao leste o tile N normal (forest
  floor dono); G1 = grama ao sul de F, G2 = grama ao sul de N:
  - N recebe o canto pequeno da GRAMA voltado para G1 (`csw`): a linha da grama faz a curva;
  - G1 perde o canto do forest floor que N jogava nele (`cne`);
  - G2 recebe o canto da GRAMA (`cnw`) EMPILHADO por cima da borda do forest floor (`n`),
    cortando o começo dela e continuando a curva.
  Gire a regra para os outros lados.
- Canto também no FIM DA RETA do outro dono: quando a borda do forest floor corre por um
  lado de um tile de grama (ex.: `dne` num bolsão) e o próximo tile nessa linha já é um
  trecho invertido, a reta chega seca na ponta. Empilhe o canto pequeno do forest floor
  (ex.: `cse`) no tile de grama para ela fazer a curva. O mesmo vale quando a borda da grama
  de um trecho invertido encosta num forest floor normal que não tem grama do lado: canto do
  forest floor empilhado por cima da borda da grama (ex.: `cne` sobre `n`).

## Forma das manchas e transições
- Contorno orgânico e SERRILHADO, com bicos para os dois lados; largura variando,
  bifurcações. Nunca retângulo, nunca "moldura". (ss10, ss26, ss61, ss406, ss408, ss450)
- Transição em LÍNGUAS finas (1 tile) entrando no outro piso, e ILHAS/SATÉLITES: 1-3
  manchinhas soltas além da mancha principal. (ss16, ss37, ss52, ss204, ss302, ss513, ss566)
- Transição em 2-3 DEGRAUS DE COR: um anel intermediário entre os dois pisos (terra, areia,
  musgo, cascalho), ex.: lava -> rocha avermelhada -> rocha fria; lava -> terra escura ->
  cascalho -> terra ocre. (ss403, ss448, ss465, ss472, ss613, ss615, ss676)
- Borda SALPICADA/esfarelada: a passagem feita com pedrinhas, pó, cristais ou pontinhos
  (musgo em pintinhas sobre a pedra), não com linha dura. (ss292, ss277, ss608, ss639)
- Três grounds se tocando na mesma cena, cada contorno com forma própria. (ss37, ss48,
  ss221, ss233, ss289)

## Variação tonal
- MANCHAS de um ground de COR PARECIDA quebram a monotonia de áreas grandes (musgo escuro na
  grama, terra rugosa na terra, pedra de outro tom no calçamento, cascalho na areia),
  muitas vezes em faixas diagonais. (ss104, ss158, ss234, ss254, ss304, ss513, ss595; old
  ss3, ss6, ss49)
- Áreas grandes também intercalam 2-3 grounds bem diferentes em manchas. (old ss1, ss8, ss40)
- Tom mais escuro de chão marca trilhas e áreas de uso (pátio, altar, spawn) sem cerca.
  (ss129, ss148, ss605, ss621)
- Detalhe de piso SEM trocar o ground: fissuras, veios de lava, raízes, fiapos, bolhas em
  densidade média. (ss528, ss582, ss642, ss655, ss683)

## Chão embaixo das coisas
- Objeto de destaque ganha uma MANCHA PRÓPRIA por baixo, de contorno pontudo: terra sob
  fogueira e tenda, grama sob palmeira, areia sob cristal/coluna caída, gelo sob cristal,
  ladrilho sob menir, chão mais claro sob vegetação. (ss227, ss231, ss459, ss548, ss564)
- Teleporte em piso contrastante (cruz ou quadrado). (ss101, ss120, ss151)

## Caminhos e estradas
- Estrada de terra (feita linha a linha): cada lado muda de coluna a cada 1-2 linhas, em
  passos de 1 tile (peças da borda viram diagonais e cantos); NUNCA 3+ linhas na mesma
  coluna nem horizontal de 3+; os dois lados com ritmo próprio (nunca paralelos) e largura
  2-4; sem ponta de 1 linha (sair e voltar na linha seguinte). Na ponte: abre em funil até
  a largura da cabeceira (tábuas + 1 de cada lado), nunca uma "barra" atravessada.
- Checar a estrada inteira (retas e pontas) antes de aplicar; se uma regra conflita com
  outra num ponto fixo (porta, ponte), gerar variações e ficar com a primeira sem falha.
- Largura variável (2-4 tiles), nunca reta, alargando nos cruzamentos; orla de cascalho;
  sujeira/sangue/detrito. (ss619, ss632, ss681; old ss27, ss52, ss62)
- Em salões, passadeira com padrão direcional (losangos, greca, tapete, fila de runas)
  marca o eixo. (ss301, ss363, ss669, ss691)

## Piso construído
- Termina irregular: tijolo serrilhado em diagonal, piso "vazando" pela caverna, lajotas
  faltando, borda quebrando sobre terra, entulho nos cantos. (ss29, ss69, ss420, ss499)
- Abandono: calçamento só em ilhas no meio da neve/pó. (ss667, ss668, ss670)
- Uniforme ganha variação: ladrilhos de outro tom, "tapetes" geométricos, sujeira ou sangue
  em manchas. (ss404, ss415, ss418, ss428)
- Cidade: dois calçamentos separam funções (praça x rua); meio-fio de pedrinhas entre rua e
  grama; canteiros com a grama "vazando". (ss221, ss272, ss57, ss584, ss588)

## Costa e contorno de mancha — regras MEDIDAS (sessão 12/11/2026)
Tudo abaixo foi extraído comparando meus exemplos com os mapas do mapper
(lava x1174-1198 y1006-1032; ilha grande x1004-1074; correções em y1136-1160).

### Conjuntos (BlackTalon)
- mar↔areia: borda **338** como CHÃO do tile de transição —
  n 37945 e 37948 s 37947 w 37946 | cnw 37988 cne 37955 csw 37954 cse 37953
  | dnw 37952 dne 37951 dse 37949 dsw 37950
- mar↔grama: borda **336** como ITEM sobre a grama —
  n 37957 e 37958 s 37959 w 37960 | cnw 37965 cne 37966 csw 37967 cse 37968
  | dnw 37961 dne 37962 dse 37964 dsw 37963
- areia dona sobre grama: **17 + 16 EMPILHADOS** (17 primeiro, nunca um só) —
  17: 4760 4761 4762 4763 | 4764 4765 4766 4767 | 4768 4769 4771 4770
  16: 4772 4775 4773 4774 | 4776 4777 4778 4779 | 4780 4781 4783 4782
- grama dona sobre areia: borda **9** — 3305 3302 3304 3303 | 3301 3300 3299 3298
  | 3309 3308 3306 3307
- água: 37934 base (36%), 37935/36/37/41/42/43 ~10% cada, 37938/39/40 ~2% (detalhe)

### Regras duras — alvo ZERO
1. **1 peça por tile.** Terra com água em 3 lados ou em lados opostos = pino. Proibido.
2. **Nenhuma reta igual colada** (vizinhança 8). Medido: lava 0 de 25, ilha grande 0 de 92.
3. **Nenhuma saliência de 3 com reta no meio** (`dnw · n · dne` e as 3 rotações).
   É o que o mapper chama de "quadrado". Alvo ≤2 numa mancha de 25x25.
4. **Nenhum motivo repetido**: no perfil da borda (esq/dir/topo/base), delta de
   período 1-3 repetido 3x — `[-1,-1,-1]`, `[0,-1,0,-1,0,-1]`, `[-2,-2,-2]`,
   `[-1,0,1]x3`. É a "escada". Mapas corrigidos do mapper: 0. Alvo 0.
5. **Largura mínima 3** de chão de verdade. Nunca conexão por tile de borda.
6. Nada de detalhe interno pequeno (furo/lagoa) feito só com borda.

### Faixas (NÃO zerar)
- escada45 (mesma peça `d` em tiles diagonalmente vizinhos): manter **1 a 6**.
  Zerar deixa a costa blocada, pior que o defeito.
- perfil: |d|=0 ~25, |d|=1 ~27, |d|=2 ~13, |d|≥3 raro.

### Dois donos numa mancha
- dono decidido **por ARESTA**, nunca por tile: `side(g)+side(s) <= 0` → areia manda.
  Assim não sobra contato sem peça nem faixa com dois donos.
- a linha que acaba **morre em quina (`c`), nunca numa reta**.
- **encaixe**: o último tile do dono que sai recebe TAMBÉM as peças dos lados que o
  outro dono levou — sempre no chão dele. Exemplos do mapper: 1151,997 (9 n + 17/16 w),
  1160-1162,986, e a correção 1158,1112 `n`→`dne` + `cne` em 1158,1113.
- **nunca** peça clara solta num tile de areia longe do contorno ("asset jogado").
- tile de contorno sem peça nenhuma é NORMAL (o vizinho cobre o contato) — não inventar pino.

### Pinça (quebrar reta repetida sem mexer no chão)
Exemplo do mapper: lava x1187-1188, y1016-1017. Onde daria `e,e,e` empilhados:
`dse` em cima, `dne` embaixo, e `cse`/`cne` um tile PARA DENTRO (lado oposto ao dono).
Espelhar para w/n/s. O `check_region` acusa as quinas internas como "floating" — é o efeito.

### Ainda NÃO resolvido
- a forma sai de otimizador: passa em todas as métricas mas não tem **intenção**
  (um cabo apontando pra algum lugar, uma baía onde vai um porto).
- a ilha de grama ficou "quase natural" — falta algo que ainda não virou métrica.
- nunca tratar como pronto; sempre pedir o mapper olhar antes.

### Frases do mapper que valem como regra (sessão 12/11)
- Nunca ligar duas diagonais por uma quina (ex. 1087,1024 / 1086,1023) — fica feio.
- Nunca ligar reta com reta em ângulo seco: fica "quadrado".
- Nunca terminar um canto com uma reta — fecha com quina.
- Duas bordas seguidas separadas só por uma quina, na praia, NÃO é erro.
- Diagonais seguidas, mesma peça repetida, "V"/ponta de diamante e ondulação regular = padrão de fórmula. Nunca.
- Borda nunca fica sozinha sobre a grama: ou completa a areia, ou passa a grama por cima. Não dá pra finalizar com borda jogada.
- Faltou o pino da quina (ex. diagonal inferior direita sem o pino de areia) = borda incompleta.
- Referência de forma natural: a lava do mapper em 1183,1018,7. Não é reta, não segue fórmula.
- O efeito de duas bordas na areia (17 + 16) está certo e fica bonito.
