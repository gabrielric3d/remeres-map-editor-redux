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

#ifndef RME_GROUND_BRUSH_H
#define RME_GROUND_BRUSH_H

#include <vector>
#include "brushes/brush.h"

//=============================================================================

class GroundBrush : public TerrainBrush {
	friend class GroundBrushLoader;
	friend class GroundBorderCalculator;

protected:
	struct BorderBlock;

public:
	static void init();

	GroundBrush();
	~GroundBrush() override;


	bool load(pugi::xml_node node, std::vector<std::string>& warnings) override;

	void draw(BaseMap* map, Tile* tile, void* parameter) override;
	void undraw(BaseMap* map, Tile* tile) override;
	void getRelatedItems(std::vector<uint16_t>& items) override;

	static void doBorders(BaseMap* map, Tile* tile);
	static const BorderBlock* getBrushTo(GroundBrush* first, GroundBrush* second);
	static std::vector<const BorderBlock*> getBrushesTo(GroundBrush* first, GroundBrush* second);

	// --- Border variants -------------------------------------------------
	// A ground brush can declare the same align/to border twice with different
	// shapes, tagged variant="1" / variant="2" in grounds.xml. Only the blocks
	// matching the active variant are used while painting, so the shape can be
	// switched with a hotkey instead of editing and reloading the brushes.
	// Blocks without a variant (variant == 0) always apply.
	static int getActiveBorderVariant();
	static void setActiveBorderVariant(int variant);
	// Cycles to the next variant declared by `context` (or, without a ground
	// brush in hand, by any loaded brush) and returns the new active variant.
	static int cycleActiveBorderVariant(const GroundBrush* context);
	// Bitmask of the variant numbers declared across every loaded ground brush.
	static uint32_t getGlobalVariantMask() {
		return global_variant_mask;
	}
	static void clearGlobalVariantMask() {
		global_variant_mask = 0;
	}

	// Bitmask of variant numbers this brush declares (bit N-1 = variant N).
	uint32_t getVariantMask() const {
		return variant_mask;
	}
	bool hasBorderVariants() const {
		// More than one bit set: the brush actually offers a choice.
		return variant_mask != 0 && (variant_mask & (variant_mask - 1)) != 0;
	}
	// The variant this brush really paints with right now: the active one when
	// declared, otherwise the lowest it does declare (so brushes that never opted
	// into variants keep their single border set).
	int getEffectiveVariant() const;

private:
	static bool isExcludedBrush(const BorderBlock* bb, uint32_t brushId);
	// True when `bb` (owned by `owner`) belongs to the variant currently painted.
	static bool isVariantActive(const GroundBrush* owner, const BorderBlock* bb);

public:

	int32_t getZ() const override {
		return z_order;
	}
	bool useSoloOptionalBorder() const {
		return use_only_optional;
	}
	bool isReRandomizable() const {
		return randomize;
	}

	bool hasOuterZilchBorder() const {
		return has_zilch_outer_border || optional_border;
	}
	bool hasInnerZilchBorder() const {
		return has_zilch_inner_border;
	}
	bool hasOuterBorder() const {
		return has_outer_border || optional_border;
	}
	bool hasInnerBorder() const {
		return has_inner_border;
	}
	bool hasOptionalBorder() const {
		return optional_border != nullptr;
	}
	// True when the item id is one of the border pieces this brush places (any of its
	// border sets, every variant, plus the optional border). Used to pick "this
	// ground's borders" out of a tile, e.g. by the magic wand selection.
	bool ownsBorderItem(uint16_t item_id) const;
	// Declared with carpet_fill="true" in grounds.xml ("Carpet fill" in the
	// Brushes Editor): this brush paints like a carpet brush - edge pieces go
	// on the painted tiles themselves and the ground only fills tiles
	// surrounded by the brush. Brushes without it always use the traditional
	// auto-border pipeline and are never touched by the carpet one.
	bool isCarpetFill() const {
		return carpet_fill;
	}
	// isCarpetFill() gated by the global master switch (Edit > Border Options >
	// Carpet Fill Borders). Everything carpet-related keys off this.
	bool paintsAsCarpet() const;

	// True if `itemId` is an edge piece of any of this brush's outer borders
	// (every variant). Carpet Fill uses the pieces as the membership marker of
	// margin tiles, which keep their old ground underneath.
	bool ownsCarpetPiece(uint16_t itemId) const;

	// Random center ground id using the <item chance> weights.
	uint16_t getRandomGroundItemId() const;

