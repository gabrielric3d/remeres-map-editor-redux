//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/toolbar/standard_toolbar.h"
#include "ui/gui.h"
#include "ui/gui_ids.h"
#include "ui/main_menubar.h"
#include "editor/editor.h"
#include "editor/action_queue.h"
#include "map/map.h"
#include "util/image_manager.h"
#include <wx/artprov.h>
#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

namespace {

wxString QuoteShellArgument(const wxString& value)
{
	wxString escaped = value;
	escaped.Replace("\"", "\"\"");
	return "\"" + escaped + "\"";
}

bool ExecuteCommandSync(const wxString& command, wxArrayString& output, wxArrayString& errors, long& exitCode, const wxString& workingDirectory = wxEmptyString)
{
	output.clear();
	errors.clear();
	wxString normalized_working_directory = workingDirectory;
	normalized_working_directory.Trim(true);
	normalized_working_directory.Trim(false);

	wxExecuteEnv env;
	wxExecuteEnv* env_ptr = nullptr;
	if (!normalized_working_directory.empty()) {
		env.cwd = normalized_working_directory;
		env_ptr = &env;
	}
	exitCode = wxExecute(command, output, errors, wxEXEC_SYNC | wxEXEC_NODISABLE, env_ptr);
#ifdef __WINDOWS__
	if (exitCode != 0 && !normalized_working_directory.empty()) {
		output.clear();
		errors.clear();
		const wxString fallback = "cmd.exe /C cd /D " + QuoteShellArgument(normalized_working_directory) + " && " + command;
		exitCode = wxExecute(fallback, output, errors, wxEXEC_SYNC | wxEXEC_NODISABLE);
	}
#endif
	return exitCode == 0;
}

wxString BuildCommandOutput(const wxArrayString& output, const wxArrayString& errors)
{
	wxString message;
	int lines_added = 0;
	const int max_lines = 12;

	auto append_lines = [&message, &lines_added, max_lines](const wxArrayString& lines) {
		for (size_t i = 0; i < lines.size() && lines_added < max_lines; ++i) {
			wxString line = lines[i];
			line.Trim(true);
			line.Trim(false);
			if (line.empty()) {
				continue;
			}
			if (!message.empty()) {
				message << "\n";
			}
			message << line;
			++lines_added;
		}
	};

	append_lines(errors);
	append_lines(output);

	if (message.empty()) {
		message = "No command output was returned.";
	}
	return message;
}

bool IsNothingToCommitMessage(const wxString& commandOutput)
{
	wxString lower = commandOutput.Lower();
	return lower.Find("nothing to commit") != wxNOT_FOUND ||
		lower.Find("working tree clean") != wxNOT_FOUND ||
		lower.Find("no changes added to commit") != wxNOT_FOUND;
}

bool PathEquals(const wxString& left, const wxString& right)
{
#ifdef __WINDOWS__
	return left.CmpNoCase(right) == 0;
#else
	return left == right;
#endif
}

void AddUniquePath(wxArrayString& paths, const wxString& value)
{
	if (value.empty()) {
		return;
	}
	for (size_t i = 0; i < paths.size(); ++i) {
		if (PathEquals(paths[i], value)) {
			return;
		}
	}
	paths.Add(value);
}

void AddUniqueNormalizedPath(wxArrayString& paths, const wxString& value, bool treatAsDirectory = false)
{
	wxString path = value;
	path.Trim(true);
	path.Trim(false);
	if (path.empty()) {
		return;
	}

	wxFileName file_name;
	if (treatAsDirectory || wxFileName::DirExists(path)) {
		file_name.AssignDir(path);
	} else {
		file_name.Assign(path);
	}
	file_name.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_CASE);
	AddUniquePath(paths, file_name.GetFullPath());
}

bool HasGitMarker(const wxString& directoryPath)
{
	const wxString gitPath = directoryPath + wxFILE_SEP_PATH + ".git";
	return wxFileName::DirExists(gitPath) || wxFileName::FileExists(gitPath);
}

