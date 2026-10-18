# Geração de mapas pelo Claude (RME Redux)

Aqui está o trabalho em que o Claude (modelo da Anthropic) **constrói mapa sozinho dentro
do RME Redux**. Ele lê o mapa, coloca grounds, bordas, paredes e detalhes por id de item e
confere o próprio resultado. O trabalho segue o estilo do mapper do BlackTalon, e o mapper
corrigiu o Claude ao vivo durante toda a sessão.

Se você vai continuar este trabalho, leia nesta ordem:

1. este README (visão geral, onde está cada peça, como rodar);
2. [`historico.md`](historico.md): o que foi pedido, o que deu errado e como cada erro foi
   corrigido. Quase todas as regras de estilo nasceram de uma correção;
3. os guias de estilo em [`../claude_style.md`](../claude_style.md) e
   [`../claude_style/`](../claude_style/), que o Claude lê antes de construir;
4. o README da pasta do exemplo que te interessa (floresta, caverna...).

---

## Situação em 18/10/2026

| Peça | Estado |
|---|---|
| Ferramentas do editor para o Claude (MCP, 13 tools) | Funcionando. Testado em 28/09/2026 |
| Painel de chat dentro do editor (Ctrl+Shift+A) | Funcionando com o backend Claude Code |
| Animação ao vivo (câmera segue, cursor "Claude") | Funcionando |
| Guias de estilo (773 capturas analisadas + correções do mapper) | Escritos, em `scripts/claude_style*` |
| **Floresta com casa de druida** (`floresta_druida/`) | **Mapa completo**, aprovado pelo mapper. O `.otbm` final está na pasta |
| Correção de bordas num mapa do mapper (`correcao_bordas_andar6/`) | A técnica foi aprovada ("você conseguiu replicar"). Uma mancha ficou com erros apontados e não foi refeita. O mapa não está incluso |
| Caverna de dragão v1, 3 andares (`caverna_dragao_v1/`) | **Interrompida**: os rios de lava saíram quadriculados |
| Caverna de dragão v2, 3 andares (`caverna_dragao_v2/`) | **Interrompida**: estrutura pronta, grounding pela metade, sem detalhes |
| Gravação time-lapse (botão REC) | Escrita. Aguardava build/teste em 28/09 |

Os scripts Python desta pasta foram **recuperados das transcrições** da sessão de
28/09/2026. A pasta temporária onde eles viviam foi apagada. A recuperação reaplicou cada
escrita e cada edição na ordem original, então cada arquivo está como estava no fim da sessão.
Eles são **scripts de trabalho**, não uma biblioteca: coordenadas fixas, um arquivo por
etapa, e às vezes um script novo que corrige o anterior. Isso foi mantido de propósito,
porque a sequência mostra como o resultado foi chegando.

---

## Onde está cada coisa

### Nesta pasta (`scripts/claude_mapgen/`)

```
claude_mapgen/
├── README.md                  ← este arquivo
├── historico.md               ← linha do tempo da sessão de 28/09: pedido → erro → correção → regra
├── .gitignore                 ← ignora o que os scripts geram (json intermediário, screenshots, .npy)
│
├── floresta_druida/           ← MAPA COMPLETO: floresta + casa do druida + riacho + ponte + estrada
│   ├── README.md              ← ordem de execução, o que cada script faz, arquivos gerados
│   ├── rme_mcp.py             ← cliente MCP (CLI e módulo)
│   ├── grounds.json           ← ids/chances dos 6 grounds usados (tirado de brushes.xml)
│   ├── forest.py … trail.py   ← 15 etapas, na ordem em que foram aplicadas
│   └── mapa/claude_floresta.otbm   ← o resultado final (cliente 10.98, área x 992-1055, y 992-1055, z 7)
│
├── correcao_bordas_andar6/    ← correção da transição pedra cinza × lama no mapa do mapper
│   ├── README.md
│   └── rme_mcp.py, fix_patch.py, fix_patches.py
│
├── caverna_dragao_v1/         ← 1ª tentativa de caverna de 3 andares em loop (interrompida)
│   ├── README.md
│   └── rme_mcp.py, cave_util.py, cave_gen.py
│
└── caverna_dragao_v2/         ← 2ª tentativa, com numpy/scipy (interrompida no grounding)
    ├── README.md
    └── rme.py, borders.py, layout.py, jag.py, build_tiles.py, grounding.py, …
```

