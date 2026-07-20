#include "ui/replace_tool/rule_builder_panel.h"
#include "ui/theme.h"
#include "ui/gui.h"

#include "app/managers/version_manager.h"
#include "util/nvg_utils.h"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/msgdlg.h>
#include <wx/dialog.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/sizer.h>
#include <nanovg.h>
#include <format>
#include <cmath>
#include "rendering/core/text_renderer.h"
#include "ui/replace_tool/rule_card_renderer.h"
#include "ui/replace_tool/brush_picker_dialog.h"
#include "ui/replace_tool/brush_swap_dialog.h"
#include "ui/replace_tool/brush_mapping_service.h"
#include "brushes/brush.h"
#include <ranges>
#include <algorithm>

// Layout Constants
static const int CARD_PADDING = 20;
static const int CARD_MARGIN_X = 10;
static const int CARD_MARGIN_Y = 10;
static const int HEADER_HEIGHT = 40;
static const int ITEM_SIZE = 56;
static const int ITEM_H = 110;
static const int ITEM_SPACING = 10;
static const int ARROW_WIDTH = 60;
static const int SECTION_GAP = 20;
static const int CARD_W = ITEM_SIZE + 20;
static const int GHOST_SLOT_WIDTH = CARD_W;

// True when the rule must not show the trailing [+] ghost slot: either a REMOVE
// target is present, or it is a brush rule (which takes exactly one target).
// Shared by the hit-test, the layout and the renderer so they never disagree.
bool ReplaceToolSuppressAddSlot(const ReplacementRule& rule) {
	if (rule.isBrushRule() && !rule.targets.empty()) {
		return true;
	}
	return std::ranges::any_of(rule.targets, [](const auto& t) {
		return t.kind == SlotKind::Item && t.id == TRASH_ITEM_ID;
	});
}

// Small modal asking for the per-rule X/Y offset. Returns true on OK.
static bool ShowOffsetDialog(wxWindow* parent, int& ioX, int& ioY) {
	wxDialog dlg(parent, wxID_ANY, "Replacement Offset");

	wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);
	wxStaticText* info = new wxStaticText(&dlg, wxID_ANY,
		"Offset (in tiles) applied to the replacement item,\nrelative to the original. 0, 0 keeps it on the same tile.");
	root->Add(info, 0, wxALL, 10);

	wxFlexGridSizer* grid = new wxFlexGridSizer(2, 2, 6, 8);
	grid->Add(new wxStaticText(&dlg, wxID_ANY, "Offset X:"), 0, wxALIGN_CENTER_VERTICAL);
	wxSpinCtrl* spinX = new wxSpinCtrl(&dlg, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, -1000, 1000, ioX);
	grid->Add(spinX, 0, wxEXPAND);
	grid->Add(new wxStaticText(&dlg, wxID_ANY, "Offset Y:"), 0, wxALIGN_CENTER_VERTICAL);
	wxSpinCtrl* spinY = new wxSpinCtrl(&dlg, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, -1000, 1000, ioY);
	grid->Add(spinY, 0, wxEXPAND);
	root->Add(grid, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);

	root->Add(dlg.CreateButtonSizer(wxOK | wxCANCEL), 0, wxALL | wxEXPAND, 10);
	dlg.SetSizerAndFit(root);
	dlg.CenterOnParent();

	if (dlg.ShowModal() != wxID_OK) {
		return false;
	}
	ioX = spinX->GetValue();
	ioY = spinY->GetValue();
	return true;
}

// ----------------------------------------------------------------------------
// ItemDropTarget
// ----------------------------------------------------------------------------

RuleBuilderPanel::ItemDropTarget::ItemDropTarget(RuleBuilderPanel* panel) : m_panel(panel) { }

