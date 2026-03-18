# Plan: New Map Dialog

## Overview
Expandir o `MapPropertiesWindow` do Redux com as features do BT_MAPEDITORv3 (Map Name, Save Location, Size Presets, Auto External Files, Create From Selection) e integrar como dialogo obrigatorio antes de criar um novo mapa em `EditorManager::NewMap()`.

## User Request
Implementar o dialogo de criacao de novo mapa (New Map Dialog), copiando as features do BT_MAPEDITORv3. Quando File > New, deve abrir um dialogo configuravel antes de criar o mapa.

## Analysis
- **New Files**: No
- **Modified Files**: Yes
  - `source/ui/map/map_properties_window.h` - adicionar novos campos e metodos
  - `source/ui/map/map_properties_window.cpp` - implementar novos campos e logica
  - `source/editor/managers/editor_manager.cpp` - integrar dialogo em NewMap()
  - `source/app/settings.h` - adicionar Config keys para persistencia
  - `source/app/settings.cpp` - registrar defaults das novas keys
  - `source/ui/gui_ids.h` - adicionar IDs para novos controles
- **CMakeLists Update**: No (arquivos existentes)
- **Menu/Toolbar Wiring**: No (File > New ja existe)
- **Action System**: No
- **Data Files (XML/TOML)**: No

## Tasks

### Phase 1: Config Keys e GUI IDs

#### Task 1.1: Adicionar Config Keys para persistencia
- **Description**: Adicionar chaves de configuracao para Remember Save Location e Default Save Location
- **Files**: `source/app/settings.h`, `source/app/settings.cpp`
- **Details**:
  - Em `settings.h`, adicionar antes de `LAST` no enum `Config::Key`:
    ```cpp
    MAP_PROPERTIES_REMEMBER_SAVE_LOCATION,
    MAP_PROPERTIES_DEFAULT_SAVE_LOCATION,
    ```
  - Em `settings.cpp`, registrar os defaults (procurar onde outros configs sao registrados):
    ```cpp
    Int(MAP_PROPERTIES_REMEMBER_SAVE_LOCATION, 0);
    String(MAP_PROPERTIES_DEFAULT_SAVE_LOCATION, "");
    ```

#### Task 1.2: Adicionar GUI IDs para novos controles
- **Parallel**: Yes (pode rodar com Task 1.1)
- **Description**: Adicionar IDs para checkbox sync external files e botao browse
- **Files**: `source/ui/gui_ids.h`
- **Details**:
  - Adicionar apos `MAP_PROPERTIES_VERSION`:
    ```cpp
    MAP_PROPERTIES_SYNC_EXTERNAL_FILES,
    MAP_PROPERTIES_BROWSE_SAVE_LOCATION,
    ```

### Phase 2: Expandir MapPropertiesWindow

#### Task 2.1: Atualizar header do MapPropertiesWindow
- **Depends On**: Task 1.1, Task 1.2
- **Description**: Adicionar novos membros, metodos e parametro `allow_create_from_selection` ao construtor
- **Files**: `source/ui/map/map_properties_window.h`
- **Details**:
  - Modificar construtor para aceitar `MapTab*` como nullptr (novo mapa) e adicionar parametro `bool allow_create_from_selection = false`
  - Adicionar metodo publico: `bool ShouldCreateFromSelection() const;`
  - Adicionar novos event handlers:
    ```cpp
    void OnToggleSyncExternalFiles(wxCommandEvent&);
    void OnMapNameChanged(wxCommandEvent&);
    void OnBrowseSaveLocation(wxCommandEvent&);
    void OnSizePresetChanged(wxCommandEvent&);
    void OnDimensionsChanged(wxCommandEvent&);
    void OnDimensionsChangedSpin(wxSpinEvent&);
    ```
  - Adicionar metodos protected:
    ```cpp
    void UpdateExternalFilenameControls();
    void UpdateAutoExternalFilenames();
    void SyncSizePresetSelectionFromDimensions();
    ```
  - Adicionar novos membros:
    ```cpp
    wxTextCtrl* map_name_ctrl;
    wxTextCtrl* save_location_ctrl;
    wxChoice* size_preset_choice;
    wxCheckBox* sync_external_files_checkbox;
    wxCheckBox* create_from_selection_checkbox;
    wxCheckBox* remember_save_location_checkbox;
    std::string default_house_filename;
    std::string default_spawn_filename;
    bool updating_dimensions;
    ```
  - Remover `wxTextCtrl* waypoint_filename_ctrl;` (BT nao tem, simplificar)
    - **NOTA**: Manter o waypoint_filename_ctrl caso o Redux o use. Verificar se e necessario. Se for, manter.

