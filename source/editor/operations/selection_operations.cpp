//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"

#include "editor/operations/selection_operations.h"
#include "editor/operations/rotation_utility.h"
#include "editor/editor.h"
#include "editor/action.h"
#include "editor/action_queue.h"
#include "editor/selection.h"
#include "map/map.h"
#include "map/tile_operations.h"
#include "brushes/ground/ground_brush.h"
#include "app/settings.h"
#include "ui/gui.h"

#include "brushes/doodad/doodad_brush.h"
#include "brushes/door/door_brush.h"
#include "brushes/wall/wall_brush.h"
#include "brushes/house/house_exit_brush.h"
#include "brushes/waypoint/waypoint_brush.h"
#include "brushes/camera/camera_path_brush.h"
#include "game/item.h"
#include "ui/replace_tool/brush_mapping_service.h"

#include <algorithm>
#include <unordered_set>
#include <functional>

void SelectionOperations::doSurroundingBorders(DoodadBrush* doodad_brush, PositionList& tilestoborder, Tile* buffer_tile, Tile* new_tile) {
	if (doodad_brush->doNewBorders() && g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		tilestoborder.push_back(Position(new_tile->getPosition().x, new_tile->getPosition().y, new_tile->getPosition().z));
		if (buffer_tile->hasGround()) {
			for (int y = -1; y <= 1; y++) {
				for (int x = -1; x <= 1; x++) {
					tilestoborder.push_back(Position(new_tile->getPosition().x + x, new_tile->getPosition().y + y, new_tile->getPosition().z));
				}
			}
		} else if (buffer_tile->hasWall()) {
			tilestoborder.push_back(Position(new_tile->getPosition().x, new_tile->getPosition().y - 1, new_tile->getPosition().z));
			tilestoborder.push_back(Position(new_tile->getPosition().x - 1, new_tile->getPosition().y, new_tile->getPosition().z));
			tilestoborder.push_back(Position(new_tile->getPosition().x + 1, new_tile->getPosition().y, new_tile->getPosition().z));
			tilestoborder.push_back(Position(new_tile->getPosition().x, new_tile->getPosition().y + 1, new_tile->getPosition().z));
		}
	}
}

void SelectionOperations::removeDuplicateWalls(Tile* buffer, Tile* tile) {
	for (const auto& item : buffer->items) {
		if (item->getWallBrush()) {
			TileOperations::cleanWalls(tile, item->getWallBrush());
		}
	}
}

void SelectionOperations::borderizeSelection(Editor& editor) {
	if (editor.selection.empty()) {
		g_gui.SetStatusText("No items selected. Can't borderize.");
	}

	std::unique_ptr<Action> action = editor.actionQueue->createAction(ACTION_BORDERIZE);
	for (Tile* tile : editor.selection) {
		std::unique_ptr<Tile> newTile = TileOperations::deepCopy(tile, editor.map);
		TileOperations::borderize(newTile.get(), &editor.map);
		TileOperations::select(newTile.get());
		action->addChange(std::make_unique<Change>(std::move(newTile)));
	}
	editor.addAction(std::move(action));
}

void SelectionOperations::randomizeSelection(Editor& editor) {
	if (editor.selection.empty()) {
		g_gui.SetStatusText("No items selected. Can't randomize.");
	}

	std::unique_ptr<Action> action = editor.actionQueue->createAction(ACTION_RANDOMIZE);
	for (Tile* tile : editor.selection) {
		std::unique_ptr<Tile> newTile = TileOperations::deepCopy(tile, editor.map);
		GroundBrush* groundBrush = newTile->getGroundBrush();
		if (groundBrush && groundBrush->isReRandomizable()) {
			groundBrush->draw(&editor.map, newTile.get(), nullptr);

			Item* oldGround = tile->ground.get();
			Item* newGround = newTile->ground.get();
			if (oldGround && newGround) {
				newGround->setActionID(oldGround->getActionID());
				newGround->setUniqueID(oldGround->getUniqueID());
			}

			TileOperations::select(newTile.get());
			action->addChange(std::make_unique<Change>(std::move(newTile)));
		}
	}
	editor.addAction(std::move(action));
}

namespace {
	// A tile counts as part of the "area" to fill when its ground is selected, or when
	// it has no ground and something other than a border piece is selected on it.
	// Area selections (rectangle/lasso) select whole tiles, so they always pass. The
	// magic wand also selects a ground's border pieces on the ring of neighbouring
	// tiles; those tiles must not be painted, or every fill would grow the patch by
	// one tile.
	bool isFillTarget(const Tile* tile) {
		if (tile->ground) {
			return tile->ground->isSelected();
		}
		if (tile->items.empty()) {
			return true;
		}
		for (const auto& item : tile->items) {
			if (item->isSelected() && !item->isBorder()) {
				return true;
			}
		}
		return false;
	}
}