bool RuleBuilderPanel::ItemDropTarget::OnDropText(wxCoord x, wxCoord y, const wxString& data) {
	m_panel->m_isExternalDrag = false;
	if (!data.StartsWith("RME_ITEM:")) {
		return false;
	}

	unsigned long idVal;
	if (!data.AfterFirst(':').ToULong(&idVal) || idVal > 0xFFFF) {
		return false;
	}
	uint16_t itemId = (uint16_t)idVal;

	RuleBuilderPanel::HitResult hit = m_panel->HitTest(x, y);

	// Slots in brush mode (and their kind badges) do not accept item drops.
	if (hit.type == RuleBuilderPanel::HitResult::PickBrush
		|| hit.type == RuleBuilderPanel::HitResult::ToggleSourceKind
		|| hit.type == RuleBuilderPanel::HitResult::ToggleTargetKind) {
		return false;
	}

	if (hit.type == RuleBuilderPanel::HitResult::Source) {
		if (hit.ruleIndex >= 0 && hit.ruleIndex < (int)m_panel->m_rules.size()) {
			// Reject item drops onto a slot in brush mode: writing fromId there
			// would be silently ignored by the engine.
			if (m_panel->m_rules[hit.ruleIndex].fromKind == SlotKind::Brush) {
				return false;
			}
			m_panel->m_rules[hit.ruleIndex].fromId = itemId;
			m_panel->m_listener->OnRuleChanged();
			m_panel->Refresh();
			return true;
		}
	} else if (hit.type == RuleBuilderPanel::HitResult::Target || hit.type == RuleBuilderPanel::HitResult::DeleteTarget) {
		if (hit.ruleIndex >= 0 && hit.ruleIndex < (int)m_panel->m_rules.size()) {
			// Replace existing target
			if (hit.targetIndex >= 0 && hit.targetIndex < m_panel->m_rules[hit.ruleIndex].targets.size()) {
				if (m_panel->m_rules[hit.ruleIndex].targets[hit.targetIndex].kind == SlotKind::Brush) {
					return false;
				}
				m_panel->m_rules[hit.ruleIndex].targets[hit.targetIndex].id = itemId;
				m_panel->m_listener->OnRuleChanged();
				m_panel->Refresh();
				return true;
			}
		}
	} else if (hit.type == RuleBuilderPanel::HitResult::AddTarget) { // Drag to [+] slot
		if (hit.ruleIndex >= 0 && hit.ruleIndex < (int)m_panel->m_rules.size()) {
			// TRASH LOGIC: Reject if has trash
			if (ReplaceToolSuppressAddSlot(m_panel->m_rules[hit.ruleIndex])) {
				return false;
			}

			ReplacementTarget nt;
			nt.id = itemId;
			m_panel->m_rules[hit.ruleIndex].targets.push_back(nt);
			m_panel->DistributeProbabilities(hit.ruleIndex);
			m_panel->m_listener->OnRuleChanged();
			m_panel->LayoutRules();
			m_panel->Refresh();
			return true;
		}
	} else if (hit.type == RuleBuilderPanel::HitResult::NewRule) {
		// Create a new rule with this item as Source
		ReplacementRule newRule;
		newRule.fromId = itemId;
		// Initialize with empty targets (User needs to add them) or maybe auto-add the same item?
		// User said "Drop New Source Here", so empty targets is fine.
		m_panel->m_rules.push_back(newRule);
		m_panel->m_listener->OnRuleChanged();
		m_panel->LayoutRules();
		m_panel->Refresh();
		return true;
	}

	m_panel->m_isExternalDrag = false;
	return false;
}

wxDragResult RuleBuilderPanel::ItemDropTarget::OnDragOver(wxCoord x, wxCoord y, wxDragResult def) {
	m_panel->m_isExternalDrag = true;
	RuleBuilderPanel::HitResult hit = m_panel->HitTest(x, y);
	m_panel->m_dragHover = hit;
	m_panel->Refresh();
	return def;
}

void RuleBuilderPanel::ItemDropTarget::OnLeave() {
	m_panel->m_isExternalDrag = false;
	m_panel->m_dragHover = { RuleBuilderPanel::HitResult::None, -1, -1 };
	m_panel->Refresh();
}

// ----------------------------------------------------------------------------
// RuleBuilderPanel
// ----------------------------------------------------------------------------

RuleBuilderPanel::RuleBuilderPanel(wxWindow* parent, Listener* listener) :
	NanoVGCanvas(parent, wxID_ANY, wxVSCROLL | wxWANTS_CHARS),
	m_listener(listener) {

	SetDropTarget(new ItemDropTarget(this));

	Bind(wxEVT_SIZE, &RuleBuilderPanel::OnSize, this);
	Bind(wxEVT_LEFT_DOWN, &RuleBuilderPanel::OnMouse, this);
	Bind(wxEVT_LEFT_DCLICK, &RuleBuilderPanel::OnMouse, this);
	Bind(wxEVT_MOTION, &RuleBuilderPanel::OnMouse, this);
	Bind(wxEVT_LEAVE_WINDOW, &RuleBuilderPanel::OnMouse, this);

	m_dragHover = { HitResult::None, -1, -1 };
}

