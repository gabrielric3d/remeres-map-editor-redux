# Plan: External Shader Manifest (data-driven post-process shaders)

## Overview
Mover o código GLSL dos 4 efeitos de pós-processamento (`screen/none`, `scanline`, `xbrz`, `nevasca`) para arquivos externos em `data/shaders/` e carregá-los a partir de um manifesto XML, eliminando os registradores estáticos em `source/rendering/postprocess/effects/*.cpp`. Usuários poderão adicionar ou editar shaders sem recompilar.

## User Request
Mover os shaders pós-processamento (nevasca, scanline, xbrz, screen) do código hardcoded em `source/rendering/postprocess/effects/*.cpp` para arquivos externos em `data/shaders/`, carregados dinamicamente via manifesto. Manter o `PostProcessManager` existente (já é data-driven). Permitir que usuários adicionem/editem shaders sem recompilar.

## Analysis
- **New Files**:
  - `data/shaders/manifest.xml` (manifesto dos efeitos)
  - `data/shaders/none.frag` (passthrough, ex-`screen.cpp`)
  - `data/shaders/scanline.frag` (ex-`scanline.cpp`)
  - `data/shaders/xbrz.frag` + `data/shaders/xbrz.vert` (ex-`xbrz.cpp`, é o único com vertex shader customizado)
  - `data/shaders/nevasca.frag` (ex-`nevasca.cpp`)
  - `source/rendering/postprocess/shader_manifest_loader.h`
  - `source/rendering/postprocess/shader_manifest_loader.cpp`
- **Modified Files**:
  - `source/CMakeLists.txt` (remover 4 efeitos + adicionar o novo loader)
  - `source/rendering/map_drawer.cpp` (invocar o loader antes de `PostProcessManager::Initialize`)
  - `source/rendering/postprocess/post_process_manager.h` (manter `ShaderNames::` — continua sendo ponto de referência central para código C++)
- **Deleted Files**:
  - `source/rendering/postprocess/effects/screen.cpp`
  - `source/rendering/postprocess/effects/scanline.cpp`
  - `source/rendering/postprocess/effects/xbrz.cpp`
  - `source/rendering/postprocess/effects/nevasca.cpp`
  - (diretório `effects/` pode ficar vazio e ser removido do disco)
- **CMakeLists Update**: Sim — remover 4 entradas (linhas 695–698) e adicionar 1 nova (shader_manifest_loader.cpp).
- **Menu/Toolbar Wiring**: Não.
- **Action System**: Não.
- **Data Files (XML/TOML)**: Sim — `data/shaders/manifest.xml` (novo). As texturas auxiliares `data/shaders/nevasca.png` e `data/shaders/clouds.png` já existem.

## Design Decisions

### Formato do manifesto: XML
- Todo o resto de `data/` que descreve recursos gráficos/jogo é XML (`borders.xml`, `grounds.xml`, `walls.xml`, `tilesets.xml`, `materials.xml`, presets de dungeon).
- TOML no projeto é usado só para configuração de app/cliente (`config.toml`, `clients.toml`).
- `pugixml` já está linkado e é usado por todo o resto do código (ex: `source/item_definitions/formats/xml/xml_item_parser.cpp:98`).
- **Decisão**: XML, consistente com o padrão do diretório `data/`.

### Estrutura do manifesto
```xml
<?xml version="1.0" encoding="UTF-8"?>
<shaders>
    <!-- name: deve bater com ShaderNames::* para shaders conhecidos.
         frag: obrigatório, relativo a data/shaders/.
         vert: opcional, relativo a data/shaders/. Se ausente, usa o vertex padrão. -->
    <effect name="None" frag="none.frag"/>
    <effect name="Scanline" frag="scanline.frag"/>
    <effect name="4xBRZ" frag="xbrz.frag" vert="xbrz.vert"/>
    <effect name="Nevasca" frag="nevasca.frag">
        <texture uniform="u_Tex1" file="shaders/nevasca.png"/>
        <texture uniform="u_Tex2" file="shaders/clouds.png"/>
    </effect>
</shaders>
```
- Atributo `file` em `<texture>` continua relativo ao diretório `data/` (compatível com `RegisterAuxTexture` atual, que já monta o path via `FileSystem::GetDataDirectory()` em `post_process_manager.cpp:40`).
- Atributos `frag`/`vert` são relativos a `data/shaders/` (mais curto, o loader prefixa `shaders/` ao resolver).