namespace {
	// Border-aware ground swap for Fill Selection (Edit > Border Options > "Fill Selection
	// Swaps Borders"). Replaces the ground of every target tile with `ground`, then maps
	// the border pieces of the grounds being replaced, on the targets and on the ring of
	// tiles around them, onto the pieces playing the same role in `ground` (outer north
	// -> outer north, ...), exactly like the Replace Tool's brush swap. No borderize pass
	// runs: hand-placed borders and every other ground keep their items, and it works
	// with Border Automagic off. Pieces with no equivalent in `ground` are kept (the
	// Replace Tool's safe failure) rather than leaving a hole.
	void swapGroundInSelection(Editor& editor, GroundBrush* ground, const PositionVector& targets) {
		const auto key = [](const Position& p) -> uint64_t {
			return (static_cast<uint64_t>(static_cast<uint8_t>(p.z)) << 32)
				| (static_cast<uint64_t>(static_cast<uint16_t>(p.y)) << 16)
				| static_cast<uint16_t>(p.x);
		};

		std::unordered_set<uint64_t> target_keys;
		std::vector<GroundBrush*> old_brushes;
		for (const Position& pos : targets) {
			target_keys.insert(key(pos));
			Tile* tile = editor.map.getTile(pos);
			GroundBrush* old = tile ? tile->getGroundBrush() : nullptr;
			if (old && old != ground && std::find(old_brushes.begin(), old_brushes.end(), old) == old_brushes.end()) {
				old_brushes.push_back(old);
			}
		}

		// Swaps the border pieces of the replaced grounds on a tile copy; true when
		// anything changed. setID keeps attributes and the selection flag intact.
		const auto swapBorders = [&](Tile* copy) {
			bool changed = false;
			for (const auto& item : copy->items) {
				if (!item->isBorder()) {
					continue;
				}
				for (GroundBrush* old : old_brushes) {
					const BrushMappingService::MapResult result = BrushMappingService::MapItem(item.get(), old, ground);
					if (!result.matched) {
						continue;
					}
					if (result.resolved && result.newId != item->getID()) {
						item->setID(result.newId);
						changed = true;
					}
					break;
				}
			}
			return changed;
		};

		std::unique_ptr<Action> action = editor.actionQueue->createAction(ACTION_DRAW);

		for (const Position& pos : targets) {
			Tile* tile = editor.map.getTile(pos);
			if (!tile) {
				continue; // Targets come from the selection, so they exist; be safe anyway.
			}
			std::unique_ptr<Tile> copy = TileOperations::deepCopy(tile, editor.map);
			bool changed = false;
			if (copy->ground) {
				if (copy->getGroundBrush() != ground) {
					const uint16_t new_id = ground->getRandomGroundItemId();
					if (new_id != 0) {
						copy->ground->setID(new_id);
						changed = true;
					}
				}
			} else {
				ground->draw(&editor.map, copy.get(), nullptr);
				changed = true;
			}
			changed = swapBorders(copy.get()) || changed;
			if (changed) {
				action->addChange(std::make_unique<Change>(std::move(copy)));
			}
		}

		// Ring: outer borders of the replaced grounds sit on the neighbours.
		std::unordered_set<uint64_t> ring_seen;
		for (const Position& pos : targets) {
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					if (dx == 0 && dy == 0) {
						continue;
					}
					const Position n(pos.x + dx, pos.y + dy, pos.z);
					if (!n.isValid()) {
						continue;
					}
					const uint64_t k = key(n);
					if (target_keys.count(k) || !ring_seen.insert(k).second) {
						continue;
					}
					Tile* neighbour = editor.map.getTile(n);
					if (!neighbour) {
						continue;
					}
					std::unique_ptr<Tile> copy = TileOperations::deepCopy(neighbour, editor.map);
					if (swapBorders(copy.get())) {
						action->addChange(std::make_unique<Change>(std::move(copy)));
					}
				}
			}
		}

		editor.addAction(std::move(action), 2);
	}
}

SelectionOperations::MagicWandTarget SelectionOperations::magicWandTarget(Tile* tile) {
	MagicWandTarget target;
	if (!tile) {
		return target;
	}
	// Topmost item first, so clicking a tree on grass picks the tree.
	for (auto it = tile->items.rbegin(); it != tile->items.rend(); ++it) {
		Item* item = it->get();
		Brush* doodad = item->getDoodadBrush();
		if (doodad && doodad->is<DoodadBrush>()) {
			target.kind = MagicWandTarget::Kind::Doodad;
			target.brush = doodad;
			target.anchor = item;
			return target;
		}
	}
	for (auto it = tile->items.rbegin(); it != tile->items.rend(); ++it) {
		Item* item = it->get();
		if (item->isWall() && item->getWallBrush()) {
			target.kind = MagicWandTarget::Kind::Wall;
			target.brush = item->getWallBrush();
			target.anchor = item;
			return target;
		}
	}
	if (tile->ground && tile->getGroundBrush()) {
		target.kind = MagicWandTarget::Kind::Ground;
		target.brush = tile->getGroundBrush();
		target.anchor = tile->ground.get();
	}
	return target;
}

namespace {
	bool wallBelongsTo(Item* item, WallBrush* wall) {
		return item->isWall() && (item->getWallBrush() == wall || wall->hasWall(item));
	}

	bool tileHasWallOf(Tile* tile, WallBrush* wall) {
		if (!tile) {
			return false;
		}
		for (const auto& item : tile->items) {
			if (wallBelongsTo(item.get(), wall)) {
				return true;
			}
		}
		return false;
	}

