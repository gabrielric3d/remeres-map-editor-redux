# Diário dos estudos

## COMO COMEÇAR A PRÓXIMA SESSÃO
1. Ler `claude_style/bordas-regras.md` INTEIRO. As regras estão **FECHADAS** (R1..R10);
   a seção 1.6 tem o sistema todo em 4 passos e a 2.5 diz como carregar o motor.
2. **Carregar o motor em vez de reescrever** (ele vive só na memória do editor):
   ```lua
   local src = app.readFile("claude_style/bordas_lib.lua")
   load(src, "bordas_lib", "t", _ENV)()   -- o _ENV é obrigatório
   ```
3. Calibrar: rodar o verificador no gabarito dele `1170,1242` — tem de dar **0**, inclusive
   R9 e R10 (27 pininhos previstos, 0 faltando). Se não zerar lá, o errado sou eu.
4. **Antes de espalhar peça, OLHAR O SPRITE** da borda (seção 1.7). Foi o erro mais caro: as
   regras fechavam em zero e o resultado era *"100% errado e muito feio"*.

### Prompt para "ligar" o conhecimento
> Leia `scripts/claude_style/bordas-regras.md` inteiro. Depois me diga: (a) o que é a R9 e a
> R10, (b) por que um passo de ±1 tem de ficar isolado, (c) como se escolhe o dono de uma
> divisa entre dois chãos. Então carregue `bordas_lib.lua` e faça o bloco que eu pedir, só
> entregando com o verificador em ZERO.

## Último dia: 23/11/2026 — chão contra chão, e o motor virou arquivo
Mapa "estudo de bordas do claude.otbm", andar 7.

**O que eu aprendi (tudo já está em `bordas-regras.md`):**
- **R9** (as linhas casam na divisa entre dois tiles que não são o dono) e **R10** (os
  pininhos são CALCULADOS, não escolhidos) — fecham o sistema; a receita do pico cai delas.
- **Gramática do contorno**: passo de ±1 tem de ficar isolado entre zeros (ou entre passos de
  sinal oposto); dois passos no mesmo sentido só valem se os dois forem ≥2; e **o fim da
  mancha conta como passo**. Medida no gabarito dele e provada pelo solver.
- **R6 estava errado comigo**: pininhos é o arranjo EXATO dos 4 bicos no mesmo vértice
  (catálogo dele em 1268,1251), não "4 quinas quaisquer num 2x2". Pela R10 é impossível.
- **Quem é o dono se decide olhando o SPRITE**: fio fino pode mandar em tudo; decal de tile
  cheio só sobre chão de cor parecida (vira degradê) — sobre cor contrastante come o chão.
  Olhar as peças retas e as verticais separado (a 156 tem tufo bonito no `n`/`s` e um fio
  costurado no `e`/`w`).
- **Ordem no solver**: por trecho conectado andando pelo contorno. Linha a linha = 400 mil
  nós sem fechar; por trecho = 2,3 mil nós, 0,5 s, mesma área.
- A silhueta dele, medida (seção 1.8): reto, reto, um ±1 isolado, e um salto grande de vez
  em quando. Eu estava gerando degrau 2 em quase toda linha.

**Onde está no mapa:** canvas `1226,1281` (meu, 0 erros) · `1290,1283` (DELE, só chão — minha
melhor referência de silhueta; está a 6 tiles de fechar, lista na seção 4) · `1222,1314` (meu,
dirt+spikes+lava, 0 erros e check_region limpo) · `1248,1314` (cópia virgem, dele).

**O que ele aprovou:** o bloco de forest `1315,1248` (*"de fato não teve erro disso dessa
vez"*, só faltaram os pininhos, que ele pôs) e, por não ter reclamado, o canvas de lava.
**O que ele reprovou:** o pathing grande `x1336-1552 / y1050-1276` — *"100% errado e muito
feio"*, porque escolhi o dono errado. **Continua errado no mapa**, é o primeiro item abaixo.

## PRÓXIMO PASSO
1. **Refazer o pathing grande** com o dono certo (mushroom contorna mold e path). O motor faz
   em ~2 min: `B.load` da área, trocar `recv`, `B.clearPieces`, `B.solveSet`, `B.apply`.
2. **Implementar a receita do pico no solver** (anel estendido a 2 tiles), para parar de ter
   que apagar picos de 1 tile — hoje eu removo o pico porque o solver não alcança as peças
   que ficam 2 tiles fora.
3. Rodar `check_region` sempre DEPOIS de resolver, por causa dos trechos retos de 6+ (nem o
   solver nem o detector de armadilha reclamam deles).

## Dias anteriores
- **22/11/2026** — regras de borda fechadas no forest floor (blocos 1131,1243 · 1170,1242 dele;
  1315,1248 meu). Método: verificador + solver exato + troca de forma. Armadilhas do run_lua:
  `tile.ground = nil` para apagar, nunca `app.transaction` dentro do run_lua, globais persistem.
- **12/11/2026** — costa mar↔areia (338), mar↔grama (336). ATENÇÃO: x1082-1166 / y1008-1192 é
  quase tudo MEU, não serve de referência do estilo dele.
