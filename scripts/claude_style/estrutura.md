# Estrutura: montanha, rocha, paredes, água

Tirado da análise de 773 capturas dos mapas do mapper (notas por imagem em `analise/`).
Os exemplos (ssN) apontam para as notas: `analise/ingame_*.md`, `analise/old_*.md` e
`analise/showoff_rme_cidade.md`.

## Contorno natural: nunca reto
- Rocha, montanha, costa, margem de lava e beira de abismo NUNCA fazem linha reta longa.
  O contorno avança e recua em degraus de 1-2 tiles, com dentes, bicos, baías e
  "penínsulas" em várias escalas. Corredores alargam e estreitam (2-5 tiles).
  (ingame ss6, ss12, ss101, ss141, ss203, ss290, ss401, ss516, ss566; old ss33)
- A massa de rocha corre de preferência em faixas DIAGONAIS (topo-esquerda para
  baixo-direita), não em blocos retangulares. Corredores e espinhaços na diagonal
  dão movimento e profundidade. (old ss1, ss8, ss37; ingame ss610, ss650; showoff 3, 6)
- Montanha empilhada em massa com contorno "dente de serra" de dentes de tamanhos
  diferentes. (showoff 6_waterfalls, animera1)
- Uma clareira no meio da rocha vira um losango irregular, não um quadrado. (ss566)

## Altura e profundidade (é tudo sprite)
- A altura é fingida com a BORDA GROSSA do terreno mais alto por cima do mais baixo
  (rocha, lama, penhasco, platô de cascalho sobre areia/capim). (ss179, ss224, ss289,
  ss372, ss373, ss387, ss397; old ss22, ss38)
- Penhascos e terraços descem em ESCADA DIAGONAL; às vezes com água ou lava descendo
  entre eles (cachoeira em série). (ss180, ss182, ss206, ss242, ss579, ss598; old ss9)
- Montanha usada como muro de arrimo entre dois níveis. (ss571, ss587)
- Beira sobre o vazio/abismo se desfaz em pontas, "dentes" pendentes e ilhotas soltas.
  (showoff 2_gateway, 8_shipcopter; ss579)
- Ponte quebrada mostra a espessura do piso. (ss455)

## Pé da montanha e da parede
- A rocha não encosta seco no chão: no pé vai uma FAIXA DE TRANSIÇÃO (cascalho, areia
  clara, terra, pedregulhos) que entra no chão em "línguas". (ss7, ss11, ss21, ss48;
  ss529, ss545)
- Cantos côncavos e base de paredes/prédios recebem pilhas de pedregulho/entulho que
  "ligam" a estrutura ao chão. (ss204, ss220, ss426, ss456, ss461, ss497; old ss26)
- Terra vermelha/escura no pé das rochas imita a face da encosta. (ss158, ss159, ss161)
- Halo escuro ou poeira pontilhada em volta de montanhas e esculturas. (ss309, ss332)

## Água, lava, ácido
- Margens recortadas que se encaixam nas reentrâncias da parede; espuma onde a água bate
  na rocha. (ss108, ss176, ss191, ss206, ss228, ss285)
- Água corre em diagonal e vira cachoeira ao cruzar desnível. (old ss1, ss38, ss39)
- Lava em FENDAS/veios estreitos e diagonais que saem da borda para dentro do chão, não
  em lagos redondos; sempre com chamas NA MARGEM (fogo quase nunca em chão seco).
  (ss108, ss125, ss153, ss185, ss216, ss237; old ss43, ss44, ss63)
- Costas alternam tipos de borda (espuma, barranco de terra, rocha) e têm ilhotas com
  pedrinhas na água. (showoff 9_shirahmun, 4_eternal)
- Riacho em RAW (data/1098): água = `sea` 4608-4619; margem de terra = conjunto 5
  (n 4644, e 4645, s 4646, w 4647, cnw 4652, cne 4653, csw 4654, cse 4655, dnw 4648,
  dne 4649, dsw 4650, dse 4651), peça no tile de TERRA com o nome do lado onde está a água,
  por cima das bordas de terra desse tile. 4632-4643 é margem de areia (praia).