RuleBuilderPanel::~RuleBuilderPanel() { }

void RuleBuilderPanel::Clear() {
	m_rules.clear();
	LayoutRules();
	Refresh();
}

void RuleBuilderPanel::SetRules(const std::vector<ReplacementRule>& rules) {
	m_rules = rules;
	LayoutRules();
	Refresh();
}

std::vector<ReplacementRule> RuleBuilderPanel::GetRules() const {
	return m_rules;
}

void RuleBuilderPanel::ApplyPickedBrush(int ruleIndex, int targetIndex, const std::string& brushName) {
	// The picker is modeless, so the panel stayed live while it was open: the
	// rule may have been deleted, or toggled back to item mode. Re-validate
	// instead of trusting the indices captured when the picker opened.
	if (ruleIndex < 0 || ruleIndex >= (int)m_rules.size()) {
		return;
	}
	auto& rule = m_rules[ruleIndex];

	if (targetIndex < 0) {
		if (rule.fromKind != SlotKind::Brush) {
			return;
		}
		rule.fromBrushName = brushName;
	} else {
		if (targetIndex >= (int)rule.targets.size()) {
			return;
		}
		auto& target = rule.targets[targetIndex];
		if (target.kind != SlotKind::Brush) {
			return;
		}
		target.brushName = brushName;
	}

	if (m_listener) {
		m_listener->OnRuleChanged();
	}
	LayoutRules();
	Refresh();
}

void RuleBuilderPanel::AddRules(const std::vector<ReplacementRule>& rules) {
	if (rules.empty()) {
		return;
	}

	for (const auto& incoming : rules) {
		// ReplacementEngine keys item rules by fromId, so a second rule for the
		// same source would silently shadow the first. Overwrite instead.
		auto existing = std::find_if(m_rules.begin(), m_rules.end(), [&incoming](const ReplacementRule& rule) {
			return !rule.isBrushRule() && rule.fromId == incoming.fromId;
		});
		if (existing != m_rules.end()) {
			*existing = incoming;
		} else {
			m_rules.push_back(incoming);
		}
	}

	// A trailing blank rule (the auto-add placeholder) would sit in the middle
	// of the list after appending, so move it back to the end.
	auto blank = std::find_if(m_rules.begin(), m_rules.end(), [](const ReplacementRule& rule) {
		return !rule.hasSource() && rule.targets.empty();
	});
	if (blank != m_rules.end() && blank + 1 != m_rules.end()) {
		std::rotate(blank, blank + 1, m_rules.end());
	}

	if (m_listener) {
		m_listener->OnRuleChanged();
	}
	LayoutRules();
	Refresh();
}

void RuleBuilderPanel::ApplyItemAsSource(uint16_t itemId) {
	// If the last rule has no targets and no source, reuse it; otherwise create a
	// new rule. Rules already in brush mode are never recycled, so dropping an
	// item never contaminates a brush rule.
	if (!m_rules.empty() && !m_rules.back().isBrushRule() && m_rules.back().fromId == 0) {
		m_rules.back().fromId = itemId;
	} else {
		ReplacementRule newRule;
		newRule.fromId = itemId;
		m_rules.push_back(newRule);
	}
	if (m_listener) {
		m_listener->OnRuleChanged();
	}
	LayoutRules();
	Refresh();
}

void RuleBuilderPanel::ApplyItemAsTarget(uint16_t itemId) {
	// Brush rules are single-target and their target is a brush, so an incoming
	// item always starts a fresh item rule instead of joining them.
	if (m_rules.empty() || m_rules.back().isBrushRule()) {
		// Create a new rule with empty source
		ReplacementRule newRule;
		ReplacementTarget t;
		t.id = itemId;
		t.probability = 100;
		newRule.targets.push_back(t);
		m_rules.push_back(newRule);
	} else {
		// Add to the last rule
		auto& rule = m_rules.back();
		// Don't add if it already has a trash target
		if (ReplaceToolSuppressAddSlot(rule)) {
			return;
		}
		ReplacementTarget t;
		t.id = itemId;
		rule.targets.push_back(t);
		DistributeProbabilities(static_cast<int>(m_rules.size()) - 1);
	}
	if (m_listener) {
		m_listener->OnRuleChanged();
	}
	LayoutRules();
	Refresh();
}

