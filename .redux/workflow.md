# RME Redux Workflow Guide

Sistema de orquestração multi-agente para desenvolvimento do RME Redux (editor de mapas C++/wxWidgets).

## Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                      RME REDUX COMPLETE FLOW                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│   /redux [request]                                                │
│         │                                                         │
│         ▼                                                         │
│   ┌─────────────┐                                                 │
│   │   PLANNER   │ ──► Creates plan in .redux/plans/               │
│   └─────────────┘                                                 │
│         │                                                         │
│         ▼                                                         │
│   /redux:execute [plan]                                           │
│         │                                                         │
│         ▼                                                         │
│   ┌─────────────┐                                                 │
│   │  EXECUTOR   │ ──► Implements code changes                     │
│   └─────────────┘                                                 │
│         │                                                         │
│         ▼                                                         │
│   Creates summary in .redux/summaries/                            │
│         │                                                         │
│         ▼                                                         │
│   /redux:review                                                   │
│         │                                                         │
│         ▼                                                         │
│   ┌─────────────┐                                                 │
│   │  REVIEWER   │ ──► Creates review in .redux/reviews/           │
│   └─────────────┘                                                 │
│         │                                                         │
│         ▼                                                         │
│   /redux:commit                                                   │
│         │                                                         │
│         ▼                                                         │
│   Git commit (single repo)                                        │
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## Specialized Agents

### Planner Agent
**File**: `.redux/agents/planner.md`

Analisa requests do usuario e cria planos detalhados de implementacao. Ele:
- Analisa o escopo completo do request
- Determina quais modulos/camadas precisam de modificacao
- Quebra o trabalho em tasks discretas e acionaveis
- Identifica oportunidades de execucao paralela
- Cria planos estruturados que o executor pode seguir

---

### Executor Agent
**File**: `.redux/agents/executor.md`

Executa os planos criados pelo planner. Ele:
- Le o plano e extrai as tasks
- Executa tasks por fase, respeitando dependencias
- Implementa codigo C++/wxWidgets seguindo as convencoes do rme_redux
- Registra novos arquivos no CMakeLists.txt
- Produz um summary de execucao

---

### Reviewer Agent
**File**: `.redux/agents/reviewer.md`

Revisa as alteracoes de codigo buscando:
- Memory leaks (raw pointers vs unique_ptr)
- Uso correto do action system (unique_ptr, std::move)
- Convencoes do projeto (Theme system, brush templates, etc.)
- Bugs logicos e edge cases
- Performance em operacoes de mapa

---

## Available Commands

| Command | Description | When to Use |
|---------|-------------|-------------|
| `/redux [request]` | Creates detailed plan | Start of workflow |
| `/redux:execute [plan]` | Executes plan tasks | After planning |
| `/redux:review` | Reviews code quality | After execution |
| `/redux:commit` | Commits changes | Finalization |

---

## Folder Structure

```
.redux/
├── agents/              # Agent instruction files
│   ├── planner.md       # Planner instructions
│   ├── executor.md      # Executor instructions
│   └── reviewer.md      # Reviewer instructions
│
├── plans/               # Implementation plans
│   ├── 01_Plan_Feature_Name.md
│   └── ...
│
├── summaries/           # Execution results
│   ├── 01_Summary_Feature_Name.md
│   └── ...
│
├── reviews/             # Review reports
│   ├── 01_Review_Feature_Name.md
│   └── ...
│
└── workflow.md          # This file
```

### Sequential Numbering
- Plans numbered sequentially: `01`, `02`, ...
- Summaries correspond to plans: `01_Plan_X` → `01_Summary_X`
- Reviews correspond to plans: `01_Plan_X` → `01_Review_X`

---

## Project Architecture Reference

### Source Structure
```
source/
├── app/           # Application entry, settings, main frame
├── assets/        # Asset management
├── brushes/       # All brush types (ground, wall, doodad, etc.)
├── editor/        # Editor core (action system, selection, copybuffer)
├── ext/           # External libraries
├── game/          # Game objects (items, creatures, sprites)
├── ingame_preview/# In-game preview system
├── io/            # File I/O (OTBM, XML, etc.)
├── item_definitions/ # Item type definitions
├── live/          # Live collaboration
├── map/           # Map, Tile, Position, TileOperations
├── net/           # Network
├── palette/       # Palette panels
├── rendering/     # OpenGL rendering
├── ui/            # wxWidgets UI (dialogs, panels, windows)
└── util/          # Utilities
```

### Key Conventions
- **unique_ptr everywhere**: Actions, Changes, Items, Tiles
- **Action system**: `actionQueue->createBatch()` / `createAction()` / `std::move()`
- **Brush templates**: `brush->is<T>()` / `brush->as<T>()`
- **Theme system**: `Theme::Get(Theme::Role::X)`
- **CMakeLists.txt**: All new .cpp/.h files MUST be registered
- **NEVER build**: User compiles manually

---

## Complete Flow Example

```
User: /redux Criar um dialog de configuração de auto-borders

Claude: [Spawns Planner Agent]
        [Creates .redux/plans/01_Plan_AutoBorder_Config.md]
        [Presents summary]
        [Offers options: Execute/View/Modify/Cancel]

User: Executar agora

Claude: [Spawns Executor Agent with the plan]
        [Phase 1: Create dialog .h and .cpp]
        [Phase 2: Register in CMakeLists.txt]
        [Phase 3: Wire up menu/toolbar entry]
        [Creates .redux/summaries/01_Summary_AutoBorder_Config.md]
        [Offers options: Review/Commit/Finish]

User: Revisar código

Claude: [Spawns Reviewer Agent]
        [Analyzes all modified files]
        [Creates .redux/reviews/01_Review_AutoBorder_Config.md]
        [Presents result]
        [Offers commit options]

User: Commitar

Claude: [Generates semantic commit message]
        [User confirms]
        [Executes git add + commit]
        [Reports commit hash]
```
