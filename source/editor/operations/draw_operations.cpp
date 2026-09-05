//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "editor/operations/draw_operations.h"
#include "editor/editor.h"
#include "editor/action_queue.h"
#include "ui/gui.h"
#include "brushes/managers/doodad_preview_manager.h"
#include "brushes/brush.h"
#include "brushes/doodad/doodad_brush.h"
#include "brushes/ground/ground_brush.h"
#include "brushes/border/optional_border_brush.h"
#include "brushes/house/house_exit_brush.h"
#include "brushes/waypoint/waypoint_brush.h"
#include "brushes/wall/wall_brush.h"
#include "brushes/carpet/carpet_brush.h"
#include "brushes/table/table_brush.h"
#include "brushes/creature/creature_brush.h"
#include "brushes/spawn/spawn_brush.h"
#include "brushes/door/door_brush.h"
#include "brushes/camera/camera_path_brush.h"
#include "brushes/managers/brush_manager.h"
#include "map/map.h"
#include "map/tile.h"
#include "map/tile_operations.h"
#include "game/creature.h"
#include "game/creatures.h"
#include "game/spawn.h"
#include "app/settings.h"
#include <algorithm>
#include <random>
#include <unordered_set>

namespace {

	void drawDoodad(Editor& editor, DoodadBrush* brush, Position offset, bool alt, bool dodraw) {
		std::unique_ptr<BatchAction> batch = editor.actionQueue->createBatch(ACTION_DRAW);
		std::unique_ptr<Action> action = editor.actionQueue->createAction(batch.get());
		BaseMap* buffer_map = g_doodad_preview.GetBufferMap();

		Position delta_pos = offset - Position(0x8000, 0x8000, 0x8);
		PositionList tilestoborder;

		for (auto& tile_ptr : *buffer_map) {
			auto* buffer_tile = tile_ptr.get();
			Position pos = buffer_tile->getPosition() + delta_pos;
			if (!pos.isValid()) {
				continue;
			}

			auto* location = editor.map.createTileL(pos);
			auto* tile = location->get();

			// Ground replace mode: only place doodad on tiles with matching ground
			if (alt && editor.replace_brush && dodraw) {
				if (!tile) {
					continue;
				}
				GroundBrush* tile_ground = tile->getGroundBrush();
				if (tile_ground != editor.replace_brush) {
					continue;
				}
			}

			if (!dodraw) {
				if (tile) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
					brush->undraw(&editor.map, new_tile.get());
					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				}
				tilestoborder.push_back(pos);
				continue;
			}

			if (brush->placeOnBlocking() || alt) {
				if (tile) {
					bool place = true;
					if (!brush->placeOnDuplicate() && !alt) {
						for (const auto& item : tile->items) {
							if (brush->ownsItem(item.get())) {
								place = false;
								break;
							}
						}
					}
					if (place) {
						std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
						SelectionOperations::removeDuplicateWalls(buffer_tile, new_tile.get());
						SelectionOperations::doSurroundingBorders(brush, tilestoborder, buffer_tile, new_tile.get());
						TileOperations::merge(new_tile.get(), buffer_tile);
						action->addChange(std::make_unique<Change>(std::move(new_tile)));
					}
				} else {
					std::unique_ptr<Tile> new_tile(editor.map.allocator(location));
					SelectionOperations::removeDuplicateWalls(buffer_tile, new_tile.get());
					SelectionOperations::doSurroundingBorders(brush, tilestoborder, buffer_tile, new_tile.get());
					TileOperations::merge(new_tile.get(), buffer_tile);
					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				}
			} else {
				if (tile && !tile->isBlocking()) {
					bool place = true;
					if (!brush->placeOnDuplicate() && !alt) {
						for (const auto& item : tile->items) {
							if (brush->ownsItem(item.get())) {
								place = false;
								break;
							}
						}
					}
					if (place) {
						std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
						SelectionOperations::removeDuplicateWalls(buffer_tile, new_tile.get());
						SelectionOperations::doSurroundingBorders(brush, tilestoborder, buffer_tile, new_tile.get());
						TileOperations::merge(new_tile.get(), buffer_tile);
						action->addChange(std::make_unique<Change>(std::move(new_tile)));
					}
				}
			}
		}
		batch->addAndCommitAction(std::move(action));

