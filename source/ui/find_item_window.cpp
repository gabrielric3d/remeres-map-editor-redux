#include "app/main.h"
#include "ui/find_item_window.h"

#include "app/settings.h"
#include "brushes/creature/creature_brush.h"
#include "brushes/raw/raw_brush.h"
#include "item_definitions/core/item_definition_store.h"
#include "ui/gui.h"
#include "ui/theme.h"
#include "util/common.h"
#include "util/image_manager.h"

#include <wx/button.h>
#include <wx/bmpbuttn.h>
#include <wx/checkbox.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/srchctrl.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace {
	template <typename Enum>
	constexpr size_t toIndex(Enum value) {
		return static_cast<size_t>(std::to_underlying(value));
	}

	class ForwardingSearchCtrl final : public wxSearchCtrl {
	public:
		ForwardingSearchCtrl(wxWindow* parent, wxWindowID id) :
			wxSearchCtrl(parent, id, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER) {
			Bind(wxEVT_KEY_DOWN, &ForwardingSearchCtrl::OnKeyDown, this);
		}

	private:
		void OnKeyDown(wxKeyEvent& event) {
			switch (event.GetKeyCode()) {
				case WXK_UP:
				case WXK_DOWN:
				case WXK_PAGEUP:
				case WXK_PAGEDOWN:
				case WXK_HOME:
				case WXK_END:
					if (auto* top_level = wxGetTopLevelParent(this)) {
						if (top_level->GetEventHandler()->ProcessEvent(event)) {
							return;
						}
						return;
					}
					break;
				default:
					break;
			}

			event.Skip();
		}
	};

	constexpr auto kTypeCheckboxes = std::to_array<std::pair<AdvancedFinderTypeFilter, std::string_view>>({
		{ AdvancedFinderTypeFilter::Depot, "Depot" },
		{ AdvancedFinderTypeFilter::Mailbox, "Mailbox" },
		{ AdvancedFinderTypeFilter::TrashHolder, "Trash Holder" },
		{ AdvancedFinderTypeFilter::Container, "Container" },
		{ AdvancedFinderTypeFilter::Door, "Door" },
		{ AdvancedFinderTypeFilter::MagicField, "Magic Field" },
		{ AdvancedFinderTypeFilter::Teleport, "Teleport" },
		{ AdvancedFinderTypeFilter::Bed, "Bed" },
		{ AdvancedFinderTypeFilter::Key, "Key" },
		{ AdvancedFinderTypeFilter::Podium, "Podium" },
		{ AdvancedFinderTypeFilter::Weapon, "Weapon" },
		{ AdvancedFinderTypeFilter::Ammo, "Ammo" },
		{ AdvancedFinderTypeFilter::Armor, "Armor" },
		{ AdvancedFinderTypeFilter::Rune, "Rune" },
		{ AdvancedFinderTypeFilter::Creature, "Creature" },
	});

	constexpr auto kPropertyCheckboxes = std::to_array<std::pair<AdvancedFinderPropertyFilter, std::string_view>>({
		{ AdvancedFinderPropertyFilter::Unpassable, "Unpassable" },
		{ AdvancedFinderPropertyFilter::Unmovable, "Unmovable" },
		{ AdvancedFinderPropertyFilter::BlockMissiles, "Block Missiles" },
		{ AdvancedFinderPropertyFilter::BlockPathfinder, "Block Pathfinder" },
		{ AdvancedFinderPropertyFilter::HasElevation, "Has Elevation" },
		{ AdvancedFinderPropertyFilter::FloorChange, "Floor Change" },
		{ AdvancedFinderPropertyFilter::FullTile, "Full Tile" },
	});

	constexpr auto kInteractionCheckboxes = std::to_array<std::pair<AdvancedFinderInteractionFilter, std::string_view>>({
		{ AdvancedFinderInteractionFilter::Readable, "Readable" },
		{ AdvancedFinderInteractionFilter::Writeable, "Writeable" },
		{ AdvancedFinderInteractionFilter::Pickupable, "Pickupable" },
		{ AdvancedFinderInteractionFilter::ForceUse, "Force Use" },
		{ AdvancedFinderInteractionFilter::DistRead, "Dist Read" },
		{ AdvancedFinderInteractionFilter::Rotatable, "Rotatable" },
		{ AdvancedFinderInteractionFilter::Hangable, "Hangable" },
	});

	constexpr auto kVisualCheckboxes = std::to_array<std::pair<AdvancedFinderVisualFilter, std::string_view>>({
		{ AdvancedFinderVisualFilter::HasLight, "Has Light" },
		{ AdvancedFinderVisualFilter::Animation, "Animation" },
		{ AdvancedFinderVisualFilter::AlwaysTop, "Always Top" },
		{ AdvancedFinderVisualFilter::IgnoreLook, "Ignore Look" },
		{ AdvancedFinderVisualFilter::HasCharges, "Has Charges" },
		{ AdvancedFinderVisualFilter::ClientCharges, "Client Charges" },
		{ AdvancedFinderVisualFilter::Decays, "Decays" },
		{ AdvancedFinderVisualFilter::HasSpeed, "Has Speed" },
	});

	wxStaticBoxSizer* createStaticBox(wxWindow* parent, std::string_view title) {
		auto* group = newd wxStaticBoxSizer(wxVERTICAL, parent, wxString::FromUTF8(title.data(), title.size()));
		group->GetStaticBox()->SetForegroundColour(Theme::Get(Theme::Role::Text));
		group->GetStaticBox()->SetFont(Theme::GetFont(9, true));
		return group;
	}

	wxStaticText* createHintText(wxWindow* parent, std::string_view text) {
		auto* label = newd wxStaticText(parent, wxID_ANY, wxString::FromUTF8(text.data(), text.size()));
		label->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
		label->SetFont(Theme::GetFont(9, false));
		return label;
	}

	template <typename FilterEnum, size_t Count>
	wxGridSizer* addCheckboxGrid(wxWindow* parent, const std::array<std::pair<FilterEnum, std::string_view>, Count>& entries, std::array<wxCheckBox*, Count>& targets) {
		auto* grid = newd wxGridSizer(0, 2, 6, 12);
		for (const auto& [filter, label] : entries) {
			auto* checkbox = newd wxCheckBox(parent, wxID_ANY, wxString::FromUTF8(label.data(), label.size()));
			checkbox->SetFont(Theme::GetFont(9, false));
			checkbox->SetForegroundColour(Theme::Get(Theme::Role::Text));
			targets[toIndex(filter)] = checkbox;
			grid->Add(checkbox, 0, wxEXPAND);
		}
		return grid;
	}

	struct SessionFinderState {
		AdvancedFinderPersistedState persisted;
		AdvancedFinderResultViewMode result_view_mode = AdvancedFinderResultViewMode::Grid;
	};

	std::optional<SessionFinderState> g_session_finder_state;
}