void AddUniqueExistingDirectory(wxArrayString& paths, const wxString& value)
{
	wxString path = value;
	path.Trim(true);
	path.Trim(false);
	if (path.empty()) {
		return;
	}
	if (wxFileName::FileExists(path)) {
		wxFileName file_name(path);
		path = file_name.GetPath();
	}
	if (!wxFileName::DirExists(path)) {
		return;
	}
	AddUniqueNormalizedPath(paths, path, true);
}

bool TryResolveGitRepoRoot(const wxString& searchPath, wxString& repoRoot)
{
	wxArrayString output;
	wxArrayString errors;
	long exitCode = -1;

	wxString normalized_search_path = searchPath;
	normalized_search_path.Trim(true);
	normalized_search_path.Trim(false);
	if (normalized_search_path.empty()) {
		return false;
	}

	wxFileName search_path_name;
	if (wxFileName::FileExists(normalized_search_path)) {
		search_path_name.Assign(normalized_search_path);
		normalized_search_path = search_path_name.GetPath();
	} else {
		search_path_name.AssignDir(normalized_search_path);
	}
	search_path_name.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_CASE);
	normalized_search_path = search_path_name.GetPath(wxPATH_GET_VOLUME);
	normalized_search_path.Trim(true);
	normalized_search_path.Trim(false);
	if (normalized_search_path.empty() || !wxFileName::DirExists(normalized_search_path)) {
		return false;
	}

	const wxString command = "git rev-parse --show-toplevel";
	if (ExecuteCommandSync(command, output, errors, exitCode, normalized_search_path) && !output.empty()) {
		repoRoot = output.front();
		repoRoot.Trim(true);
		repoRoot.Trim(false);
		return !repoRoot.empty();
	}

	wxString current_dir = normalized_search_path;
	wxString previous_dir;
	while (!current_dir.empty() && !PathEquals(current_dir, previous_dir)) {
		if (HasGitMarker(current_dir)) {
			repoRoot = current_dir;
			return true;
		}

		wxFileName parent_dir;
		parent_dir.AssignDir(current_dir);
		if (parent_dir.GetDirCount() == 0) {
			break;
		}
		parent_dir.RemoveLastDir();

		previous_dir = current_dir;
		current_dir = parent_dir.GetPath(wxPATH_GET_VOLUME);
		current_dir.Trim(true);
		current_dir.Trim(false);
	}
	return false;
}

bool ConvertToRepoRelativePath(const wxString& absolutePath, const wxString& repoRoot, wxString& relativePath)
{
	wxString abs = absolutePath;
	abs.Trim(true);
	abs.Trim(false);
	if (abs.empty() || repoRoot.empty()) {
		return false;
	}

	wxFileName file_name;
	if (wxFileName::DirExists(abs)) {
		file_name.AssignDir(abs);
	} else {
		file_name.Assign(abs);
	}
	file_name.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_CASE);

	wxFileName repo_dir;
	repo_dir.AssignDir(repoRoot);
	repo_dir.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_CASE);
	const wxString normalized_repo = repo_dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);

	if (!file_name.MakeRelativeTo(normalized_repo)) {
		return false;
	}

	relativePath = file_name.GetFullPath();
	relativePath.Trim(true);
	relativePath.Trim(false);

	if (relativePath.empty() || relativePath == "." || relativePath == "..") {
		return false;
	}

	if (relativePath.StartsWith("../") || relativePath.StartsWith("..\\") ||
		relativePath.StartsWith(".." + wxString(wxFILE_SEP_PATH))) {
		return false;
	}

	return true;
}

wxString BuildGitPathspecArgs(const wxArrayString& paths)
{
	wxString args;
	for (size_t i = 0; i < paths.size(); ++i) {
		wxString path = paths[i];
		path.Replace("\\", "/");
		args += " " + QuoteShellArgument(path);
	}
	return args;
}