		if (!tilestoborder.empty()) {
			action = editor.actionQueue->createAction(batch.get());

			// Remove duplicates
			tilestoborder.sort();
			tilestoborder.unique();

			for (const auto& pos : tilestoborder) {
				Tile* tile = editor.map.getTile(pos);
				if (tile) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
					TileOperations::borderize(new_tile.get(), &editor.map);
					TileOperations::wallize(new_tile.get(), &editor.map);
					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				}
			}
			batch->addAndCommitAction(std::move(action));
		}
		editor.addBatch(std::move(batch), 2);
	}

	template <typename T>
	void drawGroundOrEraserImpl(Editor& editor, T* brush, const PositionVector& tilestodraw, PositionVector& tilestoborder, bool alt, bool dodraw) {
		std::unique_ptr<BatchAction> batch = editor.actionQueue->createBatch(ACTION_DRAW);
		std::unique_ptr<Action> action = editor.actionQueue->createAction(batch.get());

		std::pair<bool, GroundBrush*> param_obj;
		std::pair<bool, GroundBrush*>* param = nullptr;
		if constexpr (std::is_same_v<T, GroundBrush>) {
			if (alt) {
				param_obj = editor.replace_brush
					? std::pair<bool, GroundBrush*> { false, editor.replace_brush }
					: std::pair<bool, GroundBrush*> { true, nullptr };
				param = &param_obj;
			}
		}

		for (const auto& drawPos : tilestodraw) {
			auto* location = editor.map.createTileL(drawPos);
			auto* tile = location->get();
			if (tile) {
				std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
				if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
					if (g_settings.getBoolean(Config::PRESERVE_MANUAL_BORDERS)) {
						TileOperations::cleanAutoBorders(new_tile.get());
					} else {
						TileOperations::cleanBorders(new_tile.get());
					}
				}

				if (dodraw) {
					brush->draw(&editor.map, new_tile.get(), param);
				} else {
					brush->undraw(&editor.map, new_tile.get());
					tilestoborder.push_back(drawPos);
				}
				action->addChange(std::make_unique<Change>(std::move(new_tile)));
			} else if (dodraw) {
				std::unique_ptr<Tile> new_tile(editor.map.allocator(location));
				brush->draw(&editor.map, new_tile.get(), param);
				action->addChange(std::make_unique<Change>(std::move(new_tile)));
			}
		}

		// Commit changes to map
		batch->addAndCommitAction(std::move(action));

		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			// Do borders!
			action = editor.actionQueue->createAction(batch.get());
			for (const auto& borderPos : tilestoborder) {
				auto* location = editor.map.createTileL(borderPos);
				auto* tile = location->get();
				if (tile) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
					if (brush->template is<EraserBrush>()) {
						TileOperations::wallize(new_tile.get(), &editor.map);
						TileOperations::tableize(new_tile.get(), &editor.map);
						TileOperations::carpetize(new_tile.get(), &editor.map);
					}
					TileOperations::borderize(new_tile.get(), &editor.map);
					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				} else {
					std::unique_ptr<Tile> new_tile(editor.map.allocator(location));
					if (brush->template is<EraserBrush>()) {
						// There are no carpets/tables/walls on empty tiles...
						// new_tile->wallize(map);
						// new_tile->tableize(map);
						// new_tile->carpetize(map);
					}
					TileOperations::borderize(new_tile.get(), &editor.map);
					if (!new_tile->empty()) {
						action->addChange(std::make_unique<Change>(std::move(new_tile)));
					}
				}
			}
			batch->addAndCommitAction(std::move(action));
		}

		editor.addBatch(std::move(batch), 2);
	}

	void drawGroundOrEraser(Editor& editor, GroundBrush* brush, const PositionVector& tilestodraw, PositionVector& tilestoborder, bool alt, bool dodraw) {
		drawGroundOrEraserImpl(editor, brush, tilestodraw, tilestoborder, alt, dodraw);
	}

	void drawGroundOrEraser(Editor& editor, EraserBrush* brush, const PositionVector& tilestodraw, PositionVector& tilestoborder, bool alt, bool dodraw) {
		drawGroundOrEraserImpl(editor, brush, tilestodraw, tilestoborder, alt, dodraw);
	}

	void drawWall(Editor& editor, WallBrush* brush, const PositionVector& tilestodraw, PositionVector& tilestoborder, bool alt, bool dodraw) {
		std::unique_ptr<BatchAction> batch = editor.actionQueue->createBatch(ACTION_DRAW);
		std::unique_ptr<Action> action = editor.actionQueue->createAction(batch.get());

		if (alt && dodraw) {
			// This is exempt from USE_AUTOMAGIC
			g_doodad_preview.GetBufferMap()->clear();
			BaseMap* draw_map = g_doodad_preview.GetBufferMap();

			for (const auto& drawPos : tilestodraw) {
				auto* location = editor.map.createTileL(drawPos);
				auto* tile = location->get();
				if (tile) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
					TileOperations::cleanWalls(new_tile.get(), brush);
					brush->draw(draw_map, new_tile.get(), nullptr);
					(void)draw_map->setTile(drawPos, std::move(new_tile));
				} else {
					std::unique_ptr<Tile> new_tile(editor.map.allocator(location));
					brush->draw(draw_map, new_tile.get(), nullptr);
					(void)draw_map->setTile(drawPos, std::move(new_tile));
				}
			}
			// Iterate over the map instead of tilestodraw to avoid duplicates!
			for (MapIterator it = draw_map->begin(); it != draw_map->end(); ++it) {
				Tile* tile = it->get();
				if (tile) {
					TileOperations::wallize(tile, draw_map);
					action->addChange(std::make_unique<Change>(std::unique_ptr<Tile>(tile)));
				}
			}
			draw_map->clear(false);
			// Commit
			batch->addAndCommitAction(std::move(action));
		} else {
			for (const auto& drawPos : tilestodraw) {
				auto* location = editor.map.createTileL(drawPos);
				auto* tile = location->get();
				if (tile) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
					// Wall cleaning is exempt from automagic
					TileOperations::cleanWalls(new_tile.get(), brush->as<WallBrush>());
					if (dodraw) {
						brush->draw(&editor.map, new_tile.get(), nullptr);
					} else {
						brush->undraw(&editor.map, new_tile.get());
					}
					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				} else if (dodraw) {
					std::unique_ptr<Tile> new_tile(editor.map.allocator(location));
					brush->draw(&editor.map, new_tile.get(), nullptr);
					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				}
			}

			// Commit changes to map
			batch->addAndCommitAction(std::move(action));

			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				// Do borders!
				action = editor.actionQueue->createAction(batch.get());
				for (const auto& borderPos : tilestoborder) {
					Tile* tile = editor.map.getTile(borderPos);
					if (tile) {
						std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
						TileOperations::wallize(new_tile.get(), &editor.map);
						// if(*tile == *new_tile) delete new_tile;
						action->addChange(std::make_unique<Change>(std::move(new_tile)));
					}
				}
				batch->addAndCommitAction(std::move(action));
			}
		}

		editor.addBatch(std::move(batch), 2);
	}

} // namespace