	// 4-connected flood fill on one floor over tiles accepted by `belongs`. Returns
	// the tiles found (origin first) and sets `truncated` when the cap was hit.
	template <typename Predicate>
	std::vector<Tile*> floodTiles(Editor& editor, const Position& origin, size_t cap, bool& truncated, Predicate belongs) {
		const auto key = [](int x, int y) -> uint64_t {
			return (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32) | static_cast<uint32_t>(x);
		};
		std::vector<Tile*> found;
		std::unordered_set<uint64_t> visited;
		std::vector<Position> stack;
		stack.push_back(origin);
		visited.insert(key(origin.x, origin.y));
		truncated = false;
		while (!stack.empty()) {
			const Position pos = stack.back();
			stack.pop_back();
			Tile* tile = editor.map.getTile(pos);
			if (!tile || !belongs(tile)) {
				continue;
			}
			found.push_back(tile);
			if (found.size() >= cap) {
				truncated = true;
				break;
			}
			static constexpr int dx[4] = { 1, -1, 0, 0 };
			static constexpr int dy[4] = { 0, 0, 1, -1 };
			for (int i = 0; i < 4; ++i) {
				const Position next(pos.x + dx[i], pos.y + dy[i], pos.z);
				if (next.isValid() && visited.insert(key(next.x, next.y)).second) {
					stack.push_back(next);
				}
			}
		}
		return found;
	}
}

size_t SelectionOperations::magicWandSelect(Editor& editor, const Position& origin, bool add_to_selection) {
	const MagicWandTarget target = magicWandTarget(editor.map.getTile(origin));
	if (target.kind == MagicWandTarget::Kind::None) {
		return 0;
	}

	// Tiles to touch, each with the rule picking which of its items to select
	// (ground/doodad/wall items of the brush). Patch tiles for a ground also take the
	// ground itself; ring tiles only its border pieces.
	struct Entry {
		Tile* tile;
		bool with_ground;
	};
	std::vector<Entry> entries;
	std::function<bool(Item*)> owns;
	bool truncated = false;
	std::string what;

	switch (target.kind) {
		case MagicWandTarget::Kind::Ground: {
			GroundBrush* ground = target.brush->as<GroundBrush>();
			std::vector<Tile*> patch = floodTiles(editor, origin, MAGIC_WAND_MAX_TILES, truncated, [ground](Tile* tile) {
				return tile->getGroundBrush() == ground;
			});
			std::unordered_set<uint64_t> patch_keys;
			const auto key = [](const Position& p) -> uint64_t {
				return (static_cast<uint64_t>(static_cast<uint32_t>(p.y)) << 32) | static_cast<uint32_t>(p.x);
			};
			for (Tile* tile : patch) {
				patch_keys.insert(key(tile->getPosition()));
				entries.push_back({ tile, true });
			}
			// Ring: the 8 neighbours of every patch tile that are not in the patch. Outer
			// borders of the ground live there (on the neighbouring grounds or void tiles).
			std::unordered_set<uint64_t> ring_seen;
			for (Tile* tile : patch) {
				const Position p = tile->getPosition();
				for (int oy = -1; oy <= 1; ++oy) {
					for (int ox = -1; ox <= 1; ++ox) {
						if (ox == 0 && oy == 0) {
							continue;
						}
						const Position n(p.x + ox, p.y + oy, p.z);
						if (!n.isValid()) {
							continue;
						}
						const uint64_t k = key(n);
						if (patch_keys.count(k) || !ring_seen.insert(k).second) {
							continue;
						}
						if (Tile* neighbour = editor.map.getTile(n)) {
							entries.push_back({ neighbour, false });
						}
					}
				}
			}
			owns = [ground](Item* item) {
				return item->isBorder() && ground->ownsBorderItem(item->getID());
			};
			what = std::to_string(patch.size()) + " tile(s) of " + ground->getName();
			break;
		}
		case MagicWandTarget::Kind::Wall: {
			WallBrush* wall = target.brush->as<WallBrush>();
			std::vector<Tile*> run = floodTiles(editor, origin, MAGIC_WAND_MAX_TILES, truncated, [wall](Tile* tile) {
				return tileHasWallOf(tile, wall);
			});
			for (Tile* tile : run) {
				entries.push_back({ tile, false });
			}
			owns = [wall](Item* item) {
				return wallBelongsTo(item, wall);
			};
			what = std::to_string(run.size()) + " wall tile(s) of " + wall->getName();
			break;
		}
		case MagicWandTarget::Kind::Doodad: {
			DoodadBrush* doodad = target.brush->as<DoodadBrush>();
			// Doodads are scattered by nature, so the wand is not contiguous here: every
			// item of the brush on this floor is taken.
			for (MapIterator it = editor.map.begin(); it != editor.map.end(); ++it) {
				Tile* tile = it->get();
				if (!tile || tile->getZ() != origin.z) {
					continue;
				}
				bool has = false;
				for (const auto& item : tile->items) {
					if (doodad->ownsItem(item.get())) {
						has = true;
						break;
					}
				}
				if (!has) {
					continue;
				}
				entries.push_back({ tile, false });
				if (entries.size() >= MAGIC_WAND_MAX_TILES) {
					truncated = true;
					break;
				}
			}
			owns = [doodad](Item* item) {
				return doodad->ownsItem(item);
			};
			what = std::to_string(entries.size()) + " tile(s) with " + doodad->getName() + " (whole floor)";
			break;
		}
		case MagicWandTarget::Kind::None:
			return 0;
	}
	if (entries.empty()) {
		return 0;
	}

	if (!add_to_selection) {
		editor.selection.start();
		editor.selection.clear();
		editor.selection.finish();
	}

	// One copy per tile with exactly the wanted items selected. Selection::add(tile,
	// item) would copy the tile once per item and honour BORDER_IS_GROUND, which for
	// a ground would drag the neighbours' ground into the selection through their
	// borders.
	std::unique_ptr<Action> action = editor.actionQueue->createAction(ACTION_SELECT);
	size_t changed_tiles = 0;
	for (const Entry& entry : entries) {
		bool changed = false;
		std::unique_ptr<Tile> copy = TileOperations::deepCopy(entry.tile, editor.map);
		if (entry.with_ground && copy->ground && !copy->ground->isSelected()) {
			copy->ground->select();
			changed = true;
		}
		for (const auto& item : copy->items) {
			if (!item->isSelected() && owns(item.get())) {
				item->select();
				changed = true;
			}
		}
		if (changed) {
			action->addChange(std::make_unique<Change>(std::move(copy)));
			++changed_tiles;
		}
	}
	if (changed_tiles > 0) {
		editor.addAction(std::move(action));
	}
	editor.selection.updateSelectionCount();

	std::string status = "Magic wand: " + what;
	if (truncated) {
		status += " (stopped at the " + std::to_string(MAGIC_WAND_MAX_TILES) + " tile limit)";
	}
	g_gui.SetStatusText(status);
	return entries.size();
}