void CollectGitReposRecursively(const wxString& rootPath, int remainingDepth, wxArrayString& outRepos)
{
	if (remainingDepth < 0 || !wxFileName::DirExists(rootPath)) {
		return;
	}

	if (HasGitMarker(rootPath)) {
		AddUniquePath(outRepos, rootPath);
		return;
	}

	if (remainingDepth == 0) {
		return;
	}

	wxDir dir(rootPath);
	if (!dir.IsOpened()) {
		return;
	}

	wxString childName;
	bool hasMore = dir.GetFirst(&childName, wxEmptyString, wxDIR_DIRS);
	while (hasMore) {
		if (childName != "." && childName != "..") {
			const wxString childPath = rootPath + wxFILE_SEP_PATH + childName;
			CollectGitReposRecursively(childPath, remainingDepth - 1, outRepos);
		}
		hasMore = dir.GetNext(&childName);
	}
}

wxString BuildPathsList(const wxArrayString& paths, size_t maxItems)
{
	wxString message;
	const size_t count = std::min(maxItems, paths.size());
	for (size_t i = 0; i < count; ++i) {
		message += "\n- " + paths[i];
	}
	if (paths.size() > count) {
		message += "\n- ...";
	}
	return message;
}

} // anonymous namespace

const wxString StandardToolBar::PANE_NAME = "standard_toolbar";

StandardToolBar::StandardToolBar(wxWindow* parent) {
	wxSize icon_size = FROM_DIP(parent, wxSize(16, 16));
	wxBitmap new_bitmap = IMAGE_MANAGER.GetBitmap(ICON_NEW, icon_size);
	wxBitmap open_bitmap = IMAGE_MANAGER.GetBitmap(ICON_OPEN, icon_size);
	wxBitmap save_bitmap = IMAGE_MANAGER.GetBitmap(ICON_SAVE, icon_size);
	wxBitmap saveas_bitmap = IMAGE_MANAGER.GetBitmap(ICON_SAVE, icon_size);
	wxBitmap undo_bitmap = IMAGE_MANAGER.GetBitmap(ICON_UNDO, icon_size);
	wxBitmap redo_bitmap = IMAGE_MANAGER.GetBitmap(ICON_REDO, icon_size);
	wxBitmap cut_bitmap = IMAGE_MANAGER.GetBitmap(ICON_CUT, icon_size);
	wxBitmap copy_bitmap = IMAGE_MANAGER.GetBitmap(ICON_COPY, icon_size);
	wxBitmap paste_bitmap = IMAGE_MANAGER.GetBitmap(ICON_PASTE, icon_size);
	wxBitmap find_bitmap = IMAGE_MANAGER.GetBitmap(ICON_FIND, icon_size);

	toolbar = newd wxAuiToolBar(parent, TOOLBAR_STANDARD, wxDefaultPosition, wxDefaultSize, wxAUI_TB_DEFAULT_STYLE);
	toolbar->SetToolBitmapSize(icon_size);
	toolbar->AddTool(wxID_NEW, wxEmptyString, new_bitmap, wxNullBitmap, wxITEM_NORMAL, "New Map (Ctrl+N) - Create a new empty map", "Create a new empty map", nullptr);
	toolbar->AddTool(wxID_OPEN, wxEmptyString, open_bitmap, wxNullBitmap, wxITEM_NORMAL, "Open Map (Ctrl+O) - Open an existing map", "Open an existing map", nullptr);
	toolbar->AddTool(wxID_SAVE, wxEmptyString, save_bitmap, wxNullBitmap, wxITEM_NORMAL, "Save Map (Ctrl+S)", "Save the current map", nullptr);
	toolbar->AddTool(wxID_SAVEAS, wxEmptyString, saveas_bitmap, wxNullBitmap, wxITEM_NORMAL, "Save Map As... (Ctrl+Alt+S)", "Save the current map with a new name", nullptr);
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_UNDO, wxEmptyString, undo_bitmap, wxNullBitmap, wxITEM_NORMAL, "Undo (Ctrl+Z)", "Undo the last action", nullptr);
	toolbar->AddTool(wxID_REDO, wxEmptyString, redo_bitmap, wxNullBitmap, wxITEM_NORMAL, "Redo (Ctrl+Shift+Z)", "Redo the last undone action", nullptr);
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_CUT, wxEmptyString, cut_bitmap, wxNullBitmap, wxITEM_NORMAL, "Cut (Ctrl+X)", "Cut selection to clipboard", nullptr);
	toolbar->AddTool(wxID_COPY, wxEmptyString, copy_bitmap, wxNullBitmap, wxITEM_NORMAL, "Copy (Ctrl+C)", "Copy selection to clipboard", nullptr);
	toolbar->AddTool(wxID_PASTE, wxEmptyString, paste_bitmap, wxNullBitmap, wxITEM_NORMAL, "Paste (Ctrl+V)", "Paste from clipboard", nullptr);
	toolbar->AddSeparator();
	toolbar->AddTool(MAIN_FRAME_MENU + MenuBar::JUMP_TO_BRUSH, wxEmptyString, find_bitmap, wxNullBitmap, wxITEM_NORMAL, "Jump to Brush (J)", "Find brush or item", nullptr);

	toolbar->Realize();

	toolbar->Bind(wxEVT_COMMAND_MENU_SELECTED, &StandardToolBar::OnButtonClick, this);
}