void DrawOperations::draw(Editor& editor, Position offset, bool alt, bool dodraw) {
	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		return;
	}

	if (brush->is<DoodadBrush>()) {
		drawDoodad(editor, brush->as<DoodadBrush>(), offset, alt, dodraw);
	} else if (brush->is<HouseExitBrush>()) {
		HouseExitBrush* house_exit_brush = brush->as<HouseExitBrush>();
		if (!house_exit_brush->canDraw(&editor.map, offset)) {
			return;
		}

		House* house = editor.map.houses.getHouse(house_exit_brush->getHouseID());
		if (!house) {
			return;
		}

		std::unique_ptr<BatchAction> batch = editor.actionQueue->createBatch(ACTION_DRAW);
		std::unique_ptr<Action> action = editor.actionQueue->createAction(batch.get());
		action->addChange(std::unique_ptr<Change>(Change::Create(house, offset)));
		batch->addAndCommitAction(std::move(action));
		editor.addBatch(std::move(batch), 2);
	} else if (brush->is<WaypointBrush>()) {
		WaypointBrush* waypoint_brush = brush->as<WaypointBrush>();
		if (!waypoint_brush->canDraw(&editor.map, offset)) {
			return;
		}

		Waypoint* waypoint = editor.map.waypoints.getWaypoint(waypoint_brush->getWaypoint());
		if (!waypoint || waypoint->pos == offset) {
			return;
		}

		std::unique_ptr<BatchAction> batch = editor.actionQueue->createBatch(ACTION_DRAW);
		std::unique_ptr<Action> action = editor.actionQueue->createAction(batch.get());
		action->addChange(std::unique_ptr<Change>(Change::Create(waypoint, offset)));
		batch->addAndCommitAction(std::move(action));
		editor.addBatch(std::move(batch), 2);
	} else if (brush->is<CameraPathBrush>()) {
		if (!brush->as<CameraPathBrush>()->canDraw(&editor.map, offset)) {
			return;
		}

		CameraPath* path = editor.map.camera_paths.getActivePath();
		if (!path) {
			return;
		}

		CameraPathsSnapshot snapshot = editor.map.camera_paths.snapshot();
		CameraPath* snap_path = nullptr;
		for (auto& p : snapshot.paths) {
			if (p.name == path->name) {
				snap_path = &p;
				break;
			}
		}
		if (!snap_path) {
			return;
		}

		CameraKeyframe key;
		key.pos = offset;
		key.duration = 1.0;
		key.speed = 0.0;
		key.zoom = 1.0;
		key.easing = CameraEasing::EaseInOut;
		snap_path->keyframes.push_back(key);
		snapshot.active_keyframe = static_cast<int>(snap_path->keyframes.size()) - 1;

		editor.ApplyCameraPathsSnapshot(snapshot, ACTION_DRAW);
	} else if (brush->is<WallBrush>()) {
		std::unique_ptr<BatchAction> batch = editor.actionQueue->createBatch(ACTION_DRAW);
		std::unique_ptr<Action> action = editor.actionQueue->createAction(batch.get());
		// This will only occur with a size 0, when clicking on a tile (not drawing)
		Tile* tile = editor.map.getTile(offset);
		std::unique_ptr<Tile> new_tile;
		if (tile) {
			new_tile = TileOperations::deepCopy(tile, editor.map);
		} else {
			new_tile = editor.map.allocator(editor.map.createTileL(offset));
		}

		if (dodraw) {
			bool b = true;
			brush->as<WallBrush>()->draw(&editor.map, new_tile.get(), &b);
		} else {
			brush->as<WallBrush>()->undraw(&editor.map, new_tile.get());
		}
		action->addChange(std::make_unique<Change>(std::move(new_tile)));
		batch->addAndCommitAction(std::move(action));
		editor.addBatch(std::move(batch), 2);
	} else if (brush->is<SpawnBrush>() || brush->is<CreatureBrush>()) {
		std::unique_ptr<BatchAction> batch = editor.actionQueue->createBatch(ACTION_DRAW);
		std::unique_ptr<Action> action = editor.actionQueue->createAction(batch.get());

		if (brush->is<SpawnBrush>() && dodraw) {
			int param = g_gui.GetBrushSize();

			// Track all tile changes locally so creature placement sees up-to-date state
			std::map<Position, std::unique_ptr<Tile>> changes;

			// Check if this is a new spawn (not overwriting an existing one)
			Tile* original_tile = editor.map.getTile(offset);
			const bool created_spawn = (original_tile == nullptr || original_tile->spawn == nullptr);

			// Create/copy the center tile and draw the spawn on it
			auto getOrCreateTile = [&](const Position& pos) -> Tile* {
				auto it = changes.find(pos);
				if (it != changes.end()) {
					return it->second.get();
				}
				Tile* existing = editor.map.getTile(pos);
				std::unique_ptr<Tile> copy;
				if (existing) {
					copy = TileOperations::deepCopy(existing, editor.map);
				} else {
					copy = editor.map.allocator(editor.map.createTileL(pos));
				}
				Tile* ptr = copy.get();
				changes.emplace(pos, std::move(copy));
				return ptr;
			};

			Tile* center_tile = getOrCreateTile(offset);
			brush->draw(&editor.map, center_tile, &param);

			// Distribute creatures from the spawn group (only for newly created spawns)
			if (created_spawn) {
				const auto& group = g_brush_manager.GetSpawnCreatureGroup();
				if (!group.empty()) {
					int spawn_time = g_settings.getInteger(Config::DEFAULT_SPAWNTIME);

					// Collect candidate positions within spawn radius
					std::vector<Position> positions;
					positions.reserve(static_cast<size_t>((param * 2 + 1) * (param * 2 + 1)));
					for (int dy = -param; dy <= param; ++dy) {
						for (int dx = -param; dx <= param; ++dx) {
							Position cpos(offset.x + dx, offset.y + dy, offset.z);
							if (cpos.isValid()) {
								positions.push_back(cpos);
							}
						}
					}

					// Shuffle for random distribution
					static std::mt19937 rng(std::random_device{}());
					std::shuffle(positions.begin(), positions.end(), rng);

					// Place each creature from the group
					size_t pos_index = 0;
					for (const auto& entry : group) {
						CreatureType* ctype = g_creatures[entry.name];
						if (!ctype) {
							continue;
						}

						for (int i = 0; i < entry.count; ++i) {
							Tile* target = nullptr;
							while (pos_index < positions.size()) {
								const Position& pos = positions[pos_index++];

								// Check candidate from local changes first, then from map
								Tile* candidate = nullptr;
								auto change_it = changes.find(pos);
								if (change_it != changes.end()) {
									candidate = change_it->second.get();
								} else {
									candidate = editor.map.getTile(pos);
								}

								// Validate: must have ground, not blocking, no existing creature
								if (!candidate || !candidate->ground) {
									continue;
								}
								if (candidate->isBlocking()) {
									continue;
								}
								if (candidate->creature) {
									continue;
								}
								if (candidate->isPZ() && !ctype->isNpc) {
									continue;
								}

								target = getOrCreateTile(pos);
								break;
							}

							if (!target) {
								break; // No more valid positions
							}

							target->creature = std::make_unique<Creature>(ctype);
							target->creature->setSpawnTime(spawn_time);
						}
					}
				}
			}

			// Add all changes to the action
			for (auto& [pos, tile] : changes) {
				action->addChange(std::make_unique<Change>(std::move(tile)));
			}
		} else {
			// CreatureBrush or SpawnBrush undraw
			Tile* tile = editor.map.getTile(offset);
			std::unique_ptr<Tile> new_tile;
			if (tile) {
				new_tile = TileOperations::deepCopy(tile, editor.map);
			} else {
				new_tile = editor.map.allocator(editor.map.createTileL(offset));
			}
			int param;
			if (!brush->is<CreatureBrush>()) {
				param = g_gui.GetBrushSize();
			}
			if (dodraw) {
				brush->draw(&editor.map, new_tile.get(), &param);
			} else {
				brush->undraw(&editor.map, new_tile.get());
			}
			action->addChange(std::make_unique<Change>(std::move(new_tile)));
		}

		batch->addAndCommitAction(std::move(action));
		editor.addBatch(std::move(batch), 2);
	}
}