// ----------------------------------------------------------------------------
// Layout logic using cache
// ----------------------------------------------------------------------------

int RuleBuilderPanel::GetRuleHeight(int index, int width) const {
	if (index < 0 || index >= (int)m_rules.size()) {
		return FromDIP(ITEM_H);
	}

	const float TARGET_START_X = CARD_PADDING + CARD_W + 10 + ARROW_WIDTH;
	float availableWidth = width - CARD_MARGIN_X * 2 - TARGET_START_X - CARD_PADDING;
	if (availableWidth < CARD_W) {
		availableWidth = CARD_W;
	}

	int columns = std::max(1, (int)(availableWidth / (CARD_W + ITEM_SPACING)));
	int targetCount = m_rules[index].targets.size();

	if (!ReplaceToolSuppressAddSlot(m_rules[index])) {
		targetCount++; // [+] slot
	}

	int rows = std::max(1, (targetCount + columns - 1) / columns);
	int itemH = FromDIP(ITEM_H);
	return rows * (itemH + ITEM_SPACING) + CARD_PADDING * 2;
}

int RuleBuilderPanel::GetRuleY(int index, int width) const {
	if (index < 0 || index >= (int)m_ruleYCache.size()) {
		if (index == (int)m_ruleYCache.size()) {
			return m_totalHeight;
		}
		return 0;
	}
	return m_ruleYCache[index];
}

void RuleBuilderPanel::LayoutRules() {
	int width = GetClientSize().x;
	if (width <= 0) {
		width = 800; // Fallback
	}

	m_ruleYCache.clear();
	int currentY = RuleCardRenderer::CARD_MARGIN_Y + RuleCardRenderer::HEADER_HEIGHT;

	for (size_t i = 0; i < m_rules.size(); ++i) {
		m_ruleYCache.push_back(currentY);
		currentY += GetRuleHeight((int)i, width);
	}
	m_totalHeight = currentY;

	int footerHeight = RuleCardRenderer::ITEM_H + (RuleCardRenderer::CARD_MARGIN_Y * 2);
	UpdateScrollbar(m_totalHeight + footerHeight);
}

void RuleBuilderPanel::DistributeProbabilities(int ruleIndex) {
	if (ruleIndex < 0 || ruleIndex >= m_rules.size()) {
		return;
	}
	auto& targets = m_rules[ruleIndex].targets;

	if (targets.empty()) {
		return;
	}
	int count = targets.size();
	if (count <= 0) {
		return;
	}
	// Use floating point accumulation for fairer distribution of remainders
	double step = 100.0 / count;
	double accumulated = 0.0;

	for (auto& target : targets) {
		accumulated += step;
		int currentTotal = (int)std::round(accumulated);
		int prevTotal = (int)std::round(accumulated - step);
		target.probability = currentTotal - prevTotal;
	}
}

wxSize RuleBuilderPanel::DoGetBestClientSize() const {
	return wxSize(FromDIP(500), FromDIP(400));
}

void RuleBuilderPanel::OnSize(wxSizeEvent& event) {
	// Guard against redundant size events to prevent potential loop/jitter
	if (GetClientSize() != m_lastSize) {
		m_lastSize = GetClientSize();
		LayoutRules();
		Refresh();
	}
	event.Skip();
}

