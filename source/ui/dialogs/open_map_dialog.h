//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_UI_OPEN_MAP_DIALOG_H
#define RME_UI_OPEN_MAP_DIALOG_H

#include <wx/wx.h>
#include <wx/filename.h>
#include <vector>

class RecentMapItem;

// Dialog that replaces the plain wxFileDialog for File > Open.
// Shows Favorites and Recent Maps with a Browse button.
class OpenMapDialog : public wxDialog {
public:
	OpenMapDialog(wxWindow* parent, const std::vector<wxString>& recentFiles);

	// Returns the path the user chose (empty if cancelled).
	wxString GetSelectedPath() const { return selectedPath; }

private:
	void BuildUI();
	void PopulateItems();

	void OnBrowse(wxCommandEvent& event);
	void OnItemClicked(wxMouseEvent& event);
	void OnFavoriteToggled(wxCommandEvent& event);
	void OnClose(wxCommandEvent& event);

	// Favorites persistence helpers
	std::vector<wxString> LoadFavorites() const;
	void SaveFavorites(const std::vector<wxString>& favorites) const;
	bool IsFavorite(const std::vector<wxString>& favorites, const wxString& path) const;

	wxString selectedPath;
	std::vector<wxString> recentFiles;

	wxScrolledWindow* scrollArea = nullptr;
	wxBoxSizer* contentSizer = nullptr;
};

// A single row representing a map file (used in both Favorites and Recent sections).
class RecentMapItem : public wxPanel {
public:
	RecentMapItem(wxWindow* parent, const wxString& filePath, bool isFavorite);

	wxString GetFilePath() const { return filePath; }
	bool IsFavorite() const { return isFavorite; }

private:
	void OnMouseEnter(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void OnFavoriteClick(wxMouseEvent& event);
	void PropagateClick(wxMouseEvent& event);
	void UpdateColors(bool hover);

	wxString filePath;
	bool isFavorite;
	bool isHover = false;

	wxStaticText* titleLabel = nullptr;
	wxStaticText* pathLabel = nullptr;
	wxStaticText* dateLabel = nullptr;
	wxStaticBitmap* starBitmap = nullptr;

	wxBitmap starFilled;
	wxBitmap starOutline;
	wxBitmap starOutlineHover;
};

// Custom event for favorite toggle
wxDECLARE_EVENT(EVT_FAVORITE_TOGGLED, wxCommandEvent);

#endif