void DrawOperations::draw(Editor& editor, const PositionVector& tilestodraw, bool alt, bool dodraw) {
	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		return;
	}

#ifdef __DEBUG__
	if (brush->is<GroundBrush>() || brush->is<WallBrush>()) {
		// Wrong function, end call
		return;
	}
#endif

	std::unique_ptr<Action> action = editor.actionQueue->createAction(ACTION_DRAW);

	if (brush->is<OptionalBorderBrush>()) {
		// We actually need to do borders, but on the same tiles we draw to
		for (const auto& drawPos : tilestodraw) {
			auto* location = editor.map.createTileL(drawPos);
			auto* tile = location->get();
			if (tile) {
				if (dodraw) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
					brush->draw(&editor.map, new_tile.get());
					TileOperations::borderize(new_tile.get(), &editor.map);
					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				} else if (!dodraw && tile->hasOptionalBorder()) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
					brush->undraw(&editor.map, new_tile.get());
					TileOperations::borderize(new_tile.get(), &editor.map);
					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				}
			} else if (dodraw) {
				std::unique_ptr<Tile> new_tile(editor.map.allocator(location));
				brush->draw(&editor.map, new_tile.get());
				TileOperations::borderize(new_tile.get(), &editor.map);
				if (new_tile->empty()) {
					continue;
				}
				action->addChange(std::make_unique<Change>(std::move(new_tile)));
			}
		}
		editor.addAction(std::move(action), 2);
	} else if (brush->is<SpawnBrush>() && dodraw) {
		// SpawnBrush needs special handling: spawn size as int parameter + creature group distribution
		int param = g_gui.GetBrushSize();
		std::map<Position, std::unique_ptr<Tile>> changes;
		const auto& group = g_brush_manager.GetSpawnCreatureGroup();

		auto getOrCreateTile = [&](const Position& pos) -> Tile* {
			auto it = changes.find(pos);
			if (it != changes.end()) {
				return it->second.get();
			}
			Tile* existing = editor.map.getTile(pos);
			std::unique_ptr<Tile> copy;
			if (existing) {
				copy = TileOperations::deepCopy(existing, editor.map);
			} else {
				copy = editor.map.allocator(editor.map.createTileL(pos));
			}
			Tile* ptr = copy.get();
			changes.emplace(pos, std::move(copy));
			return ptr;
		};

		for (const auto& drawPos : tilestodraw) {
			Tile* original_tile = editor.map.getTile(drawPos);
			const bool created_spawn = (original_tile == nullptr || original_tile->spawn == nullptr);

			Tile* new_tile = getOrCreateTile(drawPos);
			brush->draw(&editor.map, new_tile, &param);

			if (created_spawn && !group.empty()) {
				int spawn_time = g_settings.getInteger(Config::DEFAULT_SPAWNTIME);

				std::vector<Position> positions;
				positions.reserve(static_cast<size_t>((param * 2 + 1) * (param * 2 + 1)));
				for (int dy = -param; dy <= param; ++dy) {
					for (int dx = -param; dx <= param; ++dx) {
						Position cpos(drawPos.x + dx, drawPos.y + dy, drawPos.z);
						if (cpos.isValid()) {
							positions.push_back(cpos);
						}
					}
				}

				static std::mt19937 rng(std::random_device{}());
				std::shuffle(positions.begin(), positions.end(), rng);

				size_t pos_index = 0;
				for (const auto& entry : group) {
					CreatureType* ctype = g_creatures[entry.name];
					if (!ctype) {
						continue;
					}
					for (int i = 0; i < entry.count; ++i) {
						Tile* target = nullptr;
						while (pos_index < positions.size()) {
							const Position& pos = positions[pos_index++];
							Tile* candidate = nullptr;
							auto change_it = changes.find(pos);
							if (change_it != changes.end()) {
								candidate = change_it->second.get();
							} else {
								candidate = editor.map.getTile(pos);
							}
							if (!candidate || !candidate->ground || candidate->isBlocking() || candidate->creature) {
								continue;
							}
							if (candidate->isPZ() && !ctype->isNpc) {
								continue;
							}
							target = getOrCreateTile(pos);
							break;
						}
						if (!target) {
							break;
						}
						target->creature = std::make_unique<Creature>(ctype);
						target->creature->setSpawnTime(spawn_time);
					}
				}
			}
		}

		for (auto& [pos, tile] : changes) {
			action->addChange(std::make_unique<Change>(std::move(tile)));
		}
		editor.addAction(std::move(action), 2);
	} else {

		for (const auto& drawPos : tilestodraw) {
			auto* location = editor.map.createTileL(drawPos);
			auto* tile = location->get();
			if (tile) {
				std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
				if (dodraw) {
					brush->draw(&editor.map, new_tile.get(), &alt);
				} else {
					brush->undraw(&editor.map, new_tile.get());
				}
				action->addChange(std::make_unique<Change>(std::move(new_tile)));
			} else if (dodraw) {
				std::unique_ptr<Tile> new_tile(editor.map.allocator(location));
				brush->draw(&editor.map, new_tile.get(), &alt);
				action->addChange(std::make_unique<Change>(std::move(new_tile)));
			}
		}
		editor.addAction(std::move(action), 2);
	}
}

