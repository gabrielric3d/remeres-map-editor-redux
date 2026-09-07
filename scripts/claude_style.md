# Guia de estilo do mapa (lido pelo Claude Assistant)

Este arquivo entra no prompt do Claude Assistant (Window > Claude Assistant) toda vez
que uma conversa começa (ou depois de "Clear"). Escreva aqui como o seu mapa deve ser
feito: brushes por papel, regras estruturais, o que evitar. Pode ser em português.
Quanto mais concreto (nomes exatos de brushes, tamanhos, densidades), melhor.

## Brushes por papel
<!-- Substitua pelos nomes reais (Brushes Editor / list_brushes). Exemplo: -->
- Grama principal: `grass`
- Terra / caminho: `dirt`
- Água: `sea`
- Montanha: `mountain`
- Parede de casa comum: `wooden wall`
- Árvore comum: `trees` (doodad) — densidade ~8% em floresta
- Pedras soltas: `stones` (doodad) — densidade ~2%

## Regras estruturais
- Casas: mínimo 5x5 externo, porta sempre voltada para a rua.
- Caminhos com 2 tiles de largura entre áreas importantes.
- Nunca colocar água encostando em montanha sem uma faixa de terra.

## Estilo visual
- Bordas sempre por auto-border (usar a ferramenta `paint`, não item id).
- Florestas com clareiras; evitar blocos retangulares de árvores.

## O que não fazer
- Não mexer em andares diferentes do pedido.
- Não apagar áreas existentes sem avisar.