	// Carpet Fill support: reverse index from edge-piece item ids to the
	// carpet_fill ground brushes whose outer borders use them. Margin tiles
	// keep their old ground, so the edge piece is the only marker of which
	// brush is painted there. Several brushes may share a border set; the
	// calculator then picks the one already present around the tile.
	static const std::vector<GroundBrush*>& getCarpetPieceOwners(uint16_t itemId);
	// Returns false when the piece was already registered by ANOTHER brush (the
	// registration still happens); registering the same pair twice is a no-op.
	static bool registerCarpetPieceOwner(uint16_t itemId, GroundBrush* brush);
	static void clearCarpetPieceOwners();

	// Accessors for dungeon generator / preset editor
	uint16_t getFirstGroundItemId() const {
		return border_items.empty() ? 0 : border_items[0].id;
	}
	// Every center ground variation, in declaration order. The Advanced Replace
	// brush-swap dialog needs all of them: emitting a rule only for the first
	// would silently leave tiles painted with the other variations behind.
	std::vector<uint16_t> getGroundItemIds() const {
		std::vector<uint16_t> ids;
		ids.reserve(border_items.size());
		for (const auto& block : border_items) {
			if (block.id != 0) {
				ids.push_back(block.id);
			}
		}
		return ids;
	}
	// Outer border of the variant currently being painted, falling back to the
	// first outer border. Carpet Fill places edge pieces directly instead of
	// going through getBrushesTo(), so it needs the variant filter here.
	const AutoBorder* getActiveOuterAutoBorder() const;
	const AutoBorder* getFirstOuterAutoBorder() const {
		for (const auto& b : borders) {
			if (b && b->autoborder && b->outer) return b->autoborder;
		}
		return nullptr;
	}
	const AutoBorder* getFirstInnerAutoBorder() const {
		for (const auto& b : borders) {
			if (b && b->autoborder && !b->outer) return b->autoborder;
		}
		return nullptr;
	}
	const AutoBorder* getFirstAutoBorder() const {
		for (const auto& b : borders) {
			if (b && b->autoborder) return b->autoborder;
		}
		return nullptr;
	}

protected: // Members
	int32_t z_order;
	bool has_zilch_outer_border;
	bool has_zilch_inner_border;
	bool has_outer_border;
	bool has_inner_border;
	std::unique_ptr<AutoBorder> owned_optional_border;
	AutoBorder* optional_border;
	bool use_only_optional; // If this is true, there will be no normal border under the gravel
	bool randomize;
	bool carpet_fill;
	uint32_t variant_mask = 0;

	struct SpecificCaseBlock {
		SpecificCaseBlock() :
			match_group(0), group_match_alignment(BORDER_NONE), to_replace_id(0), with_id(0), delete_all(false), keepBorder(false) { }
		std::vector<uint16_t> items_to_match;
		uint32_t match_group;
		BorderType group_match_alignment;
		uint16_t to_replace_id;
		uint16_t with_id;
		bool delete_all;
		bool keepBorder;
	};

	struct BorderBlock {
		bool outer;
		bool super;
		uint32_t to;
		// 0 = no variant declared (always used); 1..32 = only used while that
		// variant is the active one. See getActiveBorderVariant().
		int32_t variant = 0;
		std::vector<uint32_t> not_to; // Brushes to exclude from this border
		int32_t layer_order = 0; // Order within same z-level (0 = bottom, higher = on top)

		std::unique_ptr<AutoBorder> owned_autoborder;
		AutoBorder* autoborder;
		std::vector<std::unique_ptr<SpecificCaseBlock>> specific_cases;
	};

	struct ItemChanceBlock {
		int chance;
		uint16_t id;
	};

	struct BorderCluster {
		uint32_t alignment;
		int32_t z;
		int32_t layer_order = 0; // Order within same z-level
		const AutoBorder* border;
		// Target brush id this cluster is bordering against. Part of the dedup key together
		// with `border`, so the same AutoBorder reused in <border to="X"> and <border to="Y">
		// keeps its direction bitmasks separate instead of merging into one broken alignment.
		uint32_t to = 0xFFFFFFFF;
		// Carpet Fill layer: when set, the cluster stamps exactly this piece on
		// the tile (a member of the carpet region) instead of composing pieces
		// from `alignment` through border_types.
		BorderType carpet_piece = BORDER_NONE;

		bool operator<(const BorderCluster& other) const {
			if (z != other.z) {
				return z < other.z;
			}
			return layer_order < other.layer_order;
		}
	};

	std::vector<std::unique_ptr<BorderBlock>> borders;
	std::vector<ItemChanceBlock> border_items;
	int total_chance;

public: // Static global members
	static uint32_t border_types[256];
	// Union of every brush's variant_mask, rebuilt as the materials load.
	static uint32_t global_variant_mask;
};

#endif
