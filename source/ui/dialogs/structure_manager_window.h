//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_STRUCTURE_MANAGER_WINDOW_H_
#define RME_STRUCTURE_MANAGER_WINDOW_H_

#include <wx/dialog.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/listbox.h>
#include <wx/overlay.h>
#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include <vector>

class wxPopupWindow;
class wxChoice;
class wxSpinCtrl;
class BaseMap;
class Editor;
class Position;

class StructureManagerDialog : public wxDialog {
public:
	explicit StructureManagerDialog(wxWindow* parent);
	~StructureManagerDialog() override;
	static bool HandleGlobalHotkey(wxKeyEvent& event);
	static bool GetFixedSavePreview(int& width, int& height, int& zFrom, int& zTo);
	static bool RotatePaste();
	static bool CanRotatePaste();
	static bool IsKeepPasteActive();

private:
	class StructurePreviewPanel;

	struct TutorialStepInfo {
		wxString title;
		wxString body;
	};

	struct StructureEntry {
		wxString name;
		wxString path;
		wxString category;
	};

	void CreateControls();
	void LoadStructures();
	void BuildCategoryTree();
	void RefreshItemList();
	void UpdateSelectionUi();
	void SetStatusText(const wxString& text);
	const StructureEntry* GetSelectedEntry(int* outIndex) const;
	void SelectCategoryByPath(const wxString& path);
	void SelectEntryByName(const wxString& name);
	void StartPasteFromEntry(const StructureEntry& entry);
	void RenameSelectedCategory();
	void RenameSelectedStructure();
	void SelectAdjacentEntry(int delta);
	wxString GetSelectedCategoryPath() const;
	bool HandleGlobalHotkeyInternal(wxKeyEvent& event);
	void AddCategory(bool asChild);
	void UpdatePreview(const StructureEntry* entry);
	void StartTutorial();
	void StopTutorial();
	void UpdateTutorialStep();
	void RenderTutorialOverlay();
	void ClearTutorialOverlay();
	void PositionTutorialPopup();
	wxRect GetTutorialHighlightRect() const;
	TutorialStepInfo GetTutorialStepInfo(int step) const;
	int GetTutorialStepCount() const;
	void SetTutorialUiEnabled(bool enabled);
	bool CanReorderCurrentList() const;
	wxString GetOrderFilePath(const wxString& category) const;
	bool ReadOrderFile(const wxString& category, std::vector<std::string>& out) const;
	void WriteOrderFile(const wxString& category, const std::vector<std::string>& names) const;
	void WriteOrderFile(const wxString& category, const std::vector<const StructureEntry*>& entries) const;
	std::vector<const StructureEntry*> GetOrderedEntriesForCategory(const wxString& category) const;
	void MoveSelectedStructure(int delta);
	void MoveEntryToIndex(int fromIndex, int toIndex);
	void UpdateSaveOptionsUi();
	wxString GetAutoNameBase() const;
	int GetNextAutoNameIndex(const wxString& base, const wxString& categoryPath) const;
	wxString GetAutoNamePrefix(const wxString& base) const;
	bool BuildFixedAreaBuffer(Editor& editor, const Position& center, int width, int height, int zFrom, int zTo,
		BaseMap& outMap, Position& outMinPos, Position& outMaxPos, int& outTiles, int& outItems) const;
	void OnRenameStructure(wxCommandEvent& event);
	void OnMoveStructureUp(wxCommandEvent& event);
	void OnMoveStructureDown(wxCommandEvent& event);
	void OnListLeftDown(wxMouseEvent& event);
	void OnListLeftUp(wxMouseEvent& event);
	void OnListMouseMove(wxMouseEvent& event);
	void OnAutoNameToggle(wxCommandEvent& event);
	void OnAutoNameBaseChanged(wxCommandEvent& event);
	void OnFixedSizeToggle(wxCommandEvent& event);

