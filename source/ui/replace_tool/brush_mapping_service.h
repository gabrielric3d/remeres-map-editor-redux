//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_BRUSH_MAPPING_SERVICE_H_
#define RME_BRUSH_MAPPING_SERVICE_H_

#include <cstdint>
#include <string>
#include <vector>

class Brush;
class Item;
class AutoBorder;

// Pure (no UI, no map mutation) service that maps an item belonging to a source
// brush onto the item playing the *same role* in a destination brush.
//
// Roles handled:
//   GroundBrush  -> ground center, or a border direction (BorderType 1..12)
//   WallBrush    -> wall segment alignment, preserving doors/windows by DoorType
//   CarpetBrush  -> carpet alignment group (BorderType 0..12 + CARPET_CENTER)
//
// Table and Doodad brushes are deliberately out of scope: they are not
// role-equivalent in a way that survives a 1:1 substitution.
//
// The service never calls doBorders/doWalls/doCarpets: the existing layout is
// preserved exactly, only the item ids change.
class BrushMappingService {
public:
	// Outcome of trying to map one item.
	struct MapResult {
		bool matched = false; // the item belongs to the source brush
		bool resolved = false; // an equivalent was found in the destination brush
		uint16_t newId = 0;
	};

	// Resolves a brush by name in the global registry; nullptr when absent.
	static Brush* FindBrush(const std::string& name);

	// True when both brushes are of the same family (Ground/Wall/Carpet) and can
	// therefore be swapped by role.
	static bool AreCompatible(const Brush* from, const Brush* to);

	// Maps one concrete map item. Mutates nothing.
	static MapResult MapItem(const Item* item, Brush* fromBrush, Brush* toBrush);

	// Representative item id used to draw the brush icon on a rule card.
	static uint16_t GetPreviewItemId(const Brush* brush);

	// Human readable family name ("Ground" / "Wall" / "Carpet" / "").
	static const char* GetFamilyName(const Brush* brush);

	// One row of a brush-to-brush expansion: an item of the source brush next to
	// the item playing the same role in the destination brush.
	struct RolePair {
		std::string role; // "Ground", "North", "Corner NW", "Vertical / Window"...
		uint16_t fromId = 0;
		uint16_t toId = 0; // 0 when the destination has nothing for this role
	};

	// Expands both brushes into their items, aligned by role, in canonical order
	// (center first, then N/E/S/W, corners, diagonals).
	//
	// `to` may be null: the swap dialog lists the source items as soon as the
	// first brush is picked and fills the second column in afterwards. When both
	// are given but belong to different families the result is empty.
	//
	// Variations are paired by index; if the destination declares fewer of them
	// its last one is reused, so no source item is left without a target.
	static std::vector<RolePair> BuildRolePairs(const Brush* from, const Brush* to);

	// ---- Borders -----------------------------------------------------------
	// AutoBorders (borders.xml) are not Brush objects: they live in their own
	// registry keyed by a numeric id and carry no name, which is why they can
	// only be found by id. They are still swappable by role, so the picker and
	// the swap dialog address either kind through this selection type.
	struct Selection {
		std::string brushName; // set when a Brush was picked
		uint32_t borderId = 0; // set when an AutoBorder was picked

		bool isBorder() const {
			return borderId != 0;
		}
		bool empty() const {
			return brushName.empty() && borderId == 0;
		}
	};

	// Resolves an AutoBorder by its numeric id; nullptr when absent.
	static const AutoBorder* FindBorder(uint32_t borderId);

	// Same family check, extended to borders (a border only pairs with a border).
	static bool AreCompatible(const Selection& from, const Selection& to);

	// Representative item id for a selection, used for the picker icon.
	static uint16_t GetPreviewItemId(const Selection& selection);

	// Family name of a selection ("Ground" / "Wall" / "Carpet" / "Border" / "").
	static const char* GetFamilyName(const Selection& selection);

	// Role expansion for either kind. An empty/unresolvable `to` still lists the
	// source items, so the dialog can show them before a destination is picked.
	static std::vector<RolePair> BuildRolePairs(const Selection& from, const Selection& to);

	// Every server item id the selection owns: ground centers and border
	// directions, wall segments and doors, or carpet groups. Lets the picker
	// find a brush by the server id of any item inside it.
	static std::vector<uint16_t> GetItemIds(const Selection& selection);
};

#endif
