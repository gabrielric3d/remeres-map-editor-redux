# Guia de estilo do mapa (lido pelo Claude Assistant)

Este arquivo entra no prompt do Claude Assistant (Window > Claude Assistant) toda vez
que uma conversa começa (ou depois de "Clear"). Escreva aqui como o seu mapa deve ser
feito: brushes por papel, regras estruturais, o que evitar. Pode ser em português.
Quanto mais concreto (nomes exatos de brushes, tamanhos, densidades), melhor.

## Guias por tema (LEIA o que a tarefa pedir antes de construir)
Tirados da análise de 773 capturas dos mapas do mapper; cada regra aponta exemplos.
- `claude_style/estrutura.md` — montanha, rocha, altura/profundidade, água/lava, arquitetura,
  ruína, composição da cena, cidade.
- `claude_style/piso-e-bordas.md` — manchas, transições, variação tonal, chão sob objetos,
  caminhos, piso construído, e a técnica de borda (um dono por trecho).
- `claude_style/detalhes.md` — onde vai cada coisa, densidade, grupos/kits, luz, cidade, ruína.
- `claude_style/analise/` — as notas imagem por imagem (para procurar exemplos).

## Regras estruturais
- Casas: porta sempre voltada para a rua; tamanhos e formas variando (L/T/U), frente das
  casas avançando e recuando (ver `claude_style/estrutura.md`, Cidade).
- Caminhos com largura variável (2-4 tiles), nunca retos (ver `claude_style/piso-e-bordas.md`).
- Água não encosta direto em montanha: faixa de transição (terra, cascalho, espuma).
- Os nomes de brushes/ids vêm do `list_brushes`/`list_borders` e dos trechos do mapper
  lidos com `read_region` raw=true — nunca de exemplo genérico.

## Estilo visual
- Tudo em RAW: chão, bordas, paredes, montanha e detalhes por id de item com `place`.
  Nunca auto-border (`paint`) a não ser que eu peça.
- Peças de borda sempre pelo `list_borders` (não chutar id). Antes de construir algo,
  ler um trecho parecido meu com `read_region` raw=true e seguir como está feito.
- Bordas quebradas e empilhadas à mão; nenhuma linha reta longa, largura sempre variando.
- Toda mancha de chão começa pela borda completa do `border_pieces` (lados, cantos e
  diagonais); só depois quebrar/empilhar à mão. Nunca deixar dois chãos se encostando sem
  borda: o quadrado de 32 px do tile aparece e fica feio.
- GROUNDING (chão e bordas): ligar `set_display` flat_colors "on" + ground_only (shader
  de cores chapadas + Shift+B); fica fácil ver reta, canto faltando e emenda. DETALHES
  (natureza, objetos): desligar os dois.
- Antes de dizer que terminou: `check_region` na área toda e `screenshot` com zoom 1
  (width/height em volta da área). Screenshot da tela inteira esconde peça faltando.
- Florestas com clareiras; evitar blocos retangulares de árvores.

## Manchas de chão e cavernas (lições de uma sala que ficou ruim)
- Mancha (ex.: pedra lisa cinza sobre chão de rocha) tem contorno ORGÂNICO: reentrâncias,
  diagonais longas, largura variando. Nunca retângulo, nunca "moldura" reta, nunca um dente
  quadrado no meio de um lado.
- Profundidade no Tibia vem do SPRITE da borda: alguns chãos têm borda mais alta/grossa que
  outros (ex.: `frozen mud (thick border)`, conjunto 46). A borda grossa do chão "mais alto"
  por cima do chão vizinho é o que faz parecer desnível. Qual chão domina (e em qual lado vai
  a borda grossa) se decide pelo exemplo do mapper, não por regra fixa: leia um trecho dele.