	void OnSaveSelection(wxCommandEvent& event);
	void OnPaste(wxCommandEvent& event);
	void OnDelete(wxCommandEvent& event);
	void OnSelectionChanged(wxCommandEvent& event);
	void OnListKeyDown(wxKeyEvent& event);
	void OnRenameCategory(wxCommandEvent& event);
	void OnAddCategory(wxCommandEvent& event);
	void OnAddSubcategory(wxCommandEvent& event);
	void OnRemoveCategory(wxCommandEvent& event);
	void OnRemoveSubcategory(wxCommandEvent& event);
	void OnCategoryChanged(wxTreeEvent& event);
	void OnSearchChanged(wxCommandEvent& event);
	void OnHowToUse(wxCommandEvent& event);
	void OnTutorialPrev(wxCommandEvent& event);
	void OnTutorialNext(wxCommandEvent& event);
	void OnTutorialClose(wxCommandEvent& event);
	void OnPaint(wxPaintEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnMove(wxMoveEvent& event);
	void OnCharHook(wxKeyEvent& event);
	void OnClose(wxCloseEvent& event);
	void OnPasteRotationChanged(wxCommandEvent& event);

	std::vector<StructureEntry> m_entries;
	std::vector<const StructureEntry*> m_listEntries;
	std::vector<wxString> m_categoryPaths;
	wxString m_baseDir;
	wxString m_currentCategoryPath;
	wxTreeCtrl* m_categoryTree = nullptr;
	wxTextCtrl* m_searchCtrl = nullptr;
	wxListBox* m_list = nullptr;
	wxCheckBox* m_keepPasteCheck = nullptr;
	wxButton* m_renameCategoryButton = nullptr;
	wxButton* m_addCategoryButton = nullptr;
	wxButton* m_addSubcategoryButton = nullptr;
	wxButton* m_removeCategoryButton = nullptr;
	wxButton* m_removeSubcategoryButton = nullptr;
	wxButton* m_renameStructureButton = nullptr;
	wxButton* m_moveStructureUpButton = nullptr;
	wxButton* m_moveStructureDownButton = nullptr;
	wxButton* m_helpButton = nullptr;
	wxButton* m_saveButton = nullptr;
	wxButton* m_pasteButton = nullptr;
	wxButton* m_deleteButton = nullptr;
	wxChoice* m_pasteRotationChoice = nullptr;
	wxStaticText* m_detailsText = nullptr;
	wxStaticText* m_statusText = nullptr;
	wxCheckBox* m_fixedSizeCheck = nullptr;
	wxSpinCtrl* m_fixedWidthSpin = nullptr;
	wxSpinCtrl* m_fixedHeightSpin = nullptr;
	wxSpinCtrl* m_fixedZFromSpin = nullptr;
	wxSpinCtrl* m_fixedZToSpin = nullptr;
	wxCheckBox* m_autoNameCheck = nullptr;
	wxTextCtrl* m_autoNameBaseCtrl = nullptr;
	wxStaticText* m_autoNamePreview = nullptr;
	StructurePreviewPanel* m_previewPanel = nullptr;
	wxString m_previewPath;
	int m_currentPasteRotationTurns = 0;
	bool m_suppressAutoPaste = false;
	bool m_tutorialActive = false;
	int m_tutorialStep = 0;
	bool m_tutorialLockMove = false;
	bool m_tutorialMoveGuard = false;
	bool m_listDragActive = false;
	int m_listDragStart = wxNOT_FOUND;
	wxPoint m_tutorialLockPos;
	wxOverlay m_tutorialOverlay;
	wxPopupWindow* m_tutorialPopup = nullptr;
	wxStaticText* m_tutorialStepLabel = nullptr;
	wxStaticText* m_tutorialBodyText = nullptr;
	wxButton* m_tutorialPrevButton = nullptr;
	wxButton* m_tutorialNextButton = nullptr;
	wxButton* m_tutorialCloseButton = nullptr;
	static StructureManagerDialog* s_active;

	DECLARE_EVENT_TABLE()
};

#endif