bool SelectionOperations::fillSelection(Editor& editor) {
	if (editor.selection.empty()) {
		g_gui.SetStatusText("Nothing selected. Select an area first, then fill it with the current brush.");
		return false;
	}

	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		g_gui.SetStatusText("No brush selected. Pick a brush in the palette, then fill the selection.");
		return false;
	}
	if (brush->is<HouseExitBrush>() || brush->is<WaypointBrush>() || brush->is<CameraPathBrush>()) {
		g_gui.SetStatusText("This brush places a single point and can't fill an area.");
		return false;
	}

	// Selected tiles are unique, so the positions are too. They may span several
	// floors (e.g. a "visible floors" selection); every tile is filled on its own floor.
	PositionVector positions;
	positions.reserve(editor.selection.size());
	for (Tile* tile : editor.selection) {
		if (isFillTarget(tile)) {
			positions.push_back(tile->getPosition());
		}
	}
	if (positions.empty()) {
		g_gui.SetStatusText("Only border pieces are selected; nothing to fill. Select the ground itself (or an area) first.");
		return false;
	}

	if (brush->is<DoodadBrush>()) {
		// Same as smearing a doodad brush: one composite per tile, re-rolled between
		// tiles so thickness/variation apply instead of stamping an identical copy.
		for (const Position& pos : positions) {
			editor.draw(pos, false);
			g_gui.FillDoodadPreviewBuffer();
		}
	} else if (brush->is<GroundBrush>() && g_settings.getBoolean(Config::FILL_SWAP_BORDERS)) {
		// Replace Tool style: ground + this ground's borders swapped by role, nothing
		// re-bordered, regardless of Border Automagic.
		swapGroundInSelection(editor, brush->as<GroundBrush>(), positions);
	} else if (brush->needBorders() || brush->is<WallBrush>() || brush->is<DoorBrush>()) {
		PositionVector todraw;
		if (brush->is<DoorBrush>()) {
			// Doors only go on walls; skip the rest instead of failing the whole fill.
			for (const Position& pos : positions) {
				if (brush->canDraw(&editor.map, pos)) {
					todraw.push_back(pos);
				}
			}
			if (todraw.empty()) {
				g_gui.SetStatusText("No wall in the selection to place a door on.");
				return false;
			}
		} else {
			todraw = positions;
		}

		// Auto-border needs the perimeter of the filled region to recompute the
		// neighbours, same 3x3 expansion with dedup the Ctrl+D ground flood fill uses.
		std::unordered_set<uint64_t> seen;
		const auto key = [](const Position& p) -> uint64_t {
			return (static_cast<uint64_t>(static_cast<uint8_t>(p.z)) << 32)
				| (static_cast<uint64_t>(static_cast<uint16_t>(p.y)) << 16)
				| static_cast<uint16_t>(p.x);
		};
		PositionVector toborder;
		toborder.reserve(todraw.size() * 3);
		for (const Position& p : todraw) {
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					const Position n(p.x + dx, p.y + dy, p.z);
					if (n.isValid() && seen.insert(key(n)).second) {
						toborder.push_back(n);
					}
				}
			}
		}
		editor.draw(todraw, toborder, false);
	} else {
		// RAW, house, flag/zone, creature, spawn, optional border...: plain per-tile draw.
		editor.draw(positions, false);
	}

	// The drawn tiles keep their selection flag through the action commit, but the
	// items the brush just placed do not. Re-select the filled tiles as a whole so a
	// follow-up move/cut/second fill sees the new content too. Internal session: this
	// is not a separate undo step, the draw above is.
	editor.selection.start(Selection::INTERNAL);
	for (const Position& pos : positions) {
		if (Tile* tile = editor.map.getTile(pos)) {
			editor.selection.add(tile);
		}
	}
	editor.selection.finish(Selection::INTERNAL);
	editor.selection.updateSelectionCount();
	return true;
}

