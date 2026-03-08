# Camera Path - pacote extraido (BT_MAPEDITORv3)

## Objetivo
Extrair o sistema Camera Path (UI + logica + integracoes) do `BT_MAPEDITORv3` para servir de base de porting no redux.

## Criterio de selecao
Foram incluidos arquivos `.cpp/.h` que contem referencias a:
- `camera_path` / `CameraPath`
- `CAMERA_PATH` (IDs/menu/timer)
- `TILESET_CAMERA_PATH`
- `SHOW_CAMERA_PATHS`
- `BRUSH_TYPE_CAMERA_PATH`

## Arquivos extraidos (33)

### Nucleo (dados/logica)
- `source/camera_path.h`
- `source/camera_path.cpp`
- `source/map.h`

### Undo/redo e aplicacao no editor
- `source/action.h`
- `source/action.cpp`
- `source/editor.h`
- `source/editor.cpp`

### Brush e tipagem
- `source/camera_path_brush.h`
- `source/camera_path_brush.cpp`
- `source/brush.h`
- `source/brush.cpp`
- `source/brush_enums.h`
- `source/brush_manager_panel.cpp`

### UI de paleta
- `source/palette_camera_paths.h`
- `source/palette_camera_paths.cpp`
- `source/palette_window.h`
- `source/palette_window.cpp`
- `source/gui_ids.h`
- `source/palette_common.cpp`
- `source/palette_brushlist.cpp`
- `source/recent_brushes_window.cpp`

### Playback/render
- `source/map_display.h`
- `source/map_display.cpp`
- `source/map_drawer.h`
- `source/map_drawer.cpp`

### Integracao de app/menu/config
- `source/gui.h`
- `source/gui.cpp`
- `source/main_menubar.h`
- `source/main_menubar.cpp`
- `source/settings.h`
- `source/settings.cpp`
- `source/tileset.h`
- `source/tileset.cpp`

## Cadeia de dependencias (alto nivel)
1. `camera_path.*` define paths/keyframes, snapshot e sample no tempo.
2. `palette_camera_paths.*` edita dados na UI e aplica via `Editor::ApplyCameraPathsSnapshot(...)`.
3. `action.*` salva snapshots em `CHANGE_CAMERA_PATHS` para undo/redo.
4. `map_display.*` toca animacao da camera com timer e `SampleCameraPathByTime(...)`.
5. `map_drawer.*` desenha paths quando `SHOW_CAMERA_PATHS` esta ativo.
6. `gui/main_menubar/settings/tileset/...` conectam menu, IDs e toggle visual.

## Observacao para o redux
O redux usa estrutura de pastas diferente (`source/app`, `source/ui`, `source/map`, etc.).
Este pacote foi copiado sem adaptacao estrutural, como baseline de migracao.

## Origem e destinos
- Origem: `B:\Github\BT_MAPEDITORv3`
- Pacote extraido: `B:\Github\BT_MAPEDITORv3\temp\camera_path_migration`
- Copia no redux: `B:\Github\remeres-map-editor-redux-GLASCZ\temp\camera_path_migration`