### No resto do repositório

| Onde | O que é |
|---|---|
| [`scripts/claude_style.md`](../claude_style.md) | Guia de estilo principal. **Entra no system prompt** do painel e nas instruções do MCP. Editar este arquivo é o jeito de "ensinar" o Claude |
| [`scripts/claude_style/estrutura.md`](../claude_style/estrutura.md) | Montanha, rocha, altura/profundidade, água/lava, paredes, cidade |
| [`scripts/claude_style/piso-e-bordas.md`](../claude_style/piso-e-bordas.md) | Manchas, transições, entrelaçamento de bordas, caminhos ("nunca 3 retas") |
| [`scripts/claude_style/detalhes.md`](../claude_style/detalhes.md) | Onde vai cada detalhe, densidade, **clusters**, tufos de grama |
| [`scripts/claude_style/analise/`](../claude_style/analise/) | Notas imagem por imagem das 773 capturas dos mapas do mapper (de onde os guias saíram) |
| [`scripts/README.md`](../README.md) | Referência da API Lua do editor (o Claude lê antes de usar `run_lua`) |
| [`source/ai/claude_tools.cpp`](../../source/ai/claude_tools.cpp) | **As 13 tools** (`place`, `read_region`, `check_region`, `border_pieces`…). As regras do `check_region` estão aqui |
| [`source/ai/claude_agent.cpp`](../../source/ai/claude_agent.cpp) | `SYSTEM_HEADER`: as regras de trabalho do Claude (RAW, grounding view, conferir antes de dizer "pronto") |
| [`source/ai/external_mcp.cpp`](../../source/ai/external_mcp.cpp) | Servidor MCP em porta fixa (padrão 7777) com token, para Claude Code/Desktop de fora do editor |
| [`source/ai/mcp_server.cpp`](../../source/ai/mcp_server.cpp) | Servidor MCP mínimo (HTTP, JSON-RPC) em Boost.Asio |
| [`source/ai/assistant_live.cpp`](../../source/ai/assistant_live.cpp) | Animação ao vivo: aplica as edições aos poucos, câmera desliza, cursor "Claude", tudo num único undo |
| [`source/ai/claude_code_backend.cpp`](../../source/ai/claude_code_backend.cpp) | Painel → Claude Code headless (`claude.exe -p --input-format stream-json …`) |
| [`source/ai/claude_agent.cpp`](../../source/ai/claude_agent.cpp), [`claude_client.cpp`](../../source/ai/claude_client.cpp) | Backend alternativo pela API da Anthropic (chave própria) |
| [`source/ui/claude_panel.cpp`](../../source/ui/claude_panel.cpp) | O painel de chat (Window > Claude Assistant, Ctrl+Shift+A) |
| [`source/rendering/io/map_recorder.cpp`](../../source/rendering/io/map_recorder.cpp) | Time-lapse em .mp4 (botão REC no painel) |
| `config.toml` (raiz) | Chaves `claude_*` (modelo, backend, porta e **token** do MCP). **Nunca commitar** |

---

## Como as peças se encaixam

```
                 ┌────────────────────────── RME Redux (rme.exe) ──────────────────────────┐
 Claude Code ───►│ MCP server 127.0.0.1:7777  ──►  claude_tools.cpp  ──►  mapa aberto     │
 (VSCode/CLI)    │  (Bearer token do config)        (13 tools)           (aba ativa)      │
                 │        ▲                              │                                 │
 scripts Python ─┘        │                              └─► assistant_live (animação,     │
 (rme_mcp.py)             │                                  câmera, 1 undo por chamada)  │
                 │ Painel do Claude (Ctrl+Shift+A) ── claude.exe headless (plano Max)      │
                 └─────────────────────────────────────────────────────────────────────────┘
```

Há **três jeitos** de o Claude mexer no mapa, e todos passam pelas mesmas tools:

1. **Painel dentro do editor:** `Window > Claude Assistant` (Ctrl+Shift+A). O backend
   padrão roda o Claude Code headless (usa o login do `claude.exe`). O system prompt é
   `SYSTEM_HEADER` + `scripts/claude_style.md` + `scripts/README.md`.