- CADA TRECHO DO CONTORNO TEM UM DONO SÓ. Numa mancha (ex.: pedra cinza `ravendawn stone
  ground` sobre `frozen mud`), divida o contorno: num lado a borda da mancha vai por cima do
  chão em volta; no outro lado é o chão em volta que "ganha" e põe as bordas DELE por cima da
  mancha, e ali a borda da mancha NÃO existe. Nunca as duas bordas no mesmo trecho: elas se
  cortam e aparece traço solto. Exemplo feito pelo mapper (inferno.otbm, x 4102-4108,
  y 3899-3903, andar 6):
  - oeste e norte: borda 250 da pedra cinza nos tiles de lama (e, cne, dne, s, cse...);
  - leste: borda GROSSA do marrom (46) nos tiles cinza da beira (dne; cne+cse; e+n+dse
    empilhadas no canto) — é o degrau que dá profundidade;
  - sul-leste: borda NORMAL do marrom (44) nos tiles cinza de baixo (cse, s) e dse no marrom.
  Mancha original do mapper (inferno.otbm, x 4059-4068, y 3917-3921, andar 6 — NÃO MEXER, só
  ler): só o OESTE é macio (borda 250 descendo em degraus cse/dse/cne/dne/e); em cima e à
  direita a borda grossa 46 do marrom sobre a pedra (n, dne, e, cne); fenda fina = fio de pedra
  cinza de 1 tile com a borda grossa 46 dos dois lados (dnw, w, cnw...), nunca peça solta.
  CANTO: onde a linha grossa de cima (46 `n`) termina à esquerda, o tile de marrom a oeste recebe
  o canto pequeno grosso (46 `cne`, 7084) EMPILHADO por cima da borda cinza (250 `e`), e o canto
  cinza acima dele (250 `cse`) sai. Sem isso a linha termina seca.
  O `border_pieces` dá a borda completa de UM dono; aqui chame para cada dono com `edges`
  (e `align: "inner"` para a borda do chão de fora por cima da mancha) e apague a parte do
  outro dono naquele trecho.
- Fenda/degrau escuro (a borda de desnível) corre ao longo de UM lado do contorno da mancha
  (ex.: o lado sudoeste), contínua, seguindo as diagonais do contorno. Nunca atravessa o
  meio da mancha e nunca fica em pedaço solto. No `border_pieces`, usar `edges` só com as
  peças daquele lado.
- Rochas grandes / espinhos escuros ficam na PAREDE da caverna, nunca no chão aberto nem em
  cima de uma mancha. Não empilhar uma encostada na outra, não fazer arranjo simétrico.
- Detalhes (pedrinhas, bolinhas pretas, ossos, poças escuras) esparsos e sem simetria,
  mais concentrados perto das paredes e das bordas das manchas.

## Paredes em RAW (a regra do wall brush)
- A parede é uma LINHA DE TILES PRÓPRIA em volta do piso: o anel de fora (norte, sul, leste,
  oeste e os cantos diagonais convexos). Nunca pôr parede em cima do piso da sala.
- A peça de cada tile sai dos vizinhos que também são parede, como o auto-border do wall brush
  (`wall_border_calculator.cpp`, tabela full; se o brush não tem aquele tipo, tabela half, que
  só olha N e W). Brushes com só horizontal/vertical/corner/pole (ex.: `wood wall`):
  vizinho N e W = corner; só N = vertical; só W = horizontal; nenhum = pole.
  Numa sala retangular: pole no canto superior esquerdo, horizontal no superior direito,
  vertical no inferior esquerdo, corner no inferior direito; junção onde uma parede desce
  e encontra outra vindo do oeste = corner.
- Portas e janelas vão no tile da linha, do tipo da linha (porta horizontal numa parede
  horizontal), ids do `walls.xml` do brush.
- Chão sob a parede: a parte do tile que a parede não cobre é a de baixo/direita. Nos
  trechos retos (horizontal/vertical seguidos, janelas, portas) o tile fica com o chão de
  FORA (grama, terra...), senão o piso aparece para fora e pelas janelas. Nos tiles onde a
  parede VIRA (pole, corner, junção, canto côncavo) e a sala está na diagonal sudeste, vai o
  PISO da casa, senão a grama aparece dentro da sala no canto.

## Objetos de vários tiles (compostos)
- Objeto grande do Tibia (caixão, lápide dupla, cama, mesa grande, árvore grande, estátua...)
  é COMPOSTO: uma peça por tile, cada uma com as próprias propriedades (bloqueio, uso).
  Um sprite grande sozinho só vale para o tile onde está: a parte que aparece no tile vizinho
  não bloqueia nada, dá para andar por cima.
- Sempre coloque TODAS as peças, cada uma no seu tile, na posição relativa certa. Nunca meia peça.
- Exemplos (data/1310): `wooden coffin` vertical = 1742 em (x,y) + 1743 em (x,y+1);
  horizontal = 1744 em (x,y) + 1745 em (x+1,y). `stone coffin` = 1410 em (x,y) + 1411 em (x,y+1).
- As composições estão nos doodad brushes (`data/<versão>/brushes/brushes.xml`, blocos
  `<composite>` com `<tile x=".." y="..">`). Na dúvida, procure lá pelo id ou pelo nome, ou leia
  um exemplo no mapa com `read_region` raw=true e copie a disposição das peças.

## O que não fazer
- Não mexer em andares diferentes do pedido.
- Não apagar áreas existentes sem avisar.