void DrawOperations::draw(Editor& editor, const PositionVector& tilestodraw, PositionVector& tilestoborder, bool alt, bool dodraw) {
	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		return;
	}

	if (brush->is<GroundBrush>()) {
		drawGroundOrEraser(editor, brush->as<GroundBrush>(), tilestodraw, tilestoborder, alt, dodraw);
	} else if (brush->is<EraserBrush>()) {
		drawGroundOrEraser(editor, brush->as<EraserBrush>(), tilestodraw, tilestoborder, alt, dodraw);
	} else if (brush->is<TableBrush>() || brush->is<CarpetBrush>()) {
		std::unique_ptr<BatchAction> batch = editor.actionQueue->createBatch(ACTION_DRAW);
		std::unique_ptr<Action> action = editor.actionQueue->createAction(batch.get());

		for (const auto& drawPos : tilestodraw) {
			auto* location = editor.map.createTileL(drawPos);
			auto* tile = location->get();
			if (tile) {
				std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
				if (dodraw) {
					brush->draw(&editor.map, new_tile.get(), nullptr);
				} else {
					brush->undraw(&editor.map, new_tile.get());
				}
				action->addChange(std::make_unique<Change>(std::move(new_tile)));
			} else if (dodraw) {
				std::unique_ptr<Tile> new_tile(editor.map.allocator(location));
				brush->draw(&editor.map, new_tile.get(), nullptr);
				action->addChange(std::make_unique<Change>(std::move(new_tile)));
			}
		}

		// Commit changes to map
		batch->addAndCommitAction(std::move(action));

		// Do borders!
		action = editor.actionQueue->createAction(batch.get());
		// When carpet interaction is disabled, restrict the border recalculation
		// to the brush being drawn so other carpets on the tile keep their shape.
		CarpetBrush* carpetOnlyBrush = nullptr;
		if (brush->is<CarpetBrush>() && g_settings.getBoolean(Config::DISABLE_CARPET_INTERACTION)) {
			carpetOnlyBrush = brush->as<CarpetBrush>();
		}
		for (const auto& borderPos : tilestoborder) {
			Tile* tile = editor.map.getTile(borderPos);
			if (brush->is<TableBrush>()) {
				if (tile && tile->hasTable()) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
					TileOperations::tableize(new_tile.get(), &editor.map);
					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				}
			} else if (brush->is<CarpetBrush>()) {
				if (tile && tile->hasCarpet()) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
					TileOperations::carpetize(new_tile.get(), &editor.map, carpetOnlyBrush);
					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				}
			}
		}
		batch->addAndCommitAction(std::move(action));

		editor.addBatch(std::move(batch), 2);
	} else if (brush->is<WallBrush>()) {
		drawWall(editor, brush->as<WallBrush>(), tilestodraw, tilestoborder, alt, dodraw);
	} else if (brush->is<DoorBrush>()) {
		std::unique_ptr<BatchAction> batch = editor.actionQueue->createBatch(ACTION_DRAW);
		std::unique_ptr<Action> action = editor.actionQueue->createAction(batch.get());
		DoorBrush* door_brush = brush->as<DoorBrush>();

		// Loop is kind of redundant since there will only ever be one index.
		for (const auto& drawPos : tilestodraw) {
			auto* location = editor.map.createTileL(drawPos);
			auto* tile = location->get();
			if (tile) {
				std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
				// Wall cleaning is exempt from automagic
				if (brush->is<WallBrush>()) {
					TileOperations::cleanWalls(new_tile.get(), brush->as<WallBrush>());
				}
				if (dodraw) {
					door_brush->draw(&editor.map, new_tile.get(), &alt);
				} else {
					door_brush->undraw(&editor.map, new_tile.get());
				}
				action->addChange(std::make_unique<Change>(std::move(new_tile)));
			} else if (dodraw) {
				std::unique_ptr<Tile> new_tile(editor.map.allocator(location));
				door_brush->draw(&editor.map, new_tile.get(), &alt);
				action->addChange(std::make_unique<Change>(std::move(new_tile)));
			}
		}

		// Commit changes to map
		batch->addAndCommitAction(std::move(action));

		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			// Do borders!
			action = editor.actionQueue->createAction(batch.get());
			for (const auto& borderPos : tilestoborder) {
				Tile* tile = editor.map.getTile(borderPos);
				if (tile) {
					std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
					TileOperations::wallize(new_tile.get(), &editor.map);
					// if(*tile == *new_tile) delete new_tile;
					action->addChange(std::make_unique<Change>(std::move(new_tile)));
				}
			}
			batch->addAndCommitAction(std::move(action));
		}

		editor.addBatch(std::move(batch), 2);
	} else {
		std::unique_ptr<Action> action = editor.actionQueue->createAction(ACTION_DRAW);
		for (const auto& drawPos : tilestodraw) {
			auto* location = editor.map.createTileL(drawPos);
			auto* tile = location->get();
			if (tile) {
				std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
				if (dodraw) {
					brush->draw(&editor.map, new_tile.get());
				} else {
					brush->undraw(&editor.map, new_tile.get());
				}
				action->addChange(std::make_unique<Change>(std::move(new_tile)));
			} else if (dodraw) {
				std::unique_ptr<Tile> new_tile(editor.map.allocator(location));
				brush->draw(&editor.map, new_tile.get());
				action->addChange(std::make_unique<Change>(std::move(new_tile)));
			}
		}
		editor.addAction(std::move(action), 2);
	}
}