2. **Claude Code de fora (VSCode/terminal):** no painel, marque **"MCP server, port"**
   e clique em **Copy setup**. Isso copia o comando
   `claude mcp add --scope user --transport http rme http://127.0.0.1:7777/mcp --header "Authorization: Bearer <token>"`.
   As tools aparecem como `mcp__rme__*` nas conversas iniciadas **depois** do registro e
   com o RME aberto.
3. **Scripts Python** (esta pasta): o `rme_mcp.py` fala JSON-RPC direto com o mesmo
   servidor. Foi assim que a floresta foi feita: o Claude escrevia um gerador, rodava
   `preview` (desenho ASCII, sem tocar no mapa), conferia e depois rodava `apply`.

### As 13 tools

| Tool | Para que serve |
|---|---|
| `get_editor_state` | Mapa, andar, centro da vista, zoom, seleção. Sempre a primeira chamada |
| `list_brushes` | Nomes de brushes por tipo, com filtro |
| `read_region` | Lê uma área. `raw=true` (máx 32x32) devolve a pilha de ids de cada tile, **com as bordas**. É assim que o Claude estuda como o mapper montou um trecho |
| `place` | **A ferramenta principal.** Edição RAW por id: `clear`, `ground`, `remove`, `add` por tile. Sem auto-border; valida tudo antes e é 1 undo |
| `list_borders` | Peças de cada conjunto de borda por direção (`n e s w`, `cnw cne csw cse`, `dnw dne dse dsw`) |
| `border_pieces` | Calcula a borda padrão completa de um conjunto em volta de uma mancha (outer/inner, filtro `edges`) e devolve o JSON pronto para o `place` |
| `check_region` | Acha erros que o screenshot esconde: grounds encostando sem borda, borda flutuando, item composto faltando peça, retas de 6+ tiles |
| `paint` | Pinta com brush de verdade (auto-border). **Só quando o mapper pede** |
| `run_lua` | Roda Lua no editor (geração procedural). As edições ficam animadas e viram 1 undo |
| `screenshot` | PNG da vista. Com `zoom` e `width/height` dá close-up; com `grid`, a grade de tiles |
| `set_view` | Move e dá zoom na vista do mapper (animado) |
| `set_display` | "Grounding view": shader Flat Colors + só grounds (Shift+B) + grid |
| `undo` | Desfaz N passos |

---

## Como rodar os scripts

**Pré-requisitos:**
- RME Redux aberto, com o servidor MCP ligado (painel do Claude → "MCP server, port", 7777).
- O mapa certo **na aba ativa**. As tools agem só na aba ativa e não sabem criar mapa
  novo: abra com `File > New`, com o **mesmo cliente** dos ids (10.98 para estes exemplos).
- Python 3 (a sessão usou o 3.12). A floresta, a correção e a caverna v1 só usam a
  biblioteca padrão. A caverna v2 precisa de `numpy`, `scipy` e `Pillow`.

**Token:** os clientes (`rme_mcp.py`, `rme.py`) leem `claude_mcp_port` e
`claude_mcp_token` do `config.toml` na raiz do repositório (onde o `rme.exe` roda). Para
apontar para outra instalação, use as variáveis `RME_MCP_TOKEN` e `RME_MCP_PORT`. O token
**nunca** vai no código.

**Uso típico:**

```bat
cd scripts\claude_mapgen\floresta_druida
python rme_mcp.py --list                      :: confere a conexão (lista as tools)
python rme_mcp.py get_editor_state
python forest.py preview                      :: desenho ASCII, não toca no mapa
python forest.py apply                        :: aplica (1 passo de undo por place)
python rme_mcp.py undo "{\"steps\": 1}"       :: desfaz se ficou ruim
```

Cada `place` é **um** passo de undo, e alguns scripts fazem mais de um `place` (o
`stream.py apply` faz 3). Desfaça o número certo de passos antes de reaplicar.
Os screenshots do `rme_mcp.py screenshot` são salvos como `shot_*.png` ao lado do script.

---

## O método que funcionou

Isto está espalhado pelos guias e pelo `SYSTEM_HEADER`. Aqui vai junto, porque foi o
que mais fez diferença na qualidade:

1. **Ler antes de construir.** `read_region raw=true` num trecho parecido feito pelo
   mapper, para copiar a técnica peça por peça (que borda vai por cima de qual, onde há
   chão de cor parecida por baixo). Nunca chutar id: as peças vêm do `list_borders`.
2. **Tudo em RAW.** O estilo do mapper (bordas quebradas e empilhadas à mão, contornos
   irregulares) não sai do auto-border. `paint` só se o mapper pedir.
