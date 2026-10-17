# Piso e bordas

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