void DrawOperations::eraseGroundWithBorders(Editor& editor, const PositionVector& positions, bool whole_tile) {
	// ACTION_DRAW (not ACTION_DELETE_TILES) so these batches merge with the brush-stroke
	// batches they interleave with while drawing on the floor below: the action queue only
	// merges consecutive batches of the same type, and one stroke should be one undo step.
	std::unique_ptr<BatchAction> batch = editor.actionQueue->createBatch(ACTION_DRAW);
	std::unique_ptr<Action> action = editor.actionQueue->createAction(batch.get());

	// 1) Remove the ground (and its auto-borders) from every footprint tile that has one.
	//    In whole_tile mode the eraser brush wipes the items as well, so tiles that only
	//    carry items (mountain walls on the floor below, decoration) count as erasable too.
	PositionVector erased;
	for (const auto& pos : positions) {
		Tile* tile = editor.map.getTile(pos);
		if (!tile) {
			continue;
		}
		if (!tile->hasGround() && !(whole_tile && g_brush_manager.eraser && !tile->items.empty())) {
			continue;
		}
		std::unique_ptr<Tile> new_tile = TileOperations::deepCopy(tile, editor.map);
		if (whole_tile && g_brush_manager.eraser) {
			// Same semantics as the Eraser brush, so ERASER_LEAVE_UNIQUE still protects
			// complex items (uniques, containers, ...) from being wiped by accident.
			g_brush_manager.eraser->undraw(&editor.map, new_tile.get());
		} else {
			if (g_settings.getBoolean(Config::PRESERVE_MANUAL_BORDERS)) {
				TileOperations::cleanAutoBorders(new_tile.get());
			} else {
				TileOperations::cleanBorders(new_tile.get());
			}
			new_tile->ground = nullptr;
		}
		action->addChange(std::make_unique<Change>(std::move(new_tile)));
		erased.push_back(pos);
	}
	if (erased.empty()) {
		return; // Nothing to remove on the target floors; drop the uncommitted batch.
	}
	batch->addAndCommitAction(std::move(action));

	// 2) Re-borderize the erased tiles and their 8 neighbors (deduped across overlapping
	//    neighborhoods) so the surrounding grounds reform their borders against the
	//    now-open hole. Forced on regardless of the global USE_AUTOMAGIC setting,
	//    matching the "as if auto-border is enabled" intent. The key carries z because
	//    the positions may span several floors (erase floors above/below).
	action = editor.actionQueue->createAction(batch.get());
	std::unordered_set<int64_t> border_seen;
	const auto borderKey = [](int x, int y, int z) -> int64_t {
		return (static_cast<int64_t>(z & 0xFF) << 48)
			| (static_cast<int64_t>(x & 0xFFFFFF) << 24)
			| static_cast<int64_t>(y & 0xFFFFFF);
	};
	for (const auto& pos : erased) {
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (!border_seen.insert(borderKey(pos.x + dx, pos.y + dy, pos.z)).second) {
					continue;
				}
				Tile* border_tile = editor.map.getTile(pos.x + dx, pos.y + dy, pos.z);
				if (border_tile) {
					std::unique_ptr<Tile> border_copy = TileOperations::deepCopy(border_tile, editor.map);
					TileOperations::borderize(border_copy.get(), &editor.map);
					action->addChange(std::make_unique<Change>(std::move(border_copy)));
				}
			}
		}
	}
	batch->addAndCommitAction(std::move(action));

	editor.addBatch(std::move(batch), 2);
}