StandardToolBar::~StandardToolBar() {
	toolbar->Unbind(wxEVT_COMMAND_MENU_SELECTED, &StandardToolBar::OnButtonClick, this);
}

void StandardToolBar::Update() {
	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		bool canUndo = editor->actionQueue->canUndo();
		toolbar->EnableTool(wxID_UNDO, canUndo);
		toolbar->SetToolShortHelp(wxID_UNDO, canUndo ? "Undo (Ctrl+Z)" : "Undo (Ctrl+Z) - Nothing to undo");

		bool canRedo = editor->actionQueue->canRedo();
		toolbar->EnableTool(wxID_REDO, canRedo);
		toolbar->SetToolShortHelp(wxID_REDO, canRedo ? "Redo (Ctrl+Shift+Z)" : "Redo (Ctrl+Shift+Z) - Nothing to redo");

		bool canPaste = editor->copybuffer.canPaste();
		toolbar->EnableTool(wxID_PASTE, canPaste);
		toolbar->SetToolShortHelp(wxID_PASTE, canPaste ? "Paste (Ctrl+V)" : "Paste (Ctrl+V) - Clipboard empty");
	} else {
		toolbar->EnableTool(wxID_UNDO, false);
		toolbar->SetToolShortHelp(wxID_UNDO, "Undo (Ctrl+Z) - No editor open");
		toolbar->EnableTool(wxID_REDO, false);
		toolbar->SetToolShortHelp(wxID_REDO, "Redo (Ctrl+Shift+Z) - No editor open");
		toolbar->EnableTool(wxID_PASTE, false);
		toolbar->SetToolShortHelp(wxID_PASTE, "Paste (Ctrl+V) - No editor open");
	}

	bool has_map = editor != nullptr;
	bool is_host = has_map && !editor->live_manager.IsClient();

	toolbar->EnableTool(wxID_SAVE, is_host);
	toolbar->SetToolShortHelp(wxID_SAVE, is_host ? "Save Map (Ctrl+S)" : (has_map ? "Save Map (Ctrl+S) - Client cannot save" : "Save Map (Ctrl+S) - No map open"));

	toolbar->EnableTool(wxID_SAVEAS, is_host);
	toolbar->SetToolShortHelp(wxID_SAVEAS, is_host ? "Save Map As... (Ctrl+Alt+S)" : (has_map ? "Save Map As... (Ctrl+Alt+S) - Client cannot save" : "Save Map As... (Ctrl+Alt+S) - No map open"));

	toolbar->EnableTool(wxID_CUT, has_map);
	toolbar->SetToolShortHelp(wxID_CUT, has_map ? "Cut (Ctrl+X)" : "Cut (Ctrl+X) - No map open");

	toolbar->EnableTool(wxID_COPY, has_map);
	toolbar->SetToolShortHelp(wxID_COPY, has_map ? "Copy (Ctrl+C)" : "Copy (Ctrl+C) - No map open");

	toolbar->Refresh();
}

