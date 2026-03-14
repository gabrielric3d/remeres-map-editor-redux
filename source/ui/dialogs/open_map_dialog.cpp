//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "ui/dialogs/open_map_dialog.h"
#include "ui/theme.h"
#include "app/settings.h"
#include "app/definitions.h"

#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/filename.h>
#include <wx/scrolwin.h>

#include <algorithm>
#include <cmath>
#include <sstream>

#ifndef M_PI
	#define M_PI 3.14159265358979323846
#endif

wxDEFINE_EVENT(EVT_FAVORITE_TOGGLED, wxCommandEvent);

namespace {

bool PathEquals(const wxString& a, const wxString& b) {
#ifdef __WINDOWS__
	return a.CmpNoCase(b) == 0;
#else
	return a == b;
#endif
}

void AddUniquePath(std::vector<wxString>& paths, const wxString& value) {
	if (value.empty()) return;
	for (const auto& p : paths) {
		if (PathEquals(p, value)) return;
	}
	paths.push_back(value);
}

wxBitmap CreateStarBitmap(wxWindow* win, const wxColour& colour, bool filled) {
	const int sz = wxWindow::FromDIP(16, win);
	wxBitmap bmp(sz, sz, 32);
	wxMemoryDC dc;
	dc.SelectObject(bmp);
	dc.SetBackground(*wxTRANSPARENT_BRUSH);
	dc.Clear();

	wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
	if (gc) {
		const double cx = (sz - 1) / 2.0;
		const double cy = (sz - 1) / 2.0;
		const double outer = sz * 0.45;
		const double inner = sz * 0.2;
		wxGraphicsPath path = gc->CreatePath();
		for (int i = 0; i < 10; ++i) {
			const double angle = (i * 36.0 - 90.0) * (M_PI / 180.0);
			const double r = (i % 2 == 0) ? outer : inner;
			const double x = cx + std::cos(angle) * r;
			const double y = cy + std::sin(angle) * r;
			if (i == 0) path.MoveToPoint(x, y);
			else path.AddLineToPoint(x, y);
		}
		path.CloseSubpath();
		gc->SetPen(wxPen(colour, 1));
		gc->SetBrush(filled ? wxBrush(colour) : *wxTRANSPARENT_BRUSH);
		gc->DrawPath(path);
		delete gc;
	}
	dc.SelectObject(wxNullBitmap);
	return bmp;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────
// OpenMapDialog
// ─────────────────────────────────────────────────────────────────

OpenMapDialog::OpenMapDialog(wxWindow* parent, const std::vector<wxString>& recent)
	: wxDialog(parent, wxID_ANY, "Open Map", wxDefaultPosition,
		wxWindow::FromDIP(wxSize(620, 560), parent),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	  recentFiles(recent) {
	SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	BuildUI();
	PopulateItems();
	CentreOnParent();
}

void OpenMapDialog::BuildUI() {
	auto* rootSizer = new wxBoxSizer(wxVERTICAL);

	// ── Header with title and Browse button ──
	auto* headerSizer = new wxBoxSizer(wxHORIZONTAL);

	auto* titleText = new wxStaticText(this, wxID_ANY, "Open Map");
	wxFont titleFont = Theme::GetFont(12, true);
	titleText->SetFont(titleFont);
	titleText->SetForegroundColour(Theme::Get(Theme::Role::Text));
	headerSizer->Add(titleText, 1, wxALIGN_CENTER_VERTICAL);

	auto* browseBtn = new wxButton(this, wxID_ANY, "Browse...");
	browseBtn->SetBackgroundColour(Theme::Get(Theme::Role::PrimaryButton));
	browseBtn->SetForegroundColour(Theme::Get(Theme::Role::TextOnAccent));
	browseBtn->Bind(wxEVT_BUTTON, &OpenMapDialog::OnBrowse, this);
	headerSizer->Add(browseBtn, 0, wxALIGN_CENTER_VERTICAL);

	rootSizer->Add(headerSizer, 0, wxEXPAND | wxALL, Theme::Grid(3));

	// ── Divider ──
	auto* divider = new wxPanel(this, wxID_ANY);
	divider->SetBackgroundColour(Theme::Get(Theme::Role::Border));
	divider->SetMinSize(wxSize(-1, wxWindow::FromDIP(1, this)));
	rootSizer->Add(divider, 0, wxEXPAND | wxLEFT | wxRIGHT, Theme::Grid(3));

	// ── Scrollable content area ──
	scrollArea = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	scrollArea->SetScrollRate(0, wxWindow::FromDIP(8, this));
	scrollArea->SetBackgroundColour(Theme::Get(Theme::Role::Background));

	contentSizer = new wxBoxSizer(wxVERTICAL);
	scrollArea->SetSizer(contentSizer);

	rootSizer->Add(scrollArea, 1, wxEXPAND | wxTOP, Theme::Grid(1));

	// ── Bottom buttons ──
	auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
	btnSizer->AddStretchSpacer();
	auto* cancelBtn = new wxButton(this, wxID_CANCEL, "Cancel");
	cancelBtn->Bind(wxEVT_BUTTON, &OpenMapDialog::OnClose, this);
	btnSizer->Add(cancelBtn, 0, wxALL, Theme::Grid(1));

	rootSizer->Add(btnSizer, 0, wxEXPAND | wxALL, Theme::Grid(2));

	SetSizer(rootSizer);
}

void OpenMapDialog::PopulateItems() {
	scrollArea->Freeze();
	contentSizer->Clear(true);

	auto favorites = LoadFavorites();

	// Helper to add a section title
	auto addTitle = [this](const wxString& text) {
		auto* label = new wxStaticText(scrollArea, wxID_ANY, text);
		wxFont font = Theme::GetFont(10, true);
		label->SetFont(font);
		label->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
		contentSizer->Add(label, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM,
			Theme::Grid(2));
	};

	auto addDivider = [this]() {
		auto* div = new wxPanel(scrollArea, wxID_ANY);
		div->SetBackgroundColour(Theme::Get(Theme::Role::Border));
		div->SetMinSize(wxSize(-1, wxWindow::FromDIP(1, this)));
		contentSizer->Add(div, 0, wxEXPAND | wxLEFT | wxRIGHT, Theme::Grid(2));
	};

	// Build unique lists: favorites first, then recent (excluding favorites)
	std::vector<wxString> uniqueFavorites;
	for (const auto& f : favorites) {
		AddUniquePath(uniqueFavorites, f);
	}
	std::vector<wxString> uniqueRecent;
	for (const auto& f : recentFiles) {
		if (!IsFavorite(uniqueFavorites, f)) {
			AddUniquePath(uniqueRecent, f);
		}
	}

	// ── Favorites section ──
	if (!uniqueFavorites.empty()) {
		addTitle("Favorites");
		addDivider();
		for (const auto& file : uniqueFavorites) {
			auto* item = new RecentMapItem(scrollArea, file, true);
			contentSizer->Add(item, 0, wxEXPAND);
			item->Bind(wxEVT_LEFT_UP, &OpenMapDialog::OnItemClicked, this);
			addDivider();
		}
	}

	// ── Recent Maps section ──
	if (!uniqueRecent.empty()) {
		addTitle("Recent Maps");
		addDivider();
		for (const auto& file : uniqueRecent) {
			auto* item = new RecentMapItem(scrollArea, file, false);
			contentSizer->Add(item, 0, wxEXPAND);
			item->Bind(wxEVT_LEFT_UP, &OpenMapDialog::OnItemClicked, this);
			addDivider();
		}
	}

	if (uniqueFavorites.empty() && uniqueRecent.empty()) {
		auto* empty = new wxStaticText(scrollArea, wxID_ANY,
			"No recent maps. Click Browse to open a map file.");
		empty->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
		contentSizer->Add(empty, 0, wxALL, Theme::Grid(4));
	}

	scrollArea->FitInside();
	scrollArea->Layout();
	scrollArea->Thaw();

	// Bind favorite toggle events from children
	scrollArea->Bind(EVT_FAVORITE_TOGGLED, &OpenMapDialog::OnFavoriteToggled, this);
}

void OpenMapDialog::OnBrowse(wxCommandEvent&) {
	wxFileDialog dlg(this, "Open map file", wxEmptyString, wxEmptyString,
		MAP_LOAD_FILE_WILDCARD, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		selectedPath = dlg.GetPath();
		EndModal(wxID_OK);
	}
}

void OpenMapDialog::OnItemClicked(wxMouseEvent& event) {
	auto* item = dynamic_cast<RecentMapItem*>(event.GetEventObject());
	if (!item) return;

	selectedPath = item->GetFilePath();
	EndModal(wxID_OK);
}

void OpenMapDialog::OnFavoriteToggled(wxCommandEvent& event) {
	wxString path = event.GetString();
	auto favorites = LoadFavorites();

	if (event.GetInt() != 0) {
		// Add to favorites
		AddUniquePath(favorites, path);
	} else {
		// Remove from favorites
		favorites.erase(
			std::remove_if(favorites.begin(), favorites.end(),
				[&path](const wxString& p) { return PathEquals(p, path); }),
			favorites.end());
	}
	SaveFavorites(favorites);
	PopulateItems();
}

void OpenMapDialog::OnClose(wxCommandEvent&) {
	EndModal(wxID_CANCEL);
}

std::vector<wxString> OpenMapDialog::LoadFavorites() const {
	std::vector<wxString> favorites;
	std::string raw = g_settings.getString(Config::FAVORITE_FILES);
	std::istringstream stream(raw);
	std::string line;
	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (!line.empty()) {
			AddUniquePath(favorites, wxString::FromUTF8(line));
		}
	}
	return favorites;
}

void OpenMapDialog::SaveFavorites(const std::vector<wxString>& favorites) const {
	std::ostringstream stream;
	for (size_t i = 0; i < favorites.size(); ++i) {
		if (i > 0) stream << "\n";
		stream << favorites[i].ToStdString(wxConvUTF8);
	}
	g_settings.setString(Config::FAVORITE_FILES, stream.str());
}

bool OpenMapDialog::IsFavorite(const std::vector<wxString>& favorites, const wxString& path) const {
	for (const auto& f : favorites) {
		if (PathEquals(f, path)) return true;
	}
	return false;
}

// ─────────────────────────────────────────────────────────────────
// RecentMapItem
// ─────────────────────────────────────────────────────────────────

RecentMapItem::RecentMapItem(wxWindow* parent, const wxString& path, bool favorite)
	: wxPanel(parent, wxID_ANY),
	  filePath(path),
	  isFavorite(favorite) {

	const wxColour favoriteColor(214, 170, 46);
	const wxColour textColor = isFavorite ? favoriteColor : Theme::Get(Theme::Role::Text);
	const wxColour subtleColor = isFavorite ? favoriteColor : Theme::Get(Theme::Role::TextSubtle);

	// Title (filename only)
	titleLabel = new wxStaticText(this, wxID_ANY, wxFileNameFromPath(filePath));

	wxFont titleFont = Theme::GetFont(9, true);
	titleLabel->SetFont(titleFont);
	titleLabel->SetForegroundColour(textColor);
	titleLabel->SetToolTip(filePath);

	// File path (ellipsized)
	const int pathWidth = wxWindow::FromDIP(420, this);
	pathLabel = new wxStaticText(this, wxID_ANY, filePath, wxDefaultPosition,
		wxSize(pathWidth, -1), wxST_ELLIPSIZE_START);

	pathLabel->SetFont(Theme::GetFont(8));
	pathLabel->SetForegroundColour(subtleColor);
	pathLabel->SetToolTip(filePath);

	// Modification date
	wxString dateStr = "Last modified: -";
	wxFileName fn(filePath);
	if (fn.FileExists()) {
		wxDateTime mod = fn.GetModificationTime();
		if (mod.IsValid()) {
			dateStr = "Last modified: " + mod.FormatISODate() + " " + mod.FormatISOTime();
		}
	}
	dateLabel = new wxStaticText(this, wxID_ANY, dateStr);

	dateLabel->SetFont(Theme::GetFont(8));
	dateLabel->SetForegroundColour(subtleColor);

	// Star bitmap
	starFilled = CreateStarBitmap(this, Theme::Get(Theme::Role::Accent), true);
	starOutline = CreateStarBitmap(this, Theme::Get(Theme::Role::TextSubtle), false);
	starOutlineHover = CreateStarBitmap(this, Theme::Get(Theme::Role::Accent), false);

	starBitmap = new wxStaticBitmap(this, wxID_ANY,
		isFavorite ? starFilled : starOutline);

	// Set initial background on all children
	wxColour bg = Theme::Get(Theme::Role::Background);
	SetBackgroundColour(bg);
	titleLabel->SetBackgroundColour(bg);
	pathLabel->SetBackgroundColour(bg);
	dateLabel->SetBackgroundColour(bg);
	starBitmap->SetBackgroundColour(bg);

	// Layout
	const int pad = wxWindow::FromDIP(6, this);
	auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);
	auto* textSizer = new wxBoxSizer(wxVERTICAL);
	textSizer->Add(titleLabel, 0, wxBOTTOM, wxWindow::FromDIP(1, this));
	textSizer->Add(pathLabel, 0, wxBOTTOM, wxWindow::FromDIP(1, this));
	textSizer->Add(dateLabel, 0);
	mainSizer->Add(textSizer, 1, wxEXPAND | wxALL, pad);
	mainSizer->Add(starBitmap, 0, wxALIGN_TOP | wxTOP | wxRIGHT, pad);
	SetSizer(mainSizer);

	SetMinSize(wxSize(-1, wxWindow::FromDIP(64, this)));

	// Events
	Bind(wxEVT_ENTER_WINDOW, &RecentMapItem::OnMouseEnter, this);
	Bind(wxEVT_LEAVE_WINDOW, &RecentMapItem::OnMouseLeave, this);
	titleLabel->Bind(wxEVT_LEFT_UP, &RecentMapItem::PropagateClick, this);
	pathLabel->Bind(wxEVT_LEFT_UP, &RecentMapItem::PropagateClick, this);
	dateLabel->Bind(wxEVT_LEFT_UP, &RecentMapItem::PropagateClick, this);
	starBitmap->Bind(wxEVT_LEFT_UP, &RecentMapItem::OnFavoriteClick, this);
}

void RecentMapItem::OnMouseEnter(wxMouseEvent& event) {
	if (!isHover) {
		isHover = true;
		UpdateColors(true);
		Refresh();
	}
}

void RecentMapItem::OnMouseLeave(wxMouseEvent& event) {
	if (isHover && !GetScreenRect().Contains(ClientToScreen(event.GetPosition()))) {
		isHover = false;
		UpdateColors(false);
		Refresh();
	}
}

void RecentMapItem::UpdateColors(bool hover) {
	wxColour bg = hover
		? Theme::Get(Theme::Role::CardBaseHover)
		: Theme::Get(Theme::Role::Background);
	SetBackgroundColour(bg);
	titleLabel->SetBackgroundColour(bg);
	pathLabel->SetBackgroundColour(bg);
	dateLabel->SetBackgroundColour(bg);
	starBitmap->SetBackgroundColour(bg);

	const wxColour favoriteColor(214, 170, 46);
	if (isFavorite) {
		titleLabel->SetForegroundColour(favoriteColor);
		pathLabel->SetForegroundColour(favoriteColor);
		dateLabel->SetForegroundColour(favoriteColor);
	} else {
		wxColour textCol = hover ? Theme::Get(Theme::Role::Accent) : Theme::Get(Theme::Role::Text);
		wxColour subCol = hover ? Theme::Get(Theme::Role::Accent) : Theme::Get(Theme::Role::TextSubtle);
		titleLabel->SetForegroundColour(textCol);
		pathLabel->SetForegroundColour(subCol);
		dateLabel->SetForegroundColour(subCol);
	}
	if (!isFavorite) {
		starBitmap->SetBitmap(hover ? starOutlineHover : starOutline);
	}
	titleLabel->Refresh();
	pathLabel->Refresh();
	dateLabel->Refresh();
	starBitmap->Refresh();
}

void RecentMapItem::PropagateClick(wxMouseEvent& event) {
	event.ResumePropagation(1);
	event.SetEventObject(this);
	event.Skip();
}

void RecentMapItem::OnFavoriteClick(wxMouseEvent& event) {
	wxCommandEvent favEvent(EVT_FAVORITE_TOGGLED);
	favEvent.SetString(filePath);
	favEvent.SetInt(isFavorite ? 0 : 1);
	favEvent.SetEventObject(this);
	// Send up to the scroll area / dialog
	GetParent()->GetEventHandler()->ProcessEvent(favEvent);
	event.StopPropagation();
}
