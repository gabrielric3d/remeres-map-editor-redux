#ifndef RME_RULE_MANAGER_H_
#define RME_RULE_MANAGER_H_

#include <vector>
#include <string>
#include <cstdint>

static const uint16_t TRASH_ITEM_ID = 0xFFFF;

// A rule slot either matches/produces a single item id, or a whole brush whose
// items are mapped by role (ground center, border direction, wall segment,
// carpet alignment). Persisted as an int so old JSON (missing the field)
// defaults to Item and behaves exactly like before.
enum class SlotKind : uint8_t {
	Item = 0,
	Brush = 1
};

struct ReplacementTarget {
	uint16_t id = 0;
	int probability = 0; // 0-100
	SlotKind kind = SlotKind::Item;
	std::string brushName; // valid when kind == SlotKind::Brush
};

struct ReplacementRule {
	uint16_t fromId = 0;
	std::vector<ReplacementTarget> targets;
	SlotKind fromKind = SlotKind::Item;
	std::string fromBrushName; // valid when fromKind == SlotKind::Brush
	// Position offset (in tiles) applied to the replacement item, relative to the
	// original. 0,0 keeps the replacement in place (legacy behaviour); any other
	// value relocates the new item to originalPos + (offsetX, offsetY).
	int offsetX = 0;
	int offsetY = 0;

	bool isBrushRule() const {
		return fromKind == SlotKind::Brush;
	}
	bool hasSource() const {
		return isBrushRule() ? !fromBrushName.empty() : fromId != 0;
	}
};

struct RuleSet {
	std::string name;
	std::vector<ReplacementRule> rules;
};

class RuleManager {
public:
	static RuleManager& Get();

	bool SaveRuleSet(const RuleSet& ruleSet);
	RuleSet LoadRuleSet(const std::string& name);
	bool DeleteRuleSet(const std::string& name);
	std::vector<std::string> GetAvailableRuleSets();

private:
	RuleManager();
	std::string GetRulesDir();
};

#endif
