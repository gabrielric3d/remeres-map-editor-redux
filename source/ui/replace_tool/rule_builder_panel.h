#ifndef RME_RULE_BUILDER_PANEL_H_
#define RME_RULE_BUILDER_PANEL_H_

#include "ui/replace_tool/rule_manager.h"
#include "util/nanovg_canvas.h"
#include <wx/wx.h>
#include <wx/control.h>
#include <wx/dnd.h>
#include <vector>
#include <string>
#include <cstdint>

// True when the rule must not show the trailing [+] ghost slot: either a REMOVE
// target is present, or it is a brush rule (which takes exactly one target).
// Shared by the hit-test, the layout and the renderer so they never disagree.
bool ReplaceToolSuppressAddSlot(const ReplacementRule& rule);

class RuleBuilderPanel : public NanoVGCanvas {
public:
	class Listener {
	public:
		virtual ~Listener() = default;
		virtual void OnRuleChanged() = 0;
		virtual void OnClearRules() = 0;
		virtual void OnSaveRule() = 0;
		virtual void OnRuleItemSelected(uint16_t itemId) = 0;
	};

	// Hit testing results (Public for renderer access)
	struct HitResult {
		enum Type {
			None,
			Source, // The source item icon
			Target, // A specific target item icon
			AddTarget, // The [+] ghost slot at the end of targets
			NewRule, // The large "Drop New Rule" area
			ClearRules,
			SaveRule,
			DeleteRule, // The 'X' on the rule card
			DeleteTarget, // The 'X' overlay on a specific target
			EditOffset, // The offset badge above the arrow
			// NOTE: new values must be appended AT THE END. RuleCardRenderer
			// compares hover types against these enumerators, and inserting in
			// the middle would silently shift the existing meanings.
			ToggleSourceKind, // ITEM/BRUSH badge on the source slot
			ToggleTargetKind, // ITEM/BRUSH badge on a target slot
			PickBrush, // Body of a slot already in brush mode
			SwapBrush, // "Swap Brush..." button inside the new-rule area
		};
		Type type = None;
		int ruleIndex = -1;
		int targetIndex = -1;
	};

	RuleBuilderPanel(wxWindow* parent, Listener* listener);
	virtual ~RuleBuilderPanel();
	HitResult HitTest(int x, int y) const;
	void DistributeProbabilities(int ruleIndex);

	// Drop Target
	class ItemDropTarget : public wxTextDropTarget {
	public:
		ItemDropTarget(RuleBuilderPanel* panel);
		bool OnDropText(wxCoord x, wxCoord y, const wxString& data) override;
		wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def) override;
		void OnLeave() override;

	private:
		RuleBuilderPanel* m_panel;
	};

	void Clear();
	void SetRules(const std::vector<ReplacementRule>& rules);
	std::vector<ReplacementRule> GetRules() const;
	// Appends rules produced elsewhere (the Swap Brush dialog), replacing any
	// existing rule with the same source so the engine's fromId map stays sane.
	void AddRules(const std::vector<ReplacementRule>& rules);

	// External item application
	void ApplyItemAsSource(uint16_t itemId);
	void ApplyItemAsTarget(uint16_t itemId);

	// Writes a brush name picked in the (modeless) picker back into a slot.
	// targetIndex < 0 addresses the source slot. Indices are re-validated: the
	// panel stays interactive while the picker is open, so the rule may have
	// been deleted or switched back to item mode meanwhile.
	void ApplyPickedBrush(int ruleIndex, int targetIndex, const std::string& brushName);

	// Visual Layout
	int GetRuleHeight(int index, int width) const;
	int GetRuleY(int index, int width) const;
	void LayoutRules();

protected:
	virtual void OnNanoVGPaint(NVGcontext* vg, int width, int height) override;
	virtual wxSize DoGetBestClientSize() const override;

	void OnSize(wxSizeEvent& event);
	void OnMouse(wxMouseEvent& event);

private:
	std::vector<ReplacementRule> m_rules;
	wxSize m_lastSize;
	Listener* m_listener;

	// Drag feedback
	HitResult m_dragHover;
	bool m_isExternalDrag = false;

	// Layout Cache
	mutable std::vector<int> m_ruleYCache;
	mutable int m_totalHeight = 0;
};

#endif