### Constantes `ShaderNames::`
**Manter**. São referenciadas em:
- `source/rendering/core/drawing_options.cpp:63` (default `NONE`)
- `source/rendering/map_drawer.cpp:383` (comparação de string para decidir FBO)
- `source/rendering/ui/map_display.cpp:395` (flag de visual effects para `NEVASCA`)
- `source/ui/menubar/view_settings_handler.cpp:247,250,252` (toggle de shader)

O manifesto é só dados — o código C++ continua referenciando por nome via as constantes. Se o usuário mudar o `name="None"` no manifesto, quebra (documentar isso como contrato). O loader deve logar warning se nenhum efeito "None" for registrado (fallback crítico).

### Ponto de invocação do loader
Hoje, o `map_drawer.cpp:197` chama `PostProcessManager::Instance().Initialize(screen_vert)`. Antes dessa chamada, os registradores estáticos dos 4 `.cpp` já rodaram automaticamente na inicialização do programa. Após a migração:

1. `MapDrawer::InitPostProcess()` chama **primeiro** `ShaderManifestLoader::LoadFromDataDirectory()` (popula `PostProcessManager` via `Register`/`RegisterAuxTexture`).
2. Em seguida chama `PostProcessManager::Instance().Initialize(screen_vert)` como hoje.

Isso é idempotente (`Initialize` já tem guard `initialized`, e `Register` ignora duplicatas por nome — ver `post_process_manager.cpp:17-21`).

### Tratamento de erros
- Manifesto ausente: `spdlog::error` e registra apenas `None` com passthrough embutido mínimo (fallback absoluto para não crashar).
- Shader `.frag`/`.vert` ausente no disco: `spdlog::error("Missing shader file: ...")`, pula o efeito. `PostProcessManager::Initialize` já remove efeitos com shader nulo (linhas 117–121).
- XML malformado: `spdlog::error("Shader manifest parse error: ...")`, mesmo fallback.
- Aux texture ausente: comportamento já existente (`PostProcessManager::Initialize` loga erro mas não remove o efeito — ele ainda compila, só sem a textura).

### Fallback embutido mínimo
Para garantir que o editor nunca rode sem pelo menos o efeito `None`, o loader terá uma string embutida minúscula (3 linhas de GLSL passthrough) que é registrada **apenas se** o manifesto falhar em carregar `None`. Isso protege contra manifesto corrompido/deletado.

## Tasks

### Phase 1: Extract shader sources into external files

#### Task 1.1: Create `data/shaders/none.frag`
- **Description**: Extrair literal de `screen.cpp:5-15` para arquivo externo.
- **Files**: `data/shaders/none.frag` (novo)
- **Details**:
  - Conteúdo exato do `screen_frag` (passthrough `FragColor = texture(u_Texture, vTexCoord)`).
  - Sem trailing newlines extras; preservar `#version 450 core`.

#### Task 1.2: Create `data/shaders/scanline.frag`
- **Parallel**: Yes
- **Description**: Extrair `scanline_frag_source` de `scanline.cpp:5-137`.
- **Files**: `data/shaders/scanline.frag` (novo)
- **Details**: Copiar o literal inteiro, preservar comentários e indentação.

#### Task 1.3: Create `data/shaders/xbrz.frag` and `data/shaders/xbrz.vert`
- **Parallel**: Yes
- **Description**: Extrair `xbrz_vert` e `xbrz_frag_source` de `xbrz.cpp`.
- **Files**: `data/shaders/xbrz.frag`, `data/shaders/xbrz.vert` (novos)
- **Details**:
  - `xbrz.vert` é o vertex shader customizado (linhas 5–~30 de `xbrz.cpp`).
  - `xbrz.frag` é o fragment shader (o restante até a linha 261).
  - Este é o único efeito que fornece vertex customizado hoje.