void SelectionOperations::moveSelection(Editor& editor, Position offset) {
	std::unique_ptr<BatchAction> batchAction = editor.actionQueue->createBatch(ACTION_MOVE); // Our saved action batch, for undo!
	std::unique_ptr<Action> action;

	// Remove tiles from the map
	action = editor.actionQueue->createAction(batchAction.get()); // Our action!
	bool doborders = false;
	TileSet tmp_storage;

	// Update the tiles with the newd positions
	for (Tile* tile : editor.selection) {
		// First we get the old tile and it's position

		// Create the duplicate source tile, which will replace the old one later
		std::unique_ptr<Tile> new_src_tile = TileOperations::deepCopy(tile, editor.map);

		std::unique_ptr<Tile> tmp_storage_tile = editor.map.allocator(tile->getLocation());

		// Get all the selected items from the NEW source tile and iterate through them
		// This transfers ownership to the temporary tile
		auto tile_selection = TileOperations::popSelectedItems(new_src_tile.get());
		for (auto& item : tile_selection) {
			// Add the copied item to the newd destination tile,
			tmp_storage_tile->addItem(std::move(item));
		}
		// Move spawns
		if (new_src_tile->spawn && new_src_tile->spawn->isSelected()) {
			tmp_storage_tile->spawn = std::move(new_src_tile->spawn);
		}
		// Move creatures
		if (new_src_tile->creature && new_src_tile->creature->isSelected()) {
			tmp_storage_tile->creature = std::move(new_src_tile->creature);
		}

		// Move house data & tile status if ground is transferred
		if (tmp_storage_tile->ground) {
			tmp_storage_tile->house_id = new_src_tile->house_id;
			new_src_tile->house_id = 0;
			tmp_storage_tile->soundZoneId = new_src_tile->soundZoneId; // BlackTalon
			new_src_tile->soundZoneId = 0;
			tmp_storage_tile->instanceZoneId = new_src_tile->instanceZoneId; // BlackTalon
			new_src_tile->instanceZoneId = 0;
			tmp_storage_tile->setMapFlags(new_src_tile->getMapFlags());
			new_src_tile->setMapFlags(TILESTATE_NONE);
			doborders = true;
		}

		tmp_storage.push_back(tmp_storage_tile.release());
		// Add the tile copy to the action
		action->addChange(std::make_unique<Change>(std::move(new_src_tile)));
	}
	// Commit changes to map
	batchAction->addAndCommitAction(std::move(action));

	// Remove old borders (and create some newd?)
	if (g_settings.getInteger(Config::USE_AUTOMAGIC) && g_settings.getInteger(Config::BORDERIZE_DRAG) && editor.selection.size() < size_t(g_settings.getInteger(Config::BORDERIZE_DRAG_THRESHOLD))) {
		action = editor.actionQueue->createAction(batchAction.get());
		TileList borderize_tiles;
		// Go through all modified (selected) tiles (might be slow)
		for (Tile* tile : tmp_storage) {
			Position pos = tile->getPosition();
			// Go through all neighbours
			Tile* t;
			t = editor.map.getTile(pos.x, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x - 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x + 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x - 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x + 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x - 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x + 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
		}
		// Remove duplicates
		borderize_tiles.sort();
		borderize_tiles.unique();
		// Do le borders!
		for (Tile* tile : borderize_tiles) {
			std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
			if (doborders) {
				TileOperations::borderize(new_tile.get(), &editor.map);
			}
			TileOperations::wallize(new_tile.get(), &editor.map);
			TileOperations::tableize(new_tile.get(), &editor.map);
			TileOperations::carpetize(new_tile.get(), &editor.map);
			if (tile->ground && tile->ground->isSelected()) {
				TileOperations::selectGround(new_tile.get());
			}
			action->addChange(std::make_unique<Change>(std::move(new_tile)));
		}
		// Commit changes to map
		batchAction->addAndCommitAction(std::move(action));
	}

	// New action for adding the destination tiles
	action = editor.actionQueue->createAction(batchAction.get());
	for (Tile* tile : tmp_storage) {
		const Position old_pos = tile->getPosition();
		Position new_pos;

		new_pos = old_pos - offset;

		if (new_pos.z < 0 || new_pos.z > MAP_MAX_LAYER) {
			delete tile;
			continue;
		}
		// Create the duplicate dest tile, which will replace the old one later
		TileLocation* location = editor.map.createTileL(new_pos);
		Tile* old_dest_tile = location->get();
		std::unique_ptr<Tile> new_dest_tile;

		if (g_settings.getInteger(Config::MERGE_MOVE) || !tile->ground) {
			// Move items
			if (old_dest_tile) {
				new_dest_tile = TileOperations::deepCopy(old_dest_tile, editor.map);
				ASSERT(new_dest_tile);
			} else {
				new_dest_tile = editor.map.allocator(location);
			}
			TileOperations::merge(new_dest_tile.get(), tile);
			// Removing old tile from memory since we merged it
			delete tile;
		} else {
			// Replace tile instead of just merge
			tile->setLocation(location);
			new_dest_tile.reset(tile);
		}

		action->addChange(std::make_unique<Change>(std::move(new_dest_tile)));
	}

	// Commit changes to the map
	batchAction->addAndCommitAction(std::move(action));

	// Create borders
	if (g_settings.getInteger(Config::USE_AUTOMAGIC) && g_settings.getInteger(Config::BORDERIZE_DRAG) && editor.selection.size() < size_t(g_settings.getInteger(Config::BORDERIZE_DRAG_THRESHOLD))) {
		action = editor.actionQueue->createAction(batchAction.get());
		TileList borderize_tiles;
		// Go through all modified (selected) tiles (might be slow)
		for (Tile* tile : editor.selection) {
			bool add_me = false; // If this tile is touched
			Position pos = tile->getPosition();
			// Go through all neighbours
			Tile* t;
			t = editor.map.getTile(pos.x - 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x + 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x + 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x - 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x + 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x - 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x + 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			if (add_me) {
				borderize_tiles.push_back(tile);
			}
		}
		// Remove duplicates
		borderize_tiles.sort();
		borderize_tiles.unique();
		// Do le borders!
		for (Tile* tile : borderize_tiles) {
			if (tile->ground) {
				if (tile->ground->getGroundBrush()) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);

					if (doborders) {
						TileOperations::borderize(new_tile.get(), &editor.map);
					}

					TileOperations::wallize(new_tile.get(), &editor.map);
					TileOperations::tableize(new_tile.get(), &editor.map);
					TileOperations::carpetize(new_tile.get(), &editor.map);
					if (tile->ground->isSelected()) {
						TileOperations::selectGround(new_tile.get());
					}

					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				}
			}
		}
		// Commit changes to map
		batchAction->addAndCommitAction(std::move(action));
	}

	// Store the action for undo
	editor.addBatch(std::move(batchAction));
	editor.selection.updateSelectionCount();
}