- Margem nunca em patamar: NUNCA mais de 2 peças retas seguidas (horizontal ou vertical);
  depois sempre uma quebra. A quebra vem SÓ do DESENHO da água (degrau/dente de 1 tile:
  as peças viram cantos e diagonais); se o dente não cabe no meio da reta, põe na ponta.
  NUNCA empilhar canto sobre peça reta de margem (ex.: `s` + `cse`): vira uma lasca de
  barranco solta na água. NUNCA trocar a reta por diagonal sem mudar a água (`dsw` numa
  margem reta): a água encosta na grama do tile de cima sem borda (emenda seca).
- Nenhum trecho igual ao anterior: a margem se COMPÕE com padrões curtos que nunca se
  repetem em seguida (patamar de 1-2, entalhe de 1, baía pequena 1-2-1, baía 1-2-2-1, ponta
  de terra avançando na água, zigue-zague), cada margem com a sua sequência. Escadinha
  diagonal regular (mesmo degrau repetido) também é "igual": se tem espaço, quebra com baía
  ou ponta. Baía só em terra livre (sem trilha/casa/ponte perto) e sem juntar com a baía da
  outra margem (vira lagoa).
- Ponte de tábuas: 5768-5770 (são GROUND: substituem a água). Corta o riacho em ângulo reto
  (riacho reto sob a ponte), com as tábuas também no tile da margem de cada ponta, SEM peça
  de barranco por cima, e a trilha de terra chegando nas duas pontas. Ponte de corda: 3757
  (N-S) / 3756 (L-O).
- Beira d'água em clusters: juncos 2773/2775/2779 e taboas 2774/2776 na margem; vitórias-
  régias 2755-2758 e pedras 3329-3333 (grande + pequenas) na água.

## Arquitetura
- Construção é reta, mas RECORTADA: nichos e recuos a cada 2-4 tiles, pilar em cada
  quina. (ss508, ss509, ss511)
- Simetria só na arquitetura formal (templo, portão, palácio, salão de ritual): pares de
  estátuas/braseiros/tochas/pilares nas entradas e escadas; ritmo fixo (tocha/pilar a cada
  ~4-6 tiles). O entorno natural e a sujeira NÃO seguem o espelho. (ss30, ss307, ss319,
  ss458, ss503, ss606, ss668; old ss3, ss16, ss60)
- Colunas em fileira (2x2, 2x3, 2x4); uma tombada ou suja quebra a perfeição. (ss617,
  ss669, ss678)
- A natureza INVADE a arquitetura: rocha, gelo, cristal, lava ou hera avançam sobre piso e
  muralha, que termina sem fechamento; cidade/templo termina contra montanha irregular,
  às vezes com escada entalhada. (ss23, ss78, ss95, ss604, ss672, ss689)
- Base de muralhas e zigurates escondida com rochas, vegetação ou poeira. (ss305, ss392)
- Ruína = parede interrompida + colunas tombadas + pedregulhos + pó + musgo/hera; o piso
  construído se desfaz contra a rocha. (ss115, ss172, ss532, ss603, ss682; old ss46, ss47)
- Módulos reaproveitados: o mesmo layout de sala com outro ground/decoração/cor.
  (ss229-ss230, ss402-ss403, ss414)
- Objetos grandes compostos (uma peça por tile), muitas vezes orientados na diagonal.
  (ss136, ss167, ss301, ss320, ss394)

## Composição da cena
- Um ponto focal claro por cena (portal, ponte, navio, altar), alcançado por um caminho.
  (showoff 2, 4, 6, 8)
- Linha reta só onde há mão humana: rua, lote, canal, muralha. (animera3, city)

## Cidade (city_full)
- Hierarquia de ruas: avenida larga de piso claro, rua de bairro de piso escuro, becos.
- Casas variam material, altura, orientação do telhado e planta (L/T/U); a frente das casas
  avança e recua. Bairros se distinguem pelo telhado e pela quantidade de natureza.
- Calçamento uniforme; a passagem para a grama é mureta, meio-fio ou cerca, com tiras e
  triângulos de grama entre rua e casa. (old ss5, ss19, ss61; ss24, ss57, ss584)