#### Task 1.4: Create `data/shaders/nevasca.frag`
- **Parallel**: Yes
- **Description**: Extrair `nevasca_frag_source` de `nevasca.cpp:5-341`.
- **Files**: `data/shaders/nevasca.frag` (novo)
- **Details**: Copiar o literal GLSL inteiro (~336 linhas).

#### Task 1.5: Create `data/shaders/manifest.xml`
- **Parallel**: Yes
- **Description**: Criar o manifesto XML listando os 4 efeitos.
- **Files**: `data/shaders/manifest.xml` (novo)
- **Details**: Seguir o schema descrito em "Estrutura do manifesto" acima. Ordem importante: registrar `None` primeiro (é o fallback do `PostProcessManager::GetEffect` em `post_process_manager.cpp:141-143`).

### Phase 2: Implement manifest loader

#### Task 2.1: Create `ShaderManifestLoader` header
- **Depends On**: —
- **Description**: Header com API mínima pra carregar o manifesto.
- **Files**: `source/rendering/postprocess/shader_manifest_loader.h` (novo)
- **Details**:
  ```cpp
  #ifndef RME_RENDERING_POSTPROCESS_SHADER_MANIFEST_LOADER_H
  #define RME_RENDERING_POSTPROCESS_SHADER_MANIFEST_LOADER_H

  #include <string>

  class ShaderManifestLoader {
  public:
      // Reads data/shaders/manifest.xml and calls PostProcessManager::Instance()
      // .Register / .RegisterAuxTexture for each entry.
      // Returns true if at least one effect (including fallback "None") was registered.
      // Must be called BEFORE PostProcessManager::Initialize().
      static bool LoadFromDataDirectory();

  private:
      static void RegisterFallbackNone();
      static std::string ReadFile(const std::string& absolute_path);
  };

  #endif
  ```

#### Task 2.2: Implement `ShaderManifestLoader`
- **Depends On**: Task 2.1
- **Description**: Implementar parse XML + leitura dos arquivos .frag/.vert + chamadas ao `PostProcessManager`.
- **Files**: `source/rendering/postprocess/shader_manifest_loader.cpp` (novo)
- **Details**:
  - Usar `pugi::xml_document` (mesmo padrão de `xml_item_parser.cpp:98`).
  - Resolver path do manifesto: `FileSystem::GetDataDirectory()` + `"shaders/manifest.xml"`.
  - Se manifesto não existe ou parse falha: `spdlog::error(...)`, chamar `RegisterFallbackNone()`, retornar `false`.
  - Para cada `<effect>`:
    - Ler atributo `name` (obrigatório), `frag` (obrigatório), `vert` (opcional).
    - Resolver path absoluto: `data/shaders/<frag>`. Ler conteúdo via `ReadFile` (std::ifstream).
    - Se `vert` presente, ler também; senão passar string vazia (comportamento atual: `PostProcessManager::Initialize` usa `default_vertex_source` quando vazio — ver `post_process_manager.cpp:97-100`).
    - Se arquivo não existe: `spdlog::error(...)` e pular esse `<effect>` (continua nos próximos).
    - Chamar `PostProcessManager::Instance().Register(name, frag_src, vert_src)`.
    - Para cada `<texture>` filho: ler `uniform` e `file`, chamar `PostProcessManager::Instance().RegisterAuxTexture(name, uniform, file)`.
  - `ReadFile`: abrir ifstream em modo binário, ler para std::string via `std::istreambuf_iterator`. Retornar string vazia em falha e logar.
  - `RegisterFallbackNone`: registra um passthrough embutido mínimo (`#version 450 core\nin vec2 vTexCoord;\nout vec4 FragColor;\nuniform sampler2D u_Texture;\nvoid main(){FragColor=texture(u_Texture,vTexCoord);}`) com nome `ShaderNames::NONE`. Só chamado se o manifesto falhar completamente — se o manifesto carregou mas só teve shaders inválidos, ainda assim chamar para garantir.
  - Log informativo no final: `spdlog::info("ShaderManifestLoader: registered N effects")`.
  - Includes:
    - `"rendering/postprocess/shader_manifest_loader.h"`
    - `"rendering/postprocess/post_process_manager.h"`
    - `"util/file_system.h"`
    - `<pugixml.hpp>`
    - `<spdlog/spdlog.h>`
    - `<fstream>`, `<sstream>`
    - `<wx/filename.h>` (ou usar `std::filesystem` — checar o que o projeto prefere; `file_system.h` já usa wx).