bool DrawOperations::extraFloorEraseEnabled() {
	const bool above = g_settings.getBoolean(Config::ERASE_FLOORS_ABOVE_ENABLED)
		&& g_settings.getInteger(Config::ERASE_FLOORS_ABOVE_COUNT) > 0;
	const bool below = g_settings.getBoolean(Config::ERASE_FLOORS_BELOW_ENABLED)
		&& g_settings.getInteger(Config::ERASE_FLOORS_BELOW_COUNT) > 0;
	return above || below;
}

void DrawOperations::eraseExtraFloors(Editor& editor, const PositionVector& footprint) {
	if (footprint.empty() || !extraFloorEraseEnabled()) {
		return;
	}

	const auto floorCount = [](Config::Key enabled_key, Config::Key count_key) -> int {
		if (!g_settings.getBoolean(enabled_key)) {
			return 0;
		}
		return std::clamp(g_settings.getInteger(count_key), 0, MAP_MAX_LAYER);
	};
	const int floors_above = floorCount(Config::ERASE_FLOORS_ABOVE_ENABLED, Config::ERASE_FLOORS_ABOVE_COUNT);
	const int floors_below = floorCount(Config::ERASE_FLOORS_BELOW_ENABLED, Config::ERASE_FLOORS_BELOW_COUNT);

	// Lower z is physically above, higher z is below (floor 0 is the top of the map).
	PositionVector targets;
	targets.reserve(footprint.size() * static_cast<size_t>(floors_above + floors_below));
	for (const auto& pos : footprint) {
		for (int step = 1; step <= floors_above; ++step) {
			const int z = pos.z - step;
			if (z >= 0) {
				targets.emplace_back(pos.x, pos.y, z);
			}
		}
		for (int step = 1; step <= floors_below; ++step) {
			const int z = pos.z + step;
			if (z <= MAP_MAX_LAYER) {
				targets.emplace_back(pos.x, pos.y, z);
			}
		}
	}
	if (targets.empty()) {
		return;
	}

	eraseGroundWithBorders(editor, targets, g_settings.getBoolean(Config::ERASE_FLOORS_WHOLE_TILE));
}
