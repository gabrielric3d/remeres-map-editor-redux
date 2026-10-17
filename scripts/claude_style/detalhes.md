# Detalhes: natureza, objetos, luz

Tirado da análise de 773 capturas (notas em `analise/`).

## Onde vai
- CENTRO LIVRE, DECORAÇÃO NAS BORDAS: plantas, ossos, carga, móveis, fogo ficam colados a
  paredes, rochas, margens e cantos; o meio fica para circular ou para UMA peça focal.
  (ss103, ss121, ss216, ss401, ss530, ss612, ss627; animera1, 5_darkcave)
- A vegetação marca as BORDAS e as linhas de transição, abraça a base das rochas e esconde
  quinas e linhas retas (cercas, muralhas, margens). (ss26, ss89, ss208, ss297; old ss3,
  ss12, ss48; showoff 4, 6, 9)
- A vegetação segue a UMIDADE: perto da água; rocha seca fica vazia. (old ss9, ss10, ss21)
- Cada objeto só na sua zona (ossos na lama, tentáculos na terra corrompida, sujeira só nas
  celas). (ss407, ss463, ss476)
- Teias, ossos e fungos nas QUINAS internas entre parede e chão; teia = abandono.
  (ss5, ss13, ss252, ss300, ss618; old ss46, ss47)
- Objetos de uso bloqueiam parte do caminho (carroça, caixote, barril). (ss3, ss20, ss54)

## Densidade
- Degradê: pedrinhas e cascalho densos junto à rocha/parede e raros no meio do chão;
  entulho = monte central com pedrinhas cada vez mais esparsas para fora.
  (ss462, ss497, ss550, ss600)
- Pedrinhas/entulho em quase toda superfície dura para quebrar a textura; "sujeira
  irregular sobre ordem" (folhas, palha, ossos espalhados sobre móveis alinhados ou
  lajotas em grade). (ss19, ss54, ss63, ss321, ss382; old ss26, ss37)
- Selva pode chegar a ~1 planta por tile; o resto é esparso. (ss50, ss59, ss60)

## Clusters (método do mapper para floresta e natureza)
- Não espalhar árvore/detalhe tile a tile com a mesma chance: vira um padrão de repetição e
  distância que o jogador percebe (mesmo espaçamento, tudo parecido).
- Primeiro montar CLUSTERS: grupos compostos pequenos (ex.: 2-3 árvores + samambaias +
  cogumelo; pedra grande + pedrinhas + samambaia; canteiro de flores + arbusto; árvore
  solitária + arbusto + flores). Depois posicionar os clusters no mapa com distância
  VARIADA entre eles e nunca repetir o mesmo cluster perto de outro igual (espelhar/girar e
  tirar uma peça ou outra também ajuda).
- Variar a densidade pela área: trechos de mata fechada, trechos médios e espaços abertos
  (clareiras/gramado com um grupo solto aqui e ali). Nada de densidade uniforme.
- Mata fechada = copa densa sobre o chão de floresta, com a densidade oscilando dentro da
  mancha e mais rala na beira; espécies em blocos (pinheiros numa área, folhosas em outra).
- Exemplo do mapper: clusters de planta vermelha + garras + pedrinhas do mapa Satan Citadel
  (blacktalon), montados à parte e espalhados sem repetir.
- Tufos de grama também em clusters (mancha irregular, mais densa no meio, uma família
  principal + poucos de outra), puxados para as transições (beira da mata, trilha, pé de
  parede, pedras) e raros no gramado aberto. Na grama verde (`grass`) o mapper usa o tufo
  6216-6219, com o MENOS visível mais frequente e o mais visível raro: 6219 (pontinhos) ~60,
  6218 ~45, 6217 ~25, 6216 (moita escura) ~5, só como acento. 10914-10916 (capim alto) só na
  beira da mata. 45792+/45748+ são secos e 51429+/53457+ pálidos: só em bioma seco.
- `birch` 8313 e `dead tree` 8314 são árvores SEM folha: só como acento de árvore morta,
  nunca como espécie da mata verde.

## Grupos (nunca item isolado)
- Em CACHOS de 3-5/6 do mesmo tipo, muitas vezes em diagonal: cristais em trio, cogumelos,
  samambaias com uma pedra no meio, barris empilhados, pinheiros sobrepostos com árvores
  secas nas pontas. (ss311, ss347, ss433, ss540, ss572, ss574)
- Pedra grande + 2-3 pedrinhas; pedrinhas escuras em trios. (ss222, ss211, ss624)
- Plantas em grupos de 2-4 espécies DIFERENTES; árvores da mesma espécie em bloco com
  arbustos esparsos e espaçamento irregular. (ss9, ss11, ss50, ss71)
- "Kits" por bioma: floresta = planta + capim + florzinhas; deserto = rocha vermelha +
  rocha bege + pedregulho; selva de caverna = mancha limão + planta carnívora.
  (ss331, ss358, ss387, ss388)
- Estações temáticas/pontos focais compostos: fogueira + tocos + barril + restos; caldeirão
  + armas; altar + tochas + crânios; árvore morta + teia + arbusto dourado; rocha + caveira
  + teia; caixão + ossos + teia. (ss9, ss14, ss34, ss469, ss471; old ss6, ss53)
- Combinações recorrentes: poça de ácido + bolsa + teia (ss176); canteiro de lava + planta
  carnívora nas quinas (ss197, ss199); cristal luminoso + pedras na parede (ss110, ss183);
  cristal grande + planta pequena, barris em trio + carroça, rocha + palmeira (showoff).
- Motivo repetido e ESPAÇADO dá textura a áreas grandes (ninho de brasa + veios; canteiro +
  palmeira + tocha). (ss582, ss585, ss588)
- Elementos LINEARES ligam manchas: raízes, cipós, troncos caídos, canos, veios de lava,
  pontes. (ss250, ss293, ss537, ss561, ss583)

## Luz e fogo
- Luz embutida na estrutura: brasas/lava nas paredes, tochas nas quinas, colunas e em PARES
  nas entradas; cristais e cogumelos luminosos como decoração em áreas escuras.
  (ss14, ss50, ss62, ss629, ss660, ss536, ss575)
- Cor do fogo/luz combina com o tema (azul no gelo, verde no musgo). (ss667, ss685)
- Fogo "preso ao chão": chamas na margem da lava ou com fissura vermelha; tochas cercadas de
  musgo/cogumelos/raízes. Lava sempre com chamas na borda. (ss306, ss315, ss327, ss216)

## Cidade
- Poste de luz ao lado da porta; postes roxos, pinheiros e cercas-vivas alternando em ritmo
  irregular pela rua; barril + lanterna + poste em cada fachada; carroça/barril nos nichos;
  bancos alternados com sebes; trepadeiras e árvores suavizando muralhas.
  (ss262, ss267, ss297; old ss49, ss55, ss58, ss61; city)

## Ruína
- Musgo, trepadeiras, raízes e entulho por cima; pedra e rocha invadindo a grade de lajotas.
  (ss315, ss330, ss422, ss451, ss454)