### Phase 3: Integration

#### Task 3.1: Wire loader into MapDrawer initialization
- **Depends On**: Task 2.2
- **Description**: Chamar `ShaderManifestLoader::LoadFromDataDirectory()` em `MapDrawer::InitPostProcess()` antes de `Initialize(screen_vert)`.
- **Files**: `source/rendering/map_drawer.cpp` (modificar)
- **Details**:
  - Linha ~197 (antes de `PostProcessManager::Instance().Initialize(screen_vert);`):
    ```cpp
    // Load shader effects from data/shaders/manifest.xml
    ShaderManifestLoader::LoadFromDataDirectory();

    // Load Shaders
    PostProcessManager::Instance().Initialize(screen_vert);
    ```
  - Adicionar include no topo do arquivo: `#include "rendering/postprocess/shader_manifest_loader.h"`.

#### Task 3.2: Remove effect .cpp files and update CMakeLists
- **Depends On**: Task 3.1 (só remove depois que o loader está plugado)
- **Description**: Deletar os 4 arquivos em `source/rendering/postprocess/effects/` e remover do build.
- **Files**:
  - Deletar: `source/rendering/postprocess/effects/screen.cpp`, `scanline.cpp`, `xbrz.cpp`, `nevasca.cpp`
  - Modificar: `source/CMakeLists.txt` (linhas 695–698: remover as 4 entradas; adicionar `${CMAKE_CURRENT_LIST_DIR}/rendering/postprocess/shader_manifest_loader.cpp` na seção de postprocess)
- **Details**:
  - O diretório `source/rendering/postprocess/effects/` ficará vazio — pode ser removido ou deixado vazio (Git ignora dirs vazios).

### Phase 4: Polish

#### Task 4.1: Verify `ShaderNames::` constants still match manifest
- **Depends On**: Phase 3
- **Description**: Conferir que os valores literais de `ShaderNames::NONE="None"`, `SCANLINE="Scanline"`, `XBRZ="4xBRZ"`, `NEVASCA="Nevasca"` batem exatamente com os atributos `name=` no manifesto.
- **Files**: `data/shaders/manifest.xml`, `source/rendering/postprocess/post_process_manager.h`
- **Details**: Qualquer divergência quebra o lookup em `GetEffect`. Critical check.

#### Task 4.2: Add a short comment in `post_process_manager.h` explaining the contract
- **Depends On**: —
- **Description**: Documentar no header que efeitos são carregados de `data/shaders/manifest.xml` e que os nomes em `ShaderNames::` precisam bater com o manifesto.
- **Files**: `source/rendering/postprocess/post_process_manager.h`
- **Details**: Comentário de 2–3 linhas acima do namespace `ShaderNames`.

## Execution Order
1. Phase 1: Tasks 1.1 + 1.2 + 1.3 + 1.4 + 1.5 (todos paralelos — extração independente)
2. Phase 2: Task 2.1 → Task 2.2 (loader depende do header)
3. Phase 3: Task 3.1 → Task 3.2 (integração antes de deletar)
4. Phase 4: Task 4.1 + Task 4.2 (paralelos, sanity checks)

## Testing Notes
Como não compilamos, validação manual pós-build pelo usuário:

1. **Boot limpo**: rodar o editor. No log (`rme_debug.log` ou console) deve aparecer `ShaderManifestLoader: registered 4 effects` (ou similar) sem erros.
2. **Menu Preferences → Graphics → Screen Shader**: o dropdown deve listar `None`, `Scanline`, `4xBRZ`, `Nevasca` (ordem do manifesto). Igual antes.
3. **Trocar shader**: selecionar `4xBRZ`, aplicar, verificar que o upscale funciona.
4. **Selecionar Nevasca**: verificar que a neve aparece com os sprites de floco/nuvem (texturas auxiliares carregam OK).
5. **Editar manifesto**: alterar o path de um `.frag` para algo inválido, relançar, confirmar que o log mostra erro e o efeito cai silenciosamente (sem crash, dropdown mostra os demais).
6. **Deletar manifesto inteiro**: relançar, confirmar que o editor sobe só com `None` embutido (fallback), sem crash.
7. **Adicionar shader novo**: criar `data/shaders/myshader.frag` (passthrough) + linha `<effect name="MyShader" frag="myshader.frag"/>` no manifesto, relançar, confirmar que aparece no dropdown.
8. **Mudar o valor do atributo `u_var0` / uniform do Nevasca**: não faz parte desta migração; `map_drawer.cpp` continua setando uniforms por nome hardcoded. Isso é fora de escopo.

## Risks & Considerations

### Risco 1: Ordem de inicialização OpenGL
Os registradores estáticos atuais rodam **antes** de `main()`. O novo loader roda dentro de `MapDrawer::InitPostProcess()`, que só é chamado **depois** do contexto OpenGL estar pronto. Isso é correto — `PostProcessManager::Register` só guarda strings (não toca OpenGL). A compilação dos shaders acontece em `Initialize`, que é chamado na sequência. Sem regressão de ordem.

### Risco 2: Path resolution diferente em Windows vs Unix
`FileSystem::GetDataDirectory()` já abstrai isso. O loader deve usar `wxFileName` com `AppendDir` / `SetFullName` (mesmo padrão de `post_process_manager.cpp:39-50` para aux textures). Alternativa: `std::filesystem::path` — verificar se o projeto já usa em outros lugares antes de decidir.

### Risco 3: Nomes em `ShaderNames::` ficarem dessincronizados do manifesto
Se alguém editar só o manifesto e mudar `name="Nevasca"` para `name="Snow"`, o código em `map_display.cpp:395` (`screen_shader_name == ShaderNames::NEVASCA`) continua usando `"Nevasca"` e a detecção de "visual effects ativos" quebra silenciosamente. **Mitigação**: comentário explícito no manifesto e no header. Fora do escopo garantir consistência em runtime (seria overkill).

### Risco 4: Shaders embutidos hoje são source-of-truth, mas após migração o `data/shaders/*.frag` é
Se alguém reverter um `.cpp` que não existe mais, git vai resolver. Se alguém editar só o manifesto mas esquecer de commitar o `.frag`, boot quebra. Isso é risco normal de data-driven — igual aos XMLs de borders/grounds hoje.

### Risco 5: Encoding / line endings
Os arquivos `.frag` devem ser salvos em UTF-8 sem BOM. GLSL é tolerante a CRLF, mas preservar LF evita surpresas. `std::ifstream` em modo binário (sem `std::ios::binary` explícito o Windows converte CRLF → LF, o que é OK para GLSL).

### Risco 6: Hot reload (fora de escopo)
O design já permite: bastaria um método `PostProcessManager::Reload()` que limpa `effects` e rechamar loader + `Initialize`. Não implementar agora, mas não adicionar nada que impeça (ex: não fazer cache estático que bloqueie relance).

### Risco 7: Uniforms referenciados pelo código C++ precisam existir no shader
`map_drawer.cpp:237-...` provavelmente injeta uniforms por nome (`u_Time`, `u_Resolution`, `u_var0`, etc.) no shader atualmente selecionado. Se alguém publicar um shader sem esses uniforms, o `glUniform*` chamado pelo código simplesmente não faz nada (é no-op silencioso em OpenGL). Não é um problema — é o contrato esperado.