#### Task 2.2: Implementar MapPropertiesWindow expandido
- **Depends On**: Task 2.1
- **Description**: Reescrever o construtor e adicionar novos metodos
- **Files**: `source/ui/map/map_properties_window.cpp`
- **Details**:
  - Adicionar funcoes helper no namespace anonimo (topo do arquivo):
    ```cpp
    namespace {
        const int kMapSizePresets[] = {128, 256, 512, 1024, 2048, 4096, 8192};
        constexpr int kMapSizePresetCount = sizeof(kMapSizePresets) / sizeof(kMapSizePresets[0]);

        int GetPresetSizeFromChoiceSelection(int selection) {
            if (selection <= 0 || selection > kMapSizePresetCount) return -1;
            return kMapSizePresets[selection - 1];
        }

        int GetChoiceSelectionFromDimensions(int width, int height) {
            if (width != height) return 0;
            for (int i = 0; i < kMapSizePresetCount; ++i) {
                if (width == kMapSizePresets[i]) return i + 1;
            }
            return 0;
        }

        std::string BuildMapBaseName(const std::string& map_name) {
            FileName map_file(wxstr(map_name));
            std::string base_name = nstr(map_file.GetName());
            if (base_name.empty()) base_name = "map";
            return base_name;
        }

        std::string BuildAutoHouseFilename(const std::string& map_name) {
            return BuildMapBaseName(map_name) + "-house.xml";
        }

        std::string BuildAutoSpawnFilename(const std::string& map_name) {
            return BuildMapBaseName(map_name) + "-spawn.xml";
        }

        std::string GetDefaultMapExtension() {
            return "otbm";
        }

        std::string BuildMapFilenameFromInput(const wxString& map_name_input, const std::string& fallback_extension) {
            wxString name = map_name_input;
            name.Trim(true);
            name.Trim(false);
            FileName map_name(name);
            std::string base_name = nstr(map_name.GetName());
            if (base_name.empty()) base_name = "map";
            std::string ext = nstr(map_name.GetExt());
            if (ext.empty()) ext = fallback_extension;
            return base_name + "." + ext;
        }
    }
    ```
  - **Construtor**: Reconstruir seguindo a ordem do BT:
    1. Map Name (novo)
    2. Save Location + Browse button (novo)
    3. Remember Save Location checkbox (novo)
    4. Map Description (existente)
    5. Map Version dropdown (existente)
    6. Client Version dropdown (existente)
    7. Map Size Preset dropdown (novo)
    8. Map Dimensions width/height (existente)
    9. Create From Selection checkbox (novo, condicional)
    10. Auto External Files checkbox (novo)
    11. External Housefile (existente)
    12. External Spawnfile (existente)
    13. External Waypointfile (manter do Redux original)
    14. OK/Cancel buttons com icones (existente, manter estilo Redux com IMAGE_MANAGER)
  - Inicializar `map_name_ctrl`, `save_location_ctrl`, etc como `nullptr` no construtor
  - Calcular `initial_map_name` do map name ou filename
  - Calcular `initial_save_location` do arquivo ou config persistido
  - Inicializar `default_house_filename` e `default_spawn_filename`
  - Bind events com `Bind()` (preferido no Redux, nao usar event table):
    ```cpp
    size_preset_choice->Bind(wxEVT_CHOICE, &MapPropertiesWindow::OnSizePresetChanged, this);
    map_name_ctrl->Bind(wxEVT_TEXT, &MapPropertiesWindow::OnMapNameChanged, this);
    width_spin->Bind(wxEVT_TEXT, &MapPropertiesWindow::OnDimensionsChanged, this);
    height_spin->Bind(wxEVT_TEXT, &MapPropertiesWindow::OnDimensionsChanged, this);
    width_spin->Bind(wxEVT_SPINCTRL, &MapPropertiesWindow::OnDimensionsChangedSpin, this);
    height_spin->Bind(wxEVT_SPINCTRL, &MapPropertiesWindow::OnDimensionsChangedSpin, this);
    sync_external_files_checkbox->Bind(wxEVT_CHECKBOX, &MapPropertiesWindow::OnToggleSyncExternalFiles, this);
    ```
  - Chamar `SyncSizePresetSelectionFromDimensions()` e `UpdateExternalFilenameControls()` no final do construtor
  - **Implementar novos metodos** (copiar logica do BT, converter para API Redux):
    - `OnToggleSyncExternalFiles` -> chama `UpdateExternalFilenameControls()`
    - `OnMapNameChanged` -> chama `UpdateAutoExternalFilenames()` + `UpdateExternalFilenameControls()`
    - `OnBrowseSaveLocation` -> wxDirDialog
    - `OnSizePresetChanged` -> atualiza width/height spins
    - `OnDimensionsChanged` / `OnDimensionsChangedSpin` -> sync preset selection
    - `SyncSizePresetSelectionFromDimensions` -> encontra preset correspondente
    - `UpdateExternalFilenameControls` -> enable/disable house/spawn filenames baseado no checkbox
    - `UpdateAutoExternalFilenames` -> recalcula defaults baseado no map name
    - `ShouldCreateFromSelection` -> retorna valor do checkbox
  - **Atualizar OnClickOK**:
    - Salvar map name via `map.setName(filename)`
    - Salvar save location: se preenchido, montar path completo com `map.setFilename(full_path)`
    - Persistir remember_save_location e default_save_location em g_settings
    - Chamar `g_settings.save()`
    - Manter logica existente de version change (usar `MapVersionChanger::changeMapVersion` do Redux)
    - EndModal com `wxID_OK` em vez de `1` para consistencia com o fluxo NewMap

  - **API differences Redux vs BT** (converter):
    - BT: `editor.getMap()` -> Redux: `editor.map` (acesso direto)
    - BT: `ClientVersion::get(nstr(...))->getID()` -> Redux: `ClientVersion::get(nstr(...))->getProtocolID()` (verificar)
    - BT: `g_gui.GetCurrentVersion()` -> Redux: `g_version.GetCurrentVersion()`
    - BT: `g_gui.PopupDialog()` -> Redux: `DialogUtil::PopupDialog()`
    - BT: `g_gui.GetOpenMapCount()` -> Redux: `g_editors.GetOpenMapCount()`
    - BT: usa raw `delete` -> Redux: usa `unique_ptr`
    - Manter `IMAGE_MANAGER.GetBitmapBundle()` nos botoes OK/Cancel (estilo Redux)