void StandardToolBar::OnButtonClick(wxCommandEvent& event) {
	switch (event.GetId()) {
		case wxID_NEW:
			g_gui.NewMap();
			break;
		case wxID_OPEN:
			g_gui.OpenMap();
			break;
		case wxID_SAVE:
			g_gui.SaveMap();
			break;
		case wxID_SAVEAS:
			g_gui.SaveMapAs();
			break;
		case wxID_UNDO:
			g_gui.DoUndo();
			break;
		case wxID_REDO:
			g_gui.DoRedo();
			break;
		case wxID_CUT:
			g_gui.DoCut();
			break;
		case wxID_COPY:
			g_gui.DoCopy();
			break;
		case wxID_PASTE:
			g_gui.PreparePaste();
			break;
		default:
			event.Skip();
			break;
	}
}

void StandardToolBar::DeployMap() {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || editor->live_manager.IsClient()) {
		return;
	}

	Map& map = editor->map;
	if (!map.hasFile()) {
		wxMessageBox(
			"Nao foi possivel fazer deploy porque o mapa ainda nao foi salvo.",
			"Deploy do Mapa",
			wxOK | wxICON_WARNING,
			g_gui.root);
		return;
	}

	if (map.hasChanged()) {
		const int answer = wxMessageBox(
			"O mapa possui alteracoes nao salvas.\nDeseja salvar antes do deploy?",
			"Deploy do Mapa",
			wxYES_NO | wxCANCEL | wxICON_QUESTION,
			g_gui.root);
		if (answer == wxCANCEL) {
			return;
		}
		if (answer == wxYES) {
			g_gui.SaveMap();
			if (map.hasChanged() || !map.hasFile()) {
				wxMessageBox(
					"Nao foi possivel fazer deploy porque o mapa nao foi salvo.",
					"Deploy do Mapa",
					wxOK | wxICON_WARNING,
					g_gui.root);
				return;
			}
		}
	}

	wxArrayString output;
	wxArrayString errors;
	long exit_code = -1;

	if (!ExecuteCommandSync("git --version", output, errors, exit_code)) {
		wxMessageBox(
			"Nao foi possivel fazer deploy porque o Git nao esta disponivel no sistema.",
			"Deploy do Mapa",
			wxOK | wxICON_ERROR,
			g_gui.root);
		return;
	}

	wxArrayString candidate_paths;
	AddUniqueExistingDirectory(candidate_paths, wxstr(map.getFilename()));

	wxFileName filename_path(wxstr(map.getFilename()));
	AddUniqueExistingDirectory(candidate_paths, filename_path.GetPath());

	if (candidate_paths.empty()) {
		wxMessageBox(
			"Nao foi possivel fazer deploy.\nNao foi possivel determinar o caminho do mapa.",
			"Deploy do Mapa",
			wxOK | wxICON_WARNING,
			g_gui.root);
		return;
	}

	wxString repo_root;
	for (size_t i = 0; i < candidate_paths.size(); ++i) {
		if (TryResolveGitRepoRoot(candidate_paths[i], repo_root)) {
			break;
		}
	}

	if (repo_root.empty()) {
		wxArrayString nested_roots;
		for (size_t i = 0; i < candidate_paths.size(); ++i) {
			const wxString& base = candidate_paths[i];
			AddUniqueExistingDirectory(nested_roots, base);
			AddUniqueExistingDirectory(nested_roots, base + wxFILE_SEP_PATH + "data" + wxFILE_SEP_PATH + "scripts" + wxFILE_SEP_PATH + "maps");
			AddUniqueExistingDirectory(nested_roots, base + wxFILE_SEP_PATH + "data" + wxFILE_SEP_PATH + "world");
			AddUniqueExistingDirectory(nested_roots, base + wxFILE_SEP_PATH + "maps");
		}

		wxArrayString nested_repo_paths;
		for (size_t i = 0; i < nested_roots.size(); ++i) {
			CollectGitReposRecursively(nested_roots[i], 3, nested_repo_paths);
		}

		wxArrayString nested_repo_roots;
		for (size_t i = 0; i < nested_repo_paths.size(); ++i) {
			wxString resolved_root;
			if (TryResolveGitRepoRoot(nested_repo_paths[i], resolved_root)) {
				AddUniquePath(nested_repo_roots, resolved_root);
			}
		}

		if (nested_repo_roots.size() == 1) {
			repo_root = nested_repo_roots[0];
		} else if (nested_repo_roots.size() > 1) {
			wxMessageBox(
				"Nao foi possivel fazer deploy automaticamente.\nForam encontrados varios repositorios Git candidatos:"
				+ BuildPathsList(nested_repo_roots, 6),
				"Deploy do Mapa",
				wxOK | wxICON_WARNING,
				g_gui.root);
			return;
		}
	}

	if (repo_root.empty()) {
		wxMessageBox(
			"Nao foi possivel fazer deploy.\nNenhum repositorio Git foi encontrado para este mapa.\n\nCaminhos analisados:"
			+ BuildPathsList(candidate_paths, 6),
			"Deploy do Mapa",
			wxOK | wxICON_WARNING,
			g_gui.root);
		return;
	}

	wxArrayString deploy_targets_abs;

	const wxString map_filename = wxstr(map.getFilename());
	AddUniqueNormalizedPath(deploy_targets_abs, map_filename, false);

	wxString auxiliary_base_dir;
	if (wxFileName::DirExists(map_filename)) {
		auxiliary_base_dir = map_filename;
	} else {
		wxFileName map_file(map_filename);
		auxiliary_base_dir = map_file.GetPath();
	}

	auto add_auxiliary_target = [&](const std::string& aux_name) {
		if (aux_name.empty()) {
			return;
		}
		wxFileName aux_file(wxstr(aux_name));
		if (aux_file.IsRelative() && !auxiliary_base_dir.empty()) {
			aux_file.MakeAbsolute(auxiliary_base_dir);
		}
		AddUniqueNormalizedPath(deploy_targets_abs, aux_file.GetFullPath(), false);
	};

	add_auxiliary_target(map.getHouseFilename());
	add_auxiliary_target(map.getSpawnFilename());

	wxArrayString deploy_targets_rel;
	for (size_t i = 0; i < deploy_targets_abs.size(); ++i) {
		wxString relative_target;
		if (ConvertToRepoRelativePath(deploy_targets_abs[i], repo_root, relative_target)) {
			AddUniquePath(deploy_targets_rel, relative_target);
		}
	}

	if (deploy_targets_rel.empty()) {
		wxMessageBox(
			"Nao foi possivel fazer deploy.\nOs arquivos do mapa nao pertencem ao repositorio Git encontrado.\n\nArquivos alvo:"
			+ BuildPathsList(deploy_targets_abs, 6),
			"Deploy do Mapa",
			wxOK | wxICON_WARNING,
			g_gui.root);
		return;
	}

	const wxString pathspec_args = BuildGitPathspecArgs(deploy_targets_rel);

	wxString command = "git remote";
	if (!ExecuteCommandSync(command, output, errors, exit_code, repo_root)) {
		wxMessageBox(
			"Nao foi possivel validar os remotos do repositorio.\n\n" + BuildCommandOutput(output, errors),
			"Deploy do Mapa",
			wxOK | wxICON_ERROR,
			g_gui.root);
		return;
	}

	bool has_remote = false;
	for (size_t i = 0; i < output.size(); ++i) {
		wxString line = output[i];
		line.Trim(true);
		line.Trim(false);
		if (!line.empty()) {
			has_remote = true;
			break;
		}
	}
	if (!has_remote) {
		wxMessageBox(
			"Nao foi possivel fazer deploy.\nEste repositorio nao possui remoto configurado.",
			"Deploy do Mapa",
			wxOK | wxICON_WARNING,
			g_gui.root);
		return;
	}

	const wxString default_commit_message = "Deploy map update - " + wxDateTime::Now().FormatISOCombined(' ');
	wxTextEntryDialog commit_dialog(
		g_gui.root,
		"Escreva a mensagem do commit:",
		"Mensagem do Commit",
		default_commit_message,
		wxOK | wxCANCEL);

	if (commit_dialog.ShowModal() != wxID_OK) {
		return;
	}

	wxString commit_message = commit_dialog.GetValue();
	commit_message.Trim(true);
	commit_message.Trim(false);
	if (commit_message.empty()) {
		wxMessageBox(
			"A mensagem do commit nao pode ser vazia.\nDeploy cancelado.",
			"Deploy do Mapa",
			wxOK | wxICON_WARNING,
			g_gui.root);
		return;
	}

	command = "git add --" + pathspec_args;
	if (!ExecuteCommandSync(command, output, errors, exit_code, repo_root)) {
		wxMessageBox(
			"Nao foi possivel preparar os arquivos do mapa para commit.\n\n" + BuildCommandOutput(output, errors),
			"Deploy do Mapa",
			wxOK | wxICON_ERROR,
			g_gui.root);
		return;
	}

	command = "git add -u --" + pathspec_args;
	if (!ExecuteCommandSync(command, output, errors, exit_code, repo_root)) {
		wxMessageBox(
			"Nao foi possivel sincronizar remocoes dos arquivos do mapa.\n\n" + BuildCommandOutput(output, errors),
			"Deploy do Mapa",
			wxOK | wxICON_ERROR,
			g_gui.root);
		return;
	}

	command = "git commit -m " + QuoteShellArgument(commit_message) + " --" + pathspec_args;
	const bool commit_ok = ExecuteCommandSync(command, output, errors, exit_code, repo_root);
	if (!commit_ok) {
		const wxString commit_output = BuildCommandOutput(output, errors);
		if (!IsNothingToCommitMessage(commit_output)) {
			wxMessageBox(
				"Nao foi possivel concluir o commit.\n\n" + commit_output,
				"Deploy do Mapa",
				wxOK | wxICON_ERROR,
				g_gui.root);
			return;
		}
	}

	command = "git push";
	if (!ExecuteCommandSync(command, output, errors, exit_code, repo_root)) {
		wxMessageBox(
			"Nao foi possivel concluir o push.\n\n" + BuildCommandOutput(output, errors),
			"Deploy do Mapa",
			wxOK | wxICON_ERROR,
			g_gui.root);
		return;
	}

	wxString commit_hash = "N/A";
	command = "git rev-parse --short HEAD";
	if (ExecuteCommandSync(command, output, errors, exit_code, repo_root) && !output.empty()) {
		commit_hash = output.front();
		commit_hash.Trim(true);
		commit_hash.Trim(false);
	}

	wxString success_message = "Deploy concluido com sucesso.\nRepositorio: " + repo_root;
	if (!commit_hash.empty() && commit_hash != "N/A") {
		success_message += "\nCommit: " + commit_hash;
	}
	wxMessageBox(success_message, "Deploy do Mapa", wxOK | wxICON_INFORMATION, g_gui.root);
}