3. **Por etapas:** forma → olhar → bordas → detalhes. Cada etapa com `preview` antes de
   `apply`.
4. **Grounding view** enquanto mexe em chão e borda: `set_display flat_colors=on,
   ground_only=true`. Retas, cantos faltando e emendas saltam aos olhos. Desligar para
   os detalhes.
5. **Conferir antes de dizer "pronto":** `check_region` em tudo o que mudou, depois
   `screenshot` com **zoom 1 e recorte** da área. O screenshot de tela inteira é
   reduzido a ~14 px por tile e esconde quase todos os erros. Isso foi descoberto
   comparando o mapa do Claude com o do mapper.
6. **Medir as regras no código** antes de aplicar. Os scripts medem as retas de borda
   na prévia e geram várias versões até achar uma sem defeito, em vez de sortear e
   remendar.
7. **Errou? `undo` e refaz**, em vez de construir por cima.

---

## O que melhorar (ideias e problemas em aberto)

Os itens estão em ordem de impacto estimado:

- **Levar as regras do mapper para o `check_region`.** Hoje ele só acusa retas de **6+**
  tiles (`STRAIGHT_RUN` em `claude_tools.cpp`), mas a regra do mapper é **no máximo 2**.
  Também valeria detectar: canto empilhado sobre a reta de margem ("lasca solta"), ponta
  de 1 linha, escadinha diagonal regular, uma borda dominando o contorno inteiro de uma
  mancha (falta de entrelaçamento) e o mesmo cluster repetido perto de si mesmo. Cada
  regra virando checagem automática é uma correção a menos que o mapper precisa fazer.
- **Transformar os scripts da floresta numa biblioteca.** Hoje cada script tem
  coordenadas fixas (`X0, Y0 = 992, 992`) e depende de JSONs gerados pelo anterior. As
  peças reaproveitáveis são:
  - o cálculo de bordas com dono por tile e o entrelaçamento (`forest.py`, `weave*.py`);
  - a margem por padrões que não se repetem (`stream.py`);
  - o caminho com lados independentes e checagem de retas (`trail.py`);
  - os clusters por Poisson (`clusters.py`, `tufts.py`);
  - as paredes pela regra do wall brush (`walls2.py`).
- **Terminar a caverna.** A v2 tem uma boa estrutura (layout com numpy, loop entre 3
  andares, quebra de retas com `jag.py`). Faltam o grounding completo e os detalhes. A
  v1 morreu nos rios de lava quadriculados: veja os READMEs das pastas.
- **Uma tool para criar mapa novo.** Hoje o mapper precisa fazer `File > New` e deixar a
  aba ativa.
- **Investigar o "undo deixou resto".** Depois de vários `undo 3` + reaplicar no riacho,
  sobraram 51 tiles de água e 27 peças de barranco de versões antigas, e a causa não foi
  reproduzida. O `stream.py` ganhou um modo `fix` que varre e limpa os restos.
- **`list_borders` com `filter`** pareceu ignorar o filtro em 28/09 (anotado durante o
  riacho). Vale conferir.
- **Abordagens vizinhas que já existem no repo:**
  - *Cave Warp* (`source/editor/cave_warp*.cpp`, Tools › Cave Warp): pincel C++ que
    redesenha uma forma copiando pilhas de uma caverna-modelo ("texture by numbers").
    Ataca o mesmo problema da caverna pelo outro lado: aprender do exemplo em vez de
    escrever regras;
  - *Map Synthesizer* (`scripts/map_synthesizer/`): silhueta + WFC a partir de amostras.

---

## Cuidados

- **Nunca commitar o `config.toml`**: ele guarda o token do MCP (e a chave da API, se
  usada).
- Os mapas originais do mapper (ex.: `inferno.otbm`) servem **só para leitura**. Nunca
  rode um `apply` com o mapa do mapper na aba ativa sem querer: confira com
  `get_editor_state` antes.
- Edição por id é RAW. **Nunca** chame `tile:borderize()`/`tile:wallize()` nas áreas do
  mapper, porque trocam bordas e paredes feitas à mão por automáticas.
- Itens grandes são compostos de uma peça por tile (caixão, cama, mesa grande, árvore
  grande). Coloque todas as peças nas posições certas: a propriedade de uma peça não vale
  para o tile vizinho.