### Phase 3: Integrar no EditorManager::NewMap()

#### Task 3.1: Modificar EditorManager::NewMap() para abrir dialogo
- **Depends On**: Task 2.2
- **Description**: Antes de criar o editor, abrir MapPropertiesWindow. Se cancelar, nao criar mapa.
- **Files**: `source/editor/managers/editor_manager.cpp`
- **Details**:
  - Adicionar include: `#include "ui/map/map_properties_window.h"`
  - Adicionar include: `#include "editor/copybuffer.h"` (se nao existir, para CopyBuffer)
  - Novo fluxo de `NewMap()`:
    1. Capturar selecao do editor atual (se existir) antes de fechar:
       ```cpp
       Editor* source_editor = GetCurrentEditor();
       std::unique_ptr<CopyBuffer> selection_snapshot;
       bool has_selection_snapshot = false;
       if (source_editor && !source_editor->selection.empty()) {
           selection_snapshot = std::make_unique<CopyBuffer>();
           selection_snapshot->copy(*source_editor, g_gui.GetCurrentFloor(), true);
           has_selection_snapshot = selection_snapshot->canPaste();
           if (!has_selection_snapshot) {
               selection_snapshot.reset();
           }
       }
       ```
    2. Criar editor vazio (como ja faz):
       ```cpp
       auto editor = EditorFactory::CreateEmpty(g_gui.copybuffer);
       ```
    3. Abrir `MapPropertiesWindow` com `view = nullptr` e `allow_create_from_selection = has_selection_snapshot`:
       ```cpp
       MapPropertiesWindow properties(g_gui.root, nullptr, *editor, has_selection_snapshot);
       int result = properties.ShowModal();
       bool create_from_selection = properties.ShouldCreateFromSelection();
       ```
    4. Se resultado != wxID_OK, descartar editor e retornar false
    5. Se OK, criar MapTab e opcionalmente colar selecao:
       ```cpp
       auto* mapTab = newd MapTab(g_gui.tabbook, editor.release());
       mapTab->OnSwitchEditorMode(SELECTION_MODE);
       if (create_from_selection && selection_snapshot && selection_snapshot->canPaste()) {
           selection_snapshot->paste(*mapTab->GetEditor(), Position(0, 0, 7));
       } else {
           mapTab->GetMap()->clearChanges();
       }
       ```
  - **IMPORTANTE**: Verificar se `CopyBuffer::copy` no Redux aceita parametro `bool selection_only` como terceiro argumento. Se nao, adaptar. Verificar a assinatura em `source/editor/copybuffer.h`.

