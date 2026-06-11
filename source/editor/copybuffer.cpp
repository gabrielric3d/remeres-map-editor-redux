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

#include "app/main.h"

#include "editor/copybuffer.h"
#include "editor/editor.h"
#include "editor/operations/copy_operations.h"
#include "editor/operations/rotation_utility.h"
#include "ui/gui.h"
#include "game/creature.h"
#include "map/tile_operations.h"

CopyBuffer::CopyBuffer() :
	tiles(std::make_unique<BaseMap>()) {
	;
}

size_t CopyBuffer::GetTileCount() {
	return tiles ? (size_t)tiles->size() : 0;
}

BaseMap& CopyBuffer::getBufferMap() {
	ASSERT(tiles);
	return *tiles;
}

CopyBuffer::~CopyBuffer() {
	clear();
}

Position CopyBuffer::getPosition() const {
	ASSERT(tiles);
	return copyPos;
}

void CopyBuffer::setPosition(const Position& pos) {
	copyPos = pos;
}

void CopyBuffer::clear() {
	tiles.reset();
}

void CopyBuffer::copy(Editor& editor, int floor) {
	CopyOperations::copy(editor, *this, floor);
}

void CopyBuffer::cut(Editor& editor, int floor) {
	CopyOperations::cut(editor, *this, floor);
}

void CopyBuffer::paste(Editor& editor, const Position& toPosition) {
	CopyOperations::paste(editor, *this, toPosition);
}

bool CopyBuffer::canPaste() const {
	return tiles && tiles->size() != 0;
}

void CopyBuffer::rotate(int quarterTurns) {
	if (!tiles || tiles->size() == 0) {
		return;
	}

	RotationUtility rot(quarterTurns);
	if (rot.isIdentity()) {
		return;
	}

	// Find bounding box
	bool hasPos = false;
	Position minPos;
	Position maxPos;

	for (MapIterator it = tiles->begin(); it != tiles->end(); ++it) {
		Tile* tile = (*it).get();
		if (!tile) {
			continue;
		}

		const Position pos = tile->getPosition();
		if (!hasPos) {
			minPos = pos;
			maxPos = pos;
			hasPos = true;
		} else {
			minPos.x = std::min(minPos.x, pos.x);
			minPos.y = std::min(minPos.y, pos.y);
			maxPos.x = std::max(maxPos.x, pos.x);
			maxPos.y = std::max(maxPos.y, pos.y);
		}
	}

	if (!hasPos) {
		return;
	}

	const int width = maxPos.x - minPos.x + 1;
	const int height = maxPos.y - minPos.y + 1;

	// Deep copy all tiles, rotate items, compute new positions
	struct PendingTile {
		std::unique_ptr<Tile> tile;
		Position pos;
	};

	std::vector<PendingTile> rotatedTiles;
	rotatedTiles.reserve(static_cast<size_t>(tiles->size()));

	Position newMinPos = Position(0xFFFF, 0xFFFF, copyPos.z);

	for (MapIterator it = tiles->begin(); it != tiles->end(); ++it) {
		Tile* oldTile = (*it).get();
		if (!oldTile) {
			continue;
		}

		auto rotatedTile = TileOperations::deepCopy(oldTile, *tiles);
		const Position newPos = rot.rotatePosition(oldTile->getPosition(), minPos, width, height);

		rot.rotateTileItems(rotatedTile.get());

		newMinPos.x = std::min(newMinPos.x, newPos.x);
		newMinPos.y = std::min(newMinPos.y, newPos.y);

		rotatedTiles.push_back(PendingTile { std::move(rotatedTile), newPos });
	}

	// Clear old tiles and insert rotated ones
	tiles->clear(true);

	for (PendingTile& entry : rotatedTiles) {
		if (!entry.tile) {
			continue;
		}
		TileLocation* location = tiles->createTileL(entry.pos);
		entry.tile->setLocation(location);
		tiles->setTile(entry.pos, std::move(entry.tile));
	}

	// Ensure walls are consistent with their new neighbors
	for (MapIterator it = tiles->begin(); it != tiles->end(); ++it) {
		Tile* tile = (*it).get();
		if (tile && tile->hasWall()) {
			TileOperations::wallize(tile, tiles.get());
		}
	}

	copyPos.x = newMinPos.x;
	copyPos.y = newMinPos.y;
}