void RuleBuilderPanel::OnMouse(wxMouseEvent& event) {
	if (event.LeftDown()) {
		HitResult hit = HitTest(event.GetX(), event.GetY());
		if (hit.type == HitResult::DeleteRule && hit.ruleIndex != -1) {
			m_rules.erase(m_rules.begin() + hit.ruleIndex);
			if (m_listener) {
				m_listener->OnRuleChanged();
			}
			LayoutRules();
			Refresh();
		} else if (hit.type == HitResult::DeleteTarget && hit.ruleIndex != -1 && hit.targetIndex != -1) {
			m_rules[hit.ruleIndex].targets.erase(m_rules[hit.ruleIndex].targets.begin() + hit.targetIndex);
			DistributeProbabilities(hit.ruleIndex);
			if (m_listener) {
				m_listener->OnRuleChanged();
			}
			LayoutRules();
			Refresh();
		} else if (hit.type == HitResult::AddTarget && hit.ruleIndex != -1) {
			if (m_rules[hit.ruleIndex].isBrushRule() && !m_rules[hit.ruleIndex].targets.empty()) {
				// Brush rules are single-target; ignore.
			} else if (m_rules[hit.ruleIndex].targets.empty()) {
				ReplacementTarget t;
				t.id = TRASH_ITEM_ID;
				t.probability = 100;
				m_rules[hit.ruleIndex].targets.push_back(t);
				if (m_listener) {
					m_listener->OnRuleChanged();
				}
				Refresh();
			}
		} else if (hit.type == HitResult::EditOffset && hit.ruleIndex != -1) {
			auto& rule = m_rules[hit.ruleIndex];
			int ox = rule.offsetX;
			int oy = rule.offsetY;
			if (ShowOffsetDialog(this, ox, oy)) {
				rule.offsetX = ox;
				rule.offsetY = oy;
				if (m_listener) {
					m_listener->OnRuleChanged();
				}
				Refresh();
			}
		} else if (hit.type == HitResult::ToggleSourceKind && hit.ruleIndex != -1) {
			auto& rule = m_rules[hit.ruleIndex];
			if (rule.fromKind == SlotKind::Brush) {
				rule.fromKind = SlotKind::Item;
				rule.fromBrushName.clear();
			} else {
				rule.fromKind = SlotKind::Brush;
				rule.fromId = 0;
				// A brush rule takes a single target; drop any extras.
				if (rule.targets.size() > 1) {
					rule.targets.resize(1);
				}
				DistributeProbabilities(hit.ruleIndex);
				// Pick right away: an empty brush slot is not useful on its own.
				// The picker is modeless, so the slot flips to BRUSH now and the
				// name lands later, through the callback.
				BrushPickerDialog::PickBrush(this, rule.fromBrushName, nullptr, [this, ruleIndex = hit.ruleIndex](const BrushMappingService::Selection& picked) {
					ApplyPickedBrush(ruleIndex, -1, picked.brushName);
				});
			}
			if (m_listener) {
				m_listener->OnRuleChanged();
			}
			LayoutRules();
			Refresh();
		} else if (hit.type == HitResult::ToggleTargetKind && hit.ruleIndex != -1 && hit.targetIndex != -1) {
			auto& rule = m_rules[hit.ruleIndex];
			auto& target = rule.targets[hit.targetIndex];
			if (target.kind == SlotKind::Brush) {
				target.kind = SlotKind::Item;
				target.brushName.clear();
			} else {
				target.kind = SlotKind::Brush;
				target.id = 0;
				const Brush* familyFilter = rule.isBrushRule() ? BrushMappingService::FindBrush(rule.fromBrushName) : nullptr;
				BrushPickerDialog::PickBrush(this, target.brushName, familyFilter, [this, ruleIndex = hit.ruleIndex, targetIndex = hit.targetIndex](const BrushMappingService::Selection& picked) {
					ApplyPickedBrush(ruleIndex, targetIndex, picked.brushName);
				});
			}
			if (m_listener) {
				m_listener->OnRuleChanged();
			}
			LayoutRules();
			Refresh();
		} else if (hit.type == HitResult::PickBrush && hit.ruleIndex != -1) {
			auto& rule = m_rules[hit.ruleIndex];
			if (hit.targetIndex < 0) {
				BrushPickerDialog::PickBrush(this, rule.fromBrushName, nullptr, [this, ruleIndex = hit.ruleIndex](const BrushMappingService::Selection& picked) {
					ApplyPickedBrush(ruleIndex, -1, picked.brushName);
				});
			} else if (hit.targetIndex < (int)rule.targets.size()) {
				// On a target, restrict the list to the source brush family.
				const Brush* familyFilter = rule.isBrushRule() ? BrushMappingService::FindBrush(rule.fromBrushName) : nullptr;
				BrushPickerDialog::PickBrush(this, rule.targets[hit.targetIndex].brushName, familyFilter, [this, ruleIndex = hit.ruleIndex, targetIndex = hit.targetIndex](const BrushMappingService::Selection& picked) {
					ApplyPickedBrush(ruleIndex, targetIndex, picked.brushName);
				});
			}
		} else if (hit.type == HitResult::SwapBrush) {
			BrushSwapDialog::Open(this, [this](const std::vector<ReplacementRule>& rules) {
				AddRules(rules);
			});
		} else if (hit.type == HitResult::Source && hit.ruleIndex != -1) {
			if (m_listener) {
				m_listener->OnRuleItemSelected(m_rules[hit.ruleIndex].fromId);
			}
		} else if (hit.type == HitResult::ClearRules) {
			wxMessageDialog dlg(this, "Are you sure you want to clear all rules? This action cannot be undone.", "Clear Rules", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
			if (dlg.ShowModal() == wxID_YES) {
				Clear();
				if (m_listener) {
					m_listener->OnClearRules();
				}
			}
			if (m_listener) {
				m_listener->OnSaveRule();
			}
		}
	}

	if (event.Moving()) {
		HitResult hit = HitTest(event.GetX(), event.GetY());
		if (hit.type != m_dragHover.type || hit.ruleIndex != m_dragHover.ruleIndex || hit.targetIndex != m_dragHover.targetIndex) {
			m_dragHover = hit;
			Refresh();
		}
	}
	event.Skip();
}

RuleBuilderPanel::HitResult RuleBuilderPanel::HitTest(int x, int y) const {
	int scrollPos = GetScrollPosition();
	int absY = y + scrollPos;
	int width = GetClientSize().x;

	const float ITEM_HEIGHT = FromDIP(ITEM_H);

	// Header blocked area
	if (y < HEADER_HEIGHT) {
		const int BTN_W = FromDIP(80);
		const int GAP = FromDIP(10);

		// Clear Button (Far right)
		if (x > width - BTN_W - GAP) {
			return { HitResult::ClearRules, -1, -1 };
		}
		// Save Button (Left of Clear)
		if (x > width - (BTN_W * 2) - (GAP * 2) && x < width - BTN_W - GAP) {
			return { HitResult::SaveRule, -1, -1 };
		}
		// Otherwise, we are clicking the header background/labels - consume the hit so we don't click rules underneath
		return { HitResult::None, -1, -1 };
	}

	// Check Rules
	for (size_t i = 0; i < m_rules.size(); ++i) {
		int ruleH = GetRuleHeight(i, width);
		int ruleY = GetRuleY(i, width);
		wxRect card(CARD_MARGIN_X, ruleY, width - CARD_MARGIN_X * 2, ruleH);

		if (card.Contains(x, absY)) {
			int localX = x - card.x;
			int localY = absY - card.y;

			// Delete Rule Button (Top Right)
			if (localX > card.width - 24 && localY < 24) {
				return { HitResult::DeleteRule, (int)i, -1 };
			}

			// Source Item (Left, always in first row logically)
			float startX = CARD_PADDING;
			float sourceY = CARD_PADDING; // Vertical top in card

			if (localX >= startX && localX <= startX + CARD_W && localY >= sourceY && localY <= sourceY + ITEM_HEIGHT) {
				// Bottom strip of the slot is the ITEM/BRUSH kind toggle.
				if (localY >= sourceY + ITEM_HEIGHT - RuleCardRenderer::KIND_BADGE_H) {
					return { HitResult::ToggleSourceKind, (int)i, -1 };
				}
				if (m_rules[i].fromKind == SlotKind::Brush) {
					return { HitResult::PickBrush, (int)i, -1 };
				}
				return { HitResult::Source, (int)i, -1 };
			}

			// Offset badge (above the arrow). Geometry mirrors RuleCardRenderer,
			// using raw constants so it matches the badge on any DPI.
			float badgeX = startX + CARD_W + 10;
			float badgeY = CARD_PADDING + 35.0f;
			float badgeH = 18.0f;
			if (localX >= badgeX && localX <= badgeX + ARROW_WIDTH && localY >= badgeY && localY <= badgeY + badgeH) {
				return { HitResult::EditOffset, (int)i, -1 };
			}

			// Targets (Wrapping)
			float targetStartX = startX + CARD_W + 10 + ARROW_WIDTH;
			float availableWidth = card.width - targetStartX - CARD_PADDING;
			int columns = std::max(1, (int)(availableWidth / (CARD_W + ITEM_SPACING)));

			for (size_t t = 0; t < m_rules[i].targets.size(); ++t) {
				int row = t / columns;
				int col = t % columns;
				float tx = targetStartX + col * (RuleCardRenderer::CARD_W + RuleCardRenderer::ITEM_SPACING);
				float ty = RuleCardRenderer::CARD_PADDING + row * (ITEM_HEIGHT + RuleCardRenderer::ITEM_SPACING);

				if (localX >= tx && localX <= tx + RuleCardRenderer::CARD_W && localY >= ty && localY <= ty + ITEM_HEIGHT) {
					if (localY >= ty + ITEM_HEIGHT - RuleCardRenderer::KIND_BADGE_H) {
						return { HitResult::ToggleTargetKind, (int)i, (int)t };
					}
					if (m_rules[i].targets[t].kind == SlotKind::Brush) {
						return { HitResult::PickBrush, (int)i, (int)t };
					}
					return { HitResult::DeleteTarget, (int)i, (int)t };
				}
			}

			// Add Target Slot
			bool hasTrash = ReplaceToolSuppressAddSlot(m_rules[i]);

			if (!hasTrash) {
				int tIdx = m_rules[i].targets.size();
				int row = tIdx / columns;
				int col = tIdx % columns;
				float tx = targetStartX + col * (RuleCardRenderer::CARD_W + RuleCardRenderer::ITEM_SPACING);
				float ty = RuleCardRenderer::CARD_PADDING + row * (ITEM_HEIGHT + RuleCardRenderer::ITEM_SPACING);

				if (localX >= tx && localX <= tx + RuleCardRenderer::CARD_W && localY >= ty && localY <= ty + ITEM_HEIGHT) {
					return { HitResult::AddTarget, (int)i, -1 };
				}
			}

			return { HitResult::None, (int)i, -1 };
		}
	}

	// Clear Rules Button (Far Right of Header)
	// Handled in block above

	// New Rule Area (At the bottom)
	int newRuleY = GetRuleY(m_rules.size(), width) + RuleCardRenderer::CARD_MARGIN_Y;
	float dropH = (float)RuleCardRenderer::NEW_RULE_H;
	if (absY >= newRuleY && absY <= newRuleY + dropH) {
		float bx, by, bw, bh;
		RuleCardRenderer::GetSwapButtonRect((float)width, (float)newRuleY, bx, by, bw, bh);
		if (x >= bx && x <= bx + bw && absY >= by && absY <= by + bh) {
			return { HitResult::SwapBrush, -1, -1 };
		}
		return { HitResult::NewRule, -1, -1 };
	}

	return { HitResult::None, -1, -1 };
}

// Methods removed, using RuleCardRenderer instead

void RuleBuilderPanel::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
	int scrollPos = GetScrollPosition();

	// Note: NanoVGCanvas already applies a convert translation of nvgTranslate(0, -scrollPos).
	// To draw fixed elements (Background, Header), we must undo this translation.

	// 1. Draw Fixed Elements (Background + Header)
	nvgSave(vg);
	nvgTranslate(vg, 0, (float)scrollPos); // Undo base scroll

	// Full screen BG (Fixed)
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, width, height);
	wxColour bg = Theme::Get(Theme::Role::Background);
	nvgFillColor(vg, nvgRGBA(bg.Red(), bg.Green(), bg.Blue(), 255));
	nvgFill(vg);

	RuleCardRenderer::DrawHeader(vg, width);
	RuleCardRenderer::DrawClearButton(vg, width, m_dragHover.type == HitResult::ClearRules);
	RuleCardRenderer::DrawSaveButton(vg, width, m_dragHover.type == HitResult::SaveRule);
	nvgRestore(vg);

	// 2. Draw Content (Implicitly scrolled by base class)
	for (size_t i = 0; i < m_rules.size(); ++i) {
		int ruleY = GetRuleY(i, width);
		bool hoverDel = (m_dragHover.type == HitResult::DeleteRule && m_dragHover.ruleIndex == (int)i);
		int dragType = (m_dragHover.ruleIndex == (int)i) ? (int)m_dragHover.type : 0;
		int dragIdx = (m_dragHover.ruleIndex == (int)i) ? m_dragHover.targetIndex : -1;

		RuleCardRenderer::DrawRuleCard(this, vg, (int)i, ruleY, width, hoverDel, dragIdx, dragType, m_isExternalDrag);
	}

	int newRuleY = GetRuleY(m_rules.size(), width) + RuleCardRenderer::CARD_MARGIN_Y;
	RuleCardRenderer::DrawNewRuleArea(vg, width, newRuleY, m_dragHover.type == HitResult::NewRule, m_dragHover.type == HitResult::SwapBrush);
}

// ----------------------------------------------------------------------------
// Removed all private Draw methods (now in RuleCardRenderer)
// ----------------------------------------------------------------------------