### Phase 4: Verificacao e Ajustes

#### Task 4.1: Verificar assinaturas de API
- **Depends On**: Task 3.1
- **Description**: Antes de finalizar, verificar que todas as APIs usadas existem no Redux
- **Files**: Leitura apenas
- **Details**:
  - Verificar `CopyBuffer::copy()` - assinatura exata (aceita `selection_only`?)
  - Verificar `CopyBuffer::canPaste()` existe
  - Verificar `Map::setFilename()` existe
  - Verificar `Map::hasFile()` existe
  - Verificar `Map::clearChanges()` existe (ou e `clearChanged()`)
  - Verificar `selection.empty()` vs `hasSelection()`
  - Verificar `g_gui.GetCurrentFloor()` existe
  - Se alguma API nao existir, adaptar a implementacao

#### Task 4.2: Garantir waypoint file e mantido
- **Parallel**: Yes (pode rodar com 4.1)
- **Description**: O Redux tem waypoint_filename_ctrl que o BT nao tem. Manter no dialogo.
- **Files**: `source/ui/map/map_properties_window.cpp`
- **Details**:
  - Manter o campo External Waypointfile no dialogo (apos External Spawnfile)
  - Manter a logica de salvar waypoint filename no OnClickOK

## Execution Order
1. Phase 1: Task 1.1 + Task 1.2 (paralelo)
2. Phase 2: Task 2.1 (apos Phase 1), entao Task 2.2 (apos 2.1)
3. Phase 3: Task 3.1 (apos Phase 2)
4. Phase 4: Task 4.1 + Task 4.2 (paralelo, apos Phase 3)

## Testing Notes
- Compilar e abrir o editor
- File > New deve abrir o dialogo MapPropertiesWindow
- Verificar que todos os campos aparecem: Map Name, Save Location, Remember, Description, Map/Client Version, Size Preset, Dimensions, Auto External Files, House/Spawn/Waypoint files
- Alterar Size Preset deve atualizar width/height
- Alterar width/height deve sincronizar o preset (ou mostrar "Custom")
- Alterar Map Name deve atualizar house/spawn filenames quando Auto External Files esta ativo
- Browse deve abrir wxDirDialog para escolher diretorio
- Clicar Cancel deve nao criar mapa
- Clicar OK deve criar mapa com as configuracoes escolhidas
- Se havia selecao ativa e "Create From Selection" marcado, a selecao deve ser colada em (0,0,7)
- Remember Save Location deve persistir entre sessoes

## Risks & Considerations
1. **CopyBuffer API**: A assinatura de `CopyBuffer::copy()` pode diferir entre BT e Redux. Verificar se aceita `selection_only` como terceiro parametro. Se nao, pode ser necessario adaptar ou simplesmente nao implementar "Create From Selection" inicialmente.
2. **MapVersionChanger vs BT version change**: O Redux usa `MapVersionChanger::changeMapVersion()` que encapsula toda a logica de conversao. Manter isso em vez de copiar a logica complexa do BT.
3. **EndModal return value**: O Redux atual usa `EndModal(1)`, mas para integrar com NewMap() precisamos usar `EndModal(wxID_OK)` para que `ShowModal() == wxID_OK` funcione. Isso e uma mudanca de comportamento que pode afetar quem chama o dialogo via File > Map Properties (editar mapa existente). Verificar que o caller existente (`file_menu_handler.cpp`) nao depende do valor `1`.
4. **Editor lifecycle**: No BT, o editor e criado com `new` e deletado manualmente se cancelar. No Redux, `EditorFactory::CreateEmpty` retorna `unique_ptr`, entao o cleanup e automatico se o usuario cancelar.
5. **Waypoint file**: Manter o campo waypoint do Redux que o BT nao tem, para nao perder funcionalidade.