FindItemDialog::FindItemDialog(
	wxWindow* parent,
	const wxString& title,
	bool onlyPickupables,
	ActionSet action_set,
	AdvancedFinderDefaultAction default_action,
	bool include_creatures
) :
	wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	only_pickupables_(onlyPickupables),
	include_creatures_(include_creatures),
	persist_shared_state_(action_set == ActionSet::SearchAndSelect),
	action_set_(action_set),
	default_action_(default_action) {
	buildLayout();
	bindEvents();
	loadInitialState();
	SetIcons(IMAGE_MANAGER.GetIconBundle(ICON_SEARCH));
}

FindItemDialog::~FindItemDialog() {
	savePersistedState();
}

void FindItemDialog::buildLayout() {
	SetMinSize(FromDIP(wxSize(1360, 860)));
	SetBackgroundColour(Theme::Get(Theme::Role::Surface));

	auto* root_sizer = newd wxBoxSizer(wxVERTICAL);
	auto* body_sizer = newd wxBoxSizer(wxHORIZONTAL);

	auto* left_panel = newd wxPanel(this, wxID_ANY);
	left_panel->SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	left_panel->SetMinSize(FromDIP(wxSize(360, -1)));
	auto* left_sizer = newd wxBoxSizer(wxVERTICAL);

	auto* search_box = createStaticBox(left_panel, "Search");
	auto* search_box_sizer = newd wxBoxSizer(wxHORIZONTAL);
	search_field_ = newd ForwardingSearchCtrl(search_box->GetStaticBox(), wxID_ANY);
	search_field_->SetDescriptiveText("Name, SID, or CID...");
	search_field_->SetMinSize(FromDIP(wxSize(-1, 32)));
	search_box_sizer->Add(search_field_, 1, wxEXPAND | wxALL, 8);
	reset_search_button_ = newd wxBitmapButton(search_box->GetStaticBox(), wxID_ANY, IMAGE_MANAGER.GetBitmap(ICON_XMARK, wxSize(16, 16)));
	reset_search_button_->SetMinSize(FromDIP(wxSize(30, 30)));
	search_box_sizer->Add(reset_search_button_, 0, wxTOP | wxRIGHT | wxBOTTOM, 8);
	search_box->Add(search_box_sizer, 1, wxEXPAND);
	left_sizer->Add(search_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

	auto* filter_scroll = newd wxScrolledWindow(left_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE);
	filter_scroll->SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	filter_scroll->SetScrollRate(0, FromDIP(18));

	auto* filter_scroll_sizer = newd wxBoxSizer(wxVERTICAL);
	auto* or_box = createStaticBox(filter_scroll, "OR");
	or_box->Add(addCheckboxGrid(or_box->GetStaticBox(), kTypeCheckboxes, type_checkboxes_), 0, wxEXPAND | wxALL, 8);
	filter_scroll_sizer->Add(or_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);

	auto* and_box = createStaticBox(filter_scroll, "AND");
	auto* and_grid = newd wxGridSizer(0, 2, 6, 12);
	for (const auto& [property_filter, label] : kPropertyCheckboxes) {
		auto* checkbox = newd wxCheckBox(and_box->GetStaticBox(), wxID_ANY, wxString::FromUTF8(label.data(), label.size()));
		checkbox->SetFont(Theme::GetFont(9, false));
		checkbox->SetForegroundColour(Theme::Get(Theme::Role::Text));
		property_checkboxes_[toIndex(property_filter)] = checkbox;
		and_grid->Add(checkbox, 0, wxEXPAND);
	}
	for (const auto& [interaction_filter, label] : kInteractionCheckboxes) {
		auto* checkbox = newd wxCheckBox(and_box->GetStaticBox(), wxID_ANY, wxString::FromUTF8(label.data(), label.size()));
		checkbox->SetFont(Theme::GetFont(9, false));
		checkbox->SetForegroundColour(Theme::Get(Theme::Role::Text));
		interaction_checkboxes_[toIndex(interaction_filter)] = checkbox;
		and_grid->Add(checkbox, 0, wxEXPAND);
	}
	for (const auto& [visual_filter, label] : kVisualCheckboxes) {
		auto* checkbox = newd wxCheckBox(and_box->GetStaticBox(), wxID_ANY, wxString::FromUTF8(label.data(), label.size()));
		checkbox->SetFont(Theme::GetFont(9, false));
		checkbox->SetForegroundColour(Theme::Get(Theme::Role::Text));
		visual_checkboxes_[toIndex(visual_filter)] = checkbox;
		and_grid->Add(checkbox, 0, wxEXPAND);
	}
	and_box->Add(and_grid, 0, wxEXPAND | wxALL, 8);
	filter_scroll_sizer->Add(and_box, 0, wxEXPAND | wxALL, 4);

	auto* hints_box = createStaticBox(filter_scroll, "Hints");
	hints_box->Add(createHintText(hints_box->GetStaticBox(), "Double left click item to search the item on the map"), 0, wxLEFT | wxRIGHT | wxTOP, 8);
	hints_box->Add(createHintText(hints_box->GetStaticBox(), "Double right click to select item in the tileset"), 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);
	filter_scroll_sizer->Add(hints_box, 0, wxEXPAND | wxALL, 4);

	filter_scroll->SetSizer(filter_scroll_sizer);
	filter_scroll->FitInside();
	left_sizer->Add(filter_scroll, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

	left_panel->SetSizer(left_sizer);
	body_sizer->Add(left_panel, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 12);

	auto* right_panel = newd wxPanel(this, wxID_ANY);
	right_panel->SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	auto* right_sizer = newd wxBoxSizer(wxVERTICAL);
	auto* results_box = createStaticBox(right_panel, "Results");
	auto* results_box_sizer = newd wxBoxSizer(wxVERTICAL);

	results_notebook_ = newd wxNotebook(results_box->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);
	results_notebook_->SetBackgroundColour(Theme::Get(Theme::Role::Surface));

	list_results_view_ = newd AdvancedFinderResultsView(results_notebook_, wxID_ANY);
	list_results_view_->SetMode(AdvancedFinderResultViewMode::List);
	list_results_view_->SetMinSize(FromDIP(wxSize(520, 520)));
	results_notebook_->AddPage(list_results_view_, "List", false);

	grid_results_view_ = newd AdvancedFinderResultsView(results_notebook_, wxID_ANY);
	grid_results_view_->SetMode(AdvancedFinderResultViewMode::Grid);
	grid_results_view_->SetMinSize(FromDIP(wxSize(520, 520)));
	results_notebook_->AddPage(grid_results_view_, "Grid", true);

	results_box_sizer->Add(results_notebook_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
	results_box->Add(results_box_sizer, 1, wxEXPAND);
	right_sizer->Add(results_box, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 12);
	right_panel->SetSizer(right_sizer);
	body_sizer->Add(right_panel, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 12);
	root_sizer->Add(body_sizer, 1, wxEXPAND);

	auto* footer_panel = newd wxPanel(this, wxID_ANY);
	footer_panel->SetBackgroundColour(Theme::Get(Theme::Role::RaisedSurface));
	auto* footer_sizer = newd wxBoxSizer(wxVERTICAL);
	auto* footer_line = newd wxStaticLine(footer_panel, wxID_ANY);
	footer_sizer->Add(footer_line, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 0);
	auto* footer_buttons = newd wxBoxSizer(wxHORIZONTAL);
	footer_buttons->AddStretchSpacer(1);
	result_count_label_ = newd wxStaticText(footer_panel, wxID_ANY, "Results: 0 |");
	result_count_label_->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	result_count_label_->SetFont(Theme::GetFont(9, false));
	footer_buttons->Add(result_count_label_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	if (action_set_ == ActionSet::SearchAndSelect) {
		search_map_button_ = newd wxButton(footer_panel, wxID_FIND, "Search Map");
		footer_buttons->Add(search_map_button_, 0, wxRIGHT, 8);

		select_item_button_ = newd wxButton(footer_panel, wxID_OK, "Select Item");
		footer_buttons->Add(select_item_button_, 0, wxRIGHT, 8);
	} else {
		ok_button_ = newd wxButton(footer_panel, wxID_OK, "OK");
		footer_buttons->Add(ok_button_, 0, wxRIGHT, 8);
	}

	cancel_button_ = newd wxButton(footer_panel, wxID_CANCEL, "Cancel");
	footer_buttons->Add(cancel_button_, 0);
	footer_sizer->Add(footer_buttons, 0, wxEXPAND | wxALL, 10);
	footer_panel->SetSizer(footer_sizer);
	root_sizer->Add(footer_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

	SetSizer(root_sizer);
	root_sizer->SetSizeHints(this);
}

void FindItemDialog::bindEvents() {
	search_field_->Bind(wxEVT_TEXT, &FindItemDialog::OnTextChanged, this);
	search_field_->Bind(wxEVT_TEXT_ENTER, &FindItemDialog::OnTextEnter, this);
	Bind(wxEVT_KEY_DOWN, &FindItemDialog::OnKeyDown, this);
	Bind(wxEVT_TIMER, &FindItemDialog::OnDeferredSelectTimer, this, deferred_select_timer_.GetId());

	for (wxCheckBox* checkbox : type_checkboxes_) {
		checkbox->Bind(wxEVT_CHECKBOX, &FindItemDialog::OnFilterChanged, this);
	}
	for (wxCheckBox* checkbox : property_checkboxes_) {
		checkbox->Bind(wxEVT_CHECKBOX, &FindItemDialog::OnFilterChanged, this);
	}
	for (wxCheckBox* checkbox : interaction_checkboxes_) {
		checkbox->Bind(wxEVT_CHECKBOX, &FindItemDialog::OnFilterChanged, this);
	}
	for (wxCheckBox* checkbox : visual_checkboxes_) {
		checkbox->Bind(wxEVT_CHECKBOX, &FindItemDialog::OnFilterChanged, this);
	}

	if (results_notebook_ != nullptr) {
		results_notebook_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &FindItemDialog::OnResultPageChanged, this);
	}
	if (list_results_view_ != nullptr) {
		list_results_view_->Bind(wxEVT_LISTBOX, &FindItemDialog::OnResultSelection, this);
		list_results_view_->Bind(wxEVT_LISTBOX_DCLICK, &FindItemDialog::OnResultActivate, this);
		list_results_view_->Bind(EVT_ADVANCED_FINDER_RESULT_RIGHT_ACTIVATE, &FindItemDialog::OnResultRightActivate, this);
	}
	if (grid_results_view_ != nullptr) {
		grid_results_view_->Bind(wxEVT_LISTBOX, &FindItemDialog::OnResultSelection, this);
		grid_results_view_->Bind(wxEVT_LISTBOX_DCLICK, &FindItemDialog::OnResultActivate, this);
		grid_results_view_->Bind(EVT_ADVANCED_FINDER_RESULT_RIGHT_ACTIVATE, &FindItemDialog::OnResultRightActivate, this);
	}

	if (search_map_button_ != nullptr) {
		search_map_button_->Bind(wxEVT_BUTTON, &FindItemDialog::OnSearchMap, this);
	}
	if (select_item_button_ != nullptr) {
		select_item_button_->Bind(wxEVT_BUTTON, &FindItemDialog::OnSelectItem, this);
	}
	if (ok_button_ != nullptr) {
		ok_button_->Bind(wxEVT_BUTTON, &FindItemDialog::OnSelectItem, this);
	}
	if (reset_search_button_ != nullptr) {
		reset_search_button_->Bind(wxEVT_BUTTON, &FindItemDialog::OnResetSearch, this);
	}
	cancel_button_->Bind(wxEVT_BUTTON, &FindItemDialog::OnCancel, this);
}

void FindItemDialog::loadInitialState() {
	catalog_ = BuildAdvancedFinderCatalog(include_creatures_);
	query_ = {};
	current_selection_ = {};

	if (g_session_finder_state.has_value()) {
		persisted_state_ = g_session_finder_state->persisted;
		query_ = persisted_state_.query;
		current_selection_ = persisted_state_.selection;
		result_view_mode_ = g_session_finder_state->result_view_mode;
		if (persisted_state_.size.IsFullySpecified()) {
			SetSize(persisted_state_.size);
		}
		if (persisted_state_.position != wxDefaultPosition) {
			Move(persisted_state_.position);
		} else {
			Centre(wxBOTH);
		}
	} else if (persist_shared_state_) {
		persisted_state_ = LoadAdvancedFinderPersistedState();
		query_ = persisted_state_.query;
		current_selection_ = persisted_state_.selection;
		if (persisted_state_.size.IsFullySpecified()) {
			SetSize(persisted_state_.size);
		}
		if (persisted_state_.position != wxDefaultPosition) {
			Move(persisted_state_.position);
		} else {
			Centre(wxBOTH);
		}
	} else {
		SetSize(wxSize(1500, 900));
		Centre(wxBOTH);
	}

	if (only_pickupables_) {
		query_.interaction_mask |= advancedFinderBit(AdvancedFinderInteractionFilter::Pickupable);
	}

	applyQueryToControls();
	setResultViewMode(result_view_mode_);
	refreshResults();

	if (action_set_ == ActionSet::SearchAndSelect) {
		if (default_action_ == AdvancedFinderDefaultAction::SearchMap) {
			search_map_button_->SetDefault();
		} else {
			select_item_button_->SetDefault();
		}
	} else if (ok_button_ != nullptr) {
		ok_button_->SetDefault();
	}

	search_field_->SetFocus();
	search_field_->SetSelection(-1, -1);
}

void FindItemDialog::savePersistedState() {
	auto state = persisted_state_;
	state.query = query_;
	state.selection = current_selection_;
	state.position = GetPosition();
	state.size = GetSize();
	g_session_finder_state = SessionFinderState {
		.persisted = state,
		.result_view_mode = result_view_mode_,
	};

	if (!persist_shared_state_) {
		return;
	}

	SaveAdvancedFinderPersistedState(state);
}

void FindItemDialog::applyQueryToControls() const {
	search_field_->ChangeValue(wxstr(query_.text));

	for (const auto& [type_filter, _] : kTypeCheckboxes) {
		type_checkboxes_[toIndex(type_filter)]->SetValue((query_.type_mask & advancedFinderBit(type_filter)) != 0);
	}
	for (const auto& [property_filter, _] : kPropertyCheckboxes) {
		property_checkboxes_[toIndex(property_filter)]->SetValue((query_.property_mask & advancedFinderBit(property_filter)) != 0);
	}
	for (const auto& [interaction_filter, _] : kInteractionCheckboxes) {
		interaction_checkboxes_[toIndex(interaction_filter)]->SetValue((query_.interaction_mask & advancedFinderBit(interaction_filter)) != 0);
	}
	for (const auto& [visual_filter, _] : kVisualCheckboxes) {
		visual_checkboxes_[toIndex(visual_filter)]->SetValue((query_.visual_mask & advancedFinderBit(visual_filter)) != 0);
	}

	if (only_pickupables_) {
		auto* pickupable = interaction_checkboxes_[toIndex(AdvancedFinderInteractionFilter::Pickupable)];
		pickupable->SetValue(true);
		pickupable->Enable(false);
	}

	if (!include_creatures_) {
		auto* creature_checkbox = type_checkboxes_[toIndex(AdvancedFinderTypeFilter::Creature)];
		creature_checkbox->SetValue(false);
		creature_checkbox->Enable(false);
	}
}

void FindItemDialog::readQueryFromControls() {
	query_.text = nstr(search_field_->GetValue());

	query_.type_mask = 0;
	query_.property_mask = 0;
	query_.interaction_mask = 0;
	query_.visual_mask = 0;

	for (const auto& [type_filter, _] : kTypeCheckboxes) {
		if (type_checkboxes_[toIndex(type_filter)]->GetValue()) {
			query_.type_mask |= advancedFinderBit(type_filter);
		}
	}
	for (const auto& [property_filter, _] : kPropertyCheckboxes) {
		if (property_checkboxes_[toIndex(property_filter)]->GetValue()) {
			query_.property_mask |= advancedFinderBit(property_filter);
		}
	}
	for (const auto& [interaction_filter, _] : kInteractionCheckboxes) {
		if (interaction_checkboxes_[toIndex(interaction_filter)]->GetValue()) {
			query_.interaction_mask |= advancedFinderBit(interaction_filter);
		}
	}
	for (const auto& [visual_filter, _] : kVisualCheckboxes) {
		if (visual_checkboxes_[toIndex(visual_filter)]->GetValue()) {
			query_.visual_mask |= advancedFinderBit(visual_filter);
		}
	}

	if (only_pickupables_) {
		query_.interaction_mask |= advancedFinderBit(AdvancedFinderInteractionFilter::Pickupable);
	}
	if (!include_creatures_) {
		query_.type_mask &= ~advancedFinderBit(AdvancedFinderTypeFilter::Creature);
	}
}

void FindItemDialog::refreshResults() {
	readQueryFromControls();

	auto filtered_rows = FilterAdvancedFinderCatalog(catalog_, query_);
	if (filtered_rows.empty()) {
		list_results_view_->SetNoMatches("No results", "Try a different word or fewer filters.");
		grid_results_view_->SetNoMatches("No results", "Try a different word or fewer filters.");
		current_selection_ = {};
		updateResultTitle(0);
		updateButtons();
		return;
	}

	std::vector<const AdvancedFinderCatalogRow*> rows;
	rows.reserve(filtered_rows.size());
	for (const size_t filtered_index : filtered_rows) {
		rows.push_back(&catalog_[filtered_index]);
	}

	list_results_view_->SetRows(rows, current_selection_);
	grid_results_view_->SetRows(rows, current_selection_);

	if (auto* view = activeResultsView()) {
		if (const auto* row = view->GetSelectedRow()) {
			current_selection_ = MakeAdvancedFinderSelectionKey(*row);
		} else {
			current_selection_ = {};
		}
	} else {
		current_selection_ = {};
	}

	updateResultTitle(filtered_rows.size());
	updateButtons();
}

void FindItemDialog::updateButtons() {
	const bool has_selection = activeResultsView() != nullptr && activeResultsView()->GetSelectedRow() != nullptr;

	if (search_map_button_ != nullptr) {
		search_map_button_->Enable(has_selection);
	}
	if (select_item_button_ != nullptr) {
		select_item_button_->Enable(has_selection);
	}
	if (ok_button_ != nullptr) {
		ok_button_->Enable(has_selection);
	}
}

void FindItemDialog::updateResultTitle(size_t count) const {
	if (result_count_label_ != nullptr) {
		result_count_label_->SetLabel(wxString::Format("Results: %zu |", count));
		result_count_label_->SetMinSize(result_count_label_->GetBestSize());
		if (wxWindow* parent = result_count_label_->GetParent(); parent != nullptr) {
			parent->Layout();
		}
		const_cast<FindItemDialog*>(this)->Layout();
	}
}

void FindItemDialog::updateCurrentSelection() {
	if (const auto* view = activeResultsView(); view != nullptr && view->GetSelectedRow() != nullptr) {
		const auto* row = view->GetSelectedRow();
		current_selection_ = MakeAdvancedFinderSelectionKey(*row);
	} else {
		current_selection_ = {};
	}
}

void FindItemDialog::triggerDefaultAction() {
	if (action_set_ == ActionSet::SearchAndSelect) {
		handlePositiveAction(default_action_ == AdvancedFinderDefaultAction::SearchMap ? ResultAction::SearchMap : ResultAction::SelectItem);
	} else {
		handlePositiveAction(ResultAction::ConfirmSelection);
	}
}

void FindItemDialog::handlePositiveAction(ResultAction action) {
	if (deferred_select_timer_.IsRunning()) {
		deferred_select_timer_.Stop();
	}

	const AdvancedFinderCatalogRow* selected_row = activeResultsView() != nullptr ? activeResultsView()->GetSelectedRow() : nullptr;
	if (selected_row == nullptr) {
		return;
	}

	result_kind_ = selected_row->kind;
	result_brush_ = selected_row->brush;
	result_creature_brush_ = selected_row->creature_brush;
	result_id_ = selected_row->isItem() ? selected_row->server_id : 0;
	result_creature_name_ = selected_row->isCreature() ? selected_row->label : std::string {};
	result_action_ = action;
	updateCurrentSelection();

	EndModal(action == ResultAction::SearchMap ? wxID_FIND : wxID_OK);
}

void FindItemDialog::setResultViewMode(AdvancedFinderResultViewMode mode) {
	result_view_mode_ = mode;
	if (results_notebook_ != nullptr) {
		results_notebook_->ChangeSelection(toIndex(mode));
	}
}

AdvancedFinderResultsView* FindItemDialog::activeResultsView() const {
	return result_view_mode_ == AdvancedFinderResultViewMode::Grid ? grid_results_view_ : list_results_view_;
}

void FindItemDialog::OnFilterChanged(wxCommandEvent& WXUNUSED(event)) {
	refreshResults();
}

void FindItemDialog::OnTextChanged(wxCommandEvent& WXUNUSED(event)) {
	refreshResults();
}

void FindItemDialog::syncResultViews(AdvancedFinderResultsView* source_view) {
	if (source_view == nullptr) {
		return;
	}

	AdvancedFinderResultsView* target_view = source_view == list_results_view_ ? grid_results_view_ : list_results_view_;
	if (target_view != nullptr) {
		target_view->SetSelectionIndex(source_view->GetSelectionIndex());
	}

	if (const auto* row = source_view->GetSelectedRow()) {
		current_selection_ = MakeAdvancedFinderSelectionKey(*row);
	} else {
		current_selection_ = {};
	}

	updateButtons();
}

void FindItemDialog::OnResultPageChanged(wxBookCtrlEvent& event) {
	const auto page = event.GetSelection();
	if (page == wxNOT_FOUND) {
		event.Skip();
		return;
	}

	result_view_mode_ = page == 0 ? AdvancedFinderResultViewMode::List : AdvancedFinderResultViewMode::Grid;
	updateCurrentSelection();
	updateButtons();
	event.Skip();
}

void FindItemDialog::OnResultSelection(wxCommandEvent& event) {
	auto* source_view = dynamic_cast<AdvancedFinderResultsView*>(event.GetEventObject());
	if (source_view == nullptr) {
		return;
	}

	syncResultViews(source_view);
}

void FindItemDialog::OnResultActivate(wxCommandEvent& WXUNUSED(event)) {
	if (action_set_ == ActionSet::SearchAndSelect) {
		handlePositiveAction(ResultAction::SearchMap);
		return;
	}
	handlePositiveAction(ResultAction::ConfirmSelection);
}

void FindItemDialog::OnResultRightActivate(wxCommandEvent& WXUNUSED(event)) {
	if (action_set_ == ActionSet::SearchAndSelect) {
		if (deferred_select_timer_.IsRunning()) {
			deferred_select_timer_.Stop();
		}
		deferred_select_timer_.StartOnce(300);
		return;
	}
	handlePositiveAction(ResultAction::ConfirmSelection);
}

void FindItemDialog::OnSearchMap(wxCommandEvent& WXUNUSED(event)) {
	handlePositiveAction(ResultAction::SearchMap);
}

void FindItemDialog::OnSelectItem(wxCommandEvent& WXUNUSED(event)) {
	handlePositiveAction(action_set_ == ActionSet::SearchAndSelect ? ResultAction::SelectItem : ResultAction::ConfirmSelection);
}

void FindItemDialog::OnResetSearch(wxCommandEvent& WXUNUSED(event)) {
	search_field_->Clear();

	for (wxCheckBox* checkbox : type_checkboxes_) {
		if (checkbox->IsEnabled()) {
			checkbox->SetValue(false);
		}
	}
	for (wxCheckBox* checkbox : property_checkboxes_) {
		if (checkbox->IsEnabled()) {
			checkbox->SetValue(false);
		}
	}
	for (wxCheckBox* checkbox : interaction_checkboxes_) {
		if (checkbox->IsEnabled()) {
			checkbox->SetValue(false);
		}
	}
	for (wxCheckBox* checkbox : visual_checkboxes_) {
		if (checkbox->IsEnabled()) {
			checkbox->SetValue(false);
		}
	}

	refreshResults();
	search_field_->SetFocus();
}

void FindItemDialog::OnDeferredSelectTimer(wxTimerEvent& WXUNUSED(event)) {
	if (IsModal()) {
		handlePositiveAction(ResultAction::SelectItem);
	}
}

void FindItemDialog::OnCancel(wxCommandEvent& WXUNUSED(event)) {
	result_action_ = ResultAction::None;
	EndModal(wxID_CANCEL);
}

void FindItemDialog::OnTextEnter(wxCommandEvent& WXUNUSED(event)) {
	triggerDefaultAction();
}

void FindItemDialog::OnKeyDown(wxKeyEvent& event) {
	if (event.GetEventObject() != search_field_) {
		event.Skip();
		return;
	}

	if (auto* view = activeResultsView(); view != nullptr) {
		wxKeyEvent forwarded(event);
		forwarded.SetEventObject(view);
		if (view->GetEventHandler()->ProcessEvent(forwarded)) {
			return;
		}
	}

	event.Skip();
}