void SelectionOperations::rotateSelection(Editor& editor, int quarterTurns) {
	if (editor.selection.empty()) {
		g_gui.SetStatusText("No items selected. Can't rotate.");
		return;
	}

	RotationUtility rot(quarterTurns);
	if (rot.isIdentity()) {
		return;
	}

	// Multi-floor is allowed. Each floor rotates independently inside its OWN
	// bounding box, so floors keep their relative offset (e.g. the one-tile overlap
	// that makes an upper floor stack on the floor below) instead of having it
	// rotated away. A single-floor selection has just one box, so nothing changes.
	struct FloorBox {
		int minX = 0, minY = 0, maxX = 0, maxY = 0;
		bool init = false;
	};
	FloorBox floor_box[MAP_MAX_LAYER + 1];
	for (Tile* tile : editor.selection) {
		const Position p = tile->getPosition();
		if (p.z < 0 || p.z > MAP_MAX_LAYER) {
			continue;
		}
		FloorBox& fb = floor_box[p.z];
		if (!fb.init) {
			fb.minX = fb.maxX = p.x;
			fb.minY = fb.maxY = p.y;
			fb.init = true;
		} else {
			fb.minX = std::min(fb.minX, p.x);
			fb.minY = std::min(fb.minY, p.y);
			fb.maxX = std::max(fb.maxX, p.x);
			fb.maxY = std::max(fb.maxY, p.y);
		}
	}

	// Rotate a position within its own floor's bounding box.
	auto rotateForFloor = [&](const Position& p) -> Position {
		if (p.z < 0 || p.z > MAP_MAX_LAYER) {
			return p;
		}
		const FloorBox& fb = floor_box[p.z];
		const Position fmin(fb.minX, fb.minY, p.z);
		return rot.rotatePosition(p, fmin, fb.maxX - fb.minX + 1, fb.maxY - fb.minY + 1);
	};

	// All-or-nothing: if any rotated position lands out of bounds, abort before touching the map
	for (Tile* tile : editor.selection) {
		if (!rotateForFloor(tile->getPosition()).isValid()) {
			g_gui.SetStatusText("Rotation would move selection out of bounds.");
			return;
		}
	}

	const bool create_borders = g_settings.getInteger(Config::USE_AUTOMAGIC) && g_settings.getInteger(Config::BORDERIZE_DRAG);
	const bool borderize_neighbors = create_borders && editor.selection.size() < size_t(g_settings.getInteger(Config::BORDERIZE_DRAG_THRESHOLD));

	std::unique_ptr<BatchAction> batchAction = editor.actionQueue->createBatch(ACTION_ROTATE_SELECTION); // Our saved action batch, for undo!
	std::unique_ptr<Action> action;

	// Remove tiles from the map
	action = editor.actionQueue->createAction(batchAction.get()); // Our action!
	bool doborders = false;
	TileSet tmp_storage;

	for (Tile* tile : editor.selection) {
		// Create the duplicate source tile, which will replace the old one later
		std::unique_ptr<Tile> new_src_tile = TileOperations::deepCopy(tile, editor.map);

		std::unique_ptr<Tile> tmp_storage_tile = editor.map.allocator(tile->getLocation());

		// Get all the selected items from the NEW source tile and iterate through them
		// This transfers ownership to the temporary tile
		auto tile_selection = TileOperations::popSelectedItems(new_src_tile.get());
		for (auto& item : tile_selection) {
			// Add the copied item to the newd destination tile,
			tmp_storage_tile->addItem(std::move(item));
		}
		// Move spawns
		if (new_src_tile->spawn && new_src_tile->spawn->isSelected()) {
			tmp_storage_tile->spawn = std::move(new_src_tile->spawn);
		}
		// Move creatures
		if (new_src_tile->creature && new_src_tile->creature->isSelected()) {
			tmp_storage_tile->creature = std::move(new_src_tile->creature);
		}

		// Move house data & tile status if ground is transferred
		if (tmp_storage_tile->ground) {
			tmp_storage_tile->house_id = new_src_tile->house_id;
			new_src_tile->house_id = 0;
			tmp_storage_tile->soundZoneId = new_src_tile->soundZoneId; // BlackTalon
			new_src_tile->soundZoneId = 0;
			tmp_storage_tile->instanceZoneId = new_src_tile->instanceZoneId; // BlackTalon
			new_src_tile->instanceZoneId = 0;
			tmp_storage_tile->setMapFlags(new_src_tile->getMapFlags());
			new_src_tile->setMapFlags(TILESTATE_NONE);
			doborders = true;
		}

		tmp_storage.push_back(tmp_storage_tile.release());
		// Add the tile copy to the action
		action->addChange(std::make_unique<Change>(std::move(new_src_tile)));
	}
	// Commit changes to map
	batchAction->addAndCommitAction(std::move(action));

	// Remove old borders (and create some newd?)
	if (borderize_neighbors) {
		action = editor.actionQueue->createAction(batchAction.get());
		TileList borderize_tiles;
		// Go through all modified (selected) tiles (might be slow)
		for (Tile* tile : tmp_storage) {
			Position pos = tile->getPosition();
			// Go through all neighbours
			Tile* t;
			t = editor.map.getTile(pos.x, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x - 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x + 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x - 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x + 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x - 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = editor.map.getTile(pos.x + 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
		}
		// Remove duplicates
		borderize_tiles.sort();
		borderize_tiles.unique();
		// Do le borders!
		for (Tile* tile : borderize_tiles) {
			std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
			if (doborders) {
				TileOperations::borderize(new_tile.get(), &editor.map);
			}
			TileOperations::wallize(new_tile.get(), &editor.map);
			TileOperations::tableize(new_tile.get(), &editor.map);
			TileOperations::carpetize(new_tile.get(), &editor.map);
			if (tile->ground && tile->ground->isSelected()) {
				TileOperations::selectGround(new_tile.get());
			}
			action->addChange(std::make_unique<Change>(std::move(new_tile)));
		}
		// Commit changes to map
		batchAction->addAndCommitAction(std::move(action));
	}

	// New action for adding the rotated tiles
	action = editor.actionQueue->createAction(batchAction.get());
	for (Tile* tile : tmp_storage) {
		const Position old_pos = tile->getPosition();
		const Position new_pos = rotateForFloor(old_pos);

		if (!new_pos.isValid()) {
			delete tile;
			continue;
		}
		// Create the duplicate dest tile, which will replace the old one later
		TileLocation* location = editor.map.createTileL(new_pos);
		Tile* old_dest_tile = location->get();
		std::unique_ptr<Tile> new_dest_tile;

		// Remap item IDs/orientations to the rotated frame
		rot.rotateTileItems(tile);

		if (g_settings.getInteger(Config::MERGE_MOVE) || !tile->ground) {
			// Move items
			if (old_dest_tile) {
				new_dest_tile = TileOperations::deepCopy(old_dest_tile, editor.map);
				ASSERT(new_dest_tile);
			} else {
				new_dest_tile = editor.map.allocator(location);
			}
			TileOperations::merge(new_dest_tile.get(), tile);
			// Removing old tile from memory since we merged it
			delete tile;
		} else {
			// Replace tile instead of just merge
			tile->setLocation(location);
			new_dest_tile.reset(tile);
		}

		action->addChange(std::make_unique<Change>(std::move(new_dest_tile)));
	}

	// Commit changes to the map
	batchAction->addAndCommitAction(std::move(action));

	// Create borders
	if (borderize_neighbors) {
		action = editor.actionQueue->createAction(batchAction.get());
		TileList borderize_tiles;
		// Go through all modified (selected) tiles (might be slow)
		for (Tile* tile : editor.selection) {
			bool add_me = false; // If this tile is touched
			Position pos = tile->getPosition();
			// Go through all neighbours
			Tile* t;
			t = editor.map.getTile(pos.x - 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x + 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x + 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x - 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x + 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x - 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = editor.map.getTile(pos.x + 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			if (add_me) {
				borderize_tiles.push_back(tile);
			}
		}
		// Remove duplicates
		borderize_tiles.sort();
		borderize_tiles.unique();
		// Do le borders!
		for (Tile* tile : borderize_tiles) {
			if (tile->ground) {
				if (tile->ground->getGroundBrush()) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);

					if (doborders) {
						TileOperations::borderize(new_tile.get(), &editor.map);
					}

					TileOperations::wallize(new_tile.get(), &editor.map);
					TileOperations::tableize(new_tile.get(), &editor.map);
					TileOperations::carpetize(new_tile.get(), &editor.map);
					if (tile->ground->isSelected()) {
						TileOperations::selectGround(new_tile.get());
					}

					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				}
			}
		}
		// Commit changes to map
		batchAction->addAndCommitAction(std::move(action));
	}

	// Unlike moving, rotation changes internal border/wall orientations, so the
	// rotated tiles themselves must be rebuilt.
	//
	// Walls, tables and carpets keep their orientation purely through their item
	// id, and a wall set normally defines a single "corner" sprite
	// (WALL_NORTHWEST_DIAGONAL) with no item for the other three diagonal
	// alignments. The in-place alignment remap in RotationUtility therefore
	// cannot turn a corner into its rotated counterpart: the corner sprite would
	// stay put while the straight walls around it rotate, leaving a broken joint.
	// So we ALWAYS re-run their auto-border here (mirroring CopyBuffer::rotate),
	// independent of automagic, letting the rotated neighbour geometry pick the
	// correct piece. Ground borders, on the other hand, still follow automagic.
	{
		action = editor.actionQueue->createAction(batchAction.get());
		for (Tile* tile : editor.selection) {
			if (!tile) {
				continue;
			}

			Tile* map_tile = editor.map.getTile(tile->getPosition());
			if (!map_tile) {
				continue;
			}

			const bool has_ground_brush = map_tile->ground && map_tile->ground->getGroundBrush();
			const bool want_ground_borders = create_borders && doborders && has_ground_brush;
			const bool has_oriented_item = map_tile->hasWall() || map_tile->hasTable() || map_tile->hasCarpet();
			if (!want_ground_borders && !has_oriented_item) {
				continue;
			}

			std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(map_tile, editor.map);
			if (want_ground_borders) {
				TileOperations::borderize(new_tile.get(), &editor.map);
			}
			TileOperations::wallize(new_tile.get(), &editor.map);
			TileOperations::tableize(new_tile.get(), &editor.map);
			TileOperations::carpetize(new_tile.get(), &editor.map);
			if (map_tile->ground && map_tile->ground->isSelected()) {
				TileOperations::selectGround(new_tile.get());
			}
			action->addChange(std::make_unique<Change>(std::move(new_tile)));
		}
		batchAction->addAndCommitAction(std::move(action));
	}

	// Store the action for undo
	editor.addBatch(std::move(batchAction));
	editor.selection.updateSelectionCount();
}

void SelectionOperations::destroySelection(Editor& editor) {
	if (editor.selection.empty()) {
		g_gui.SetStatusText("No selected items to delete.");
	} else {
		int tile_count = 0;
		int item_count = 0;
		PositionList tilestoborder;

		// Global toggle (Edit > Other Options > Delete Removes Zone Flags): wipe the
		// zone mapflags from tiles whose ground is deleted, so erasing an area does
		// not leave orphaned PZ/PVP markers behind.
		const bool remove_zones = g_settings.getBoolean(Config::DELETE_REMOVES_ZONES);
		// World Boss entra junto: apagar o chao de uma arena e deixar o 0x40 orfao
		// faria o servidor otimizar efeitos num lugar que nao existe mais.
		constexpr uint32_t ZONE_MAPFLAGS = TILESTATE_PROTECTIONZONE | TILESTATE_NOPVP | TILESTATE_NOLOGOUT | TILESTATE_PVPZONE | TILESTATE_WORLDBOSS;
		int zone_count = 0;

		std::unique_ptr<BatchAction> batch = editor.actionQueue->createBatch(ACTION_DELETE_TILES);
		std::unique_ptr<Action> action = editor.actionQueue->createAction(batch.get());

		for (Tile* tile : editor.selection) {
			tile_count++;

			std::unique_ptr<Tile> newtile = TileOperations::deepCopy(tile, editor.map);

			auto tile_selection = TileOperations::popSelectedItems(newtile.get());
			for (auto& item : tile_selection) {
				++item_count;
				// Items are deleted when the unique_ptr in tile_selection goes out of scope
			}

			if (newtile->creature && newtile->creature->isSelected()) {
				newtile->creature.reset();
			}

			if (newtile->spawn && newtile->spawn->isSelected()) {
				newtile->spawn.reset();
			}

			if (remove_zones && !newtile->ground && (newtile->getMapFlags() & ZONE_MAPFLAGS) != 0) {
				newtile->unsetMapFlags(ZONE_MAPFLAGS);
				++zone_count;
			}

			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				for (int y = -1; y <= 1; y++) {
					for (int x = -1; x <= 1; x++) {
						tilestoborder.push_back(
							Position(tile->getX() + x, tile->getY() + y, tile->getZ())
						);
					}
				}
			}
			action->addChange(std::make_unique<Change>(std::move(newtile)));
		}

		batch->addAndCommitAction(std::move(action));

		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			// Remove duplicates
			tilestoborder.sort();
			tilestoborder.unique();

			action = editor.actionQueue->createAction(batch.get());
			for (const Position& pos : tilestoborder) {
				TileLocation* location = editor.map.createTileL(pos);
				Tile* tile = location->get();

				if (tile) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
					TileOperations::borderize(new_tile.get(), &editor.map);
					TileOperations::wallize(new_tile.get(), &editor.map);
					TileOperations::tableize(new_tile.get(), &editor.map);
					TileOperations::carpetize(new_tile.get(), &editor.map);
					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				} else {
					std::unique_ptr<Tile> new_tile = editor.map.allocator(location);
					TileOperations::borderize(new_tile.get(), &editor.map);
					if (!new_tile->empty()) {
						action->addChange(std::make_unique<Change>(std::move(new_tile)));
					}
				}
			}

			batch->addAndCommitAction(std::move(action));
		}

		editor.addBatch(std::move(batch));
		wxString ss;
		ss << "Deleted " << tile_count << " tile" << (tile_count > 1 ? "s" : "") << " (" << item_count << " item" << (item_count > 1 ? "s" : "") << ")";
		if (zone_count > 0) {
			ss << ", cleared zones on " << zone_count << " tile" << (zone_count > 1 ? "s" : "");
		}
		g_gui.SetStatusText(ss);
	}
}
