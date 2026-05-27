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
#include "io/iominimap.h"

#include "map/tile.h"
#include "map/map.h"
#include "game/item.h"
#include "game/items.h"
#include "editor/editor.h"
#include "editor/selection.h"
#include "io/filehandle.h"
#include "io/iomap_otbm.h"
#include "ui/gui.h"
#include "app/definitions.h"

#include <wx/image.h>
#include <wx/filename.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <zlib.h>

IOMinimap::IOMinimap(Editor* editor, MinimapExportFormat format, MinimapExportMode mode, bool updateLoadbar) :
	m_editor(editor),
	m_format(format),
	m_mode(mode),
	m_updateLoadbar(updateLoadbar) {
}

bool IOMinimap::saveMinimap(const std::string& directory, const std::string& name, int floor) {
	if (m_mode == MinimapExportMode::AllFloors || m_mode == MinimapExportMode::SelectedArea || m_mode == MinimapExportMode::AreaView) {
		floor = -1;
	} else if (m_mode == MinimapExportMode::GroundFloor) {
		floor = GROUND_LAYER;
	} else if (m_mode == MinimapExportMode::SpecificFloor) {
		if (floor < 0 || floor > MAP_MAX_LAYER) {
			floor = GROUND_LAYER;
		}
	}

	m_floor = floor;

	try {
		if (m_format == MinimapExportFormat::Otmm) {
			return saveOtmm(wxFileName(directory, name + ".otmm"));
		}

		switch (m_mode) {
			case MinimapExportMode::AllFloors:
			case MinimapExportMode::GroundFloor:
			case MinimapExportMode::SpecificFloor:
				return exportMinimap(directory, name);
			case MinimapExportMode::SelectedArea:
				return exportSelection(directory, name);
			case MinimapExportMode::AreaView:
				return exportAreaView(directory, name);
		}
	} catch (std::bad_alloc&) {
		m_error = "There is not enough memory available to complete the operation.";
		return false;
	}

	return true;
}

bool IOMinimap::exportMinimap(const std::string& directory, const std::string& name) {
	auto& map = m_editor->map;
	if (map.size() == 0) {
		return true;
	}

	int min_z = m_floor == -1 ? 0 : m_floor;
	int max_z = m_floor == -1 ? MAP_MAX_LAYER : m_floor;

	struct FloorBounds {
		int min_x = MAP_MAX_WIDTH + 1;
		int min_y = MAP_MAX_HEIGHT + 1;
		int max_x = 0;
		int max_y = 0;
		bool valid() const {
			return max_x >= min_x && max_y >= min_y;
		}
	};
	FloorBounds bounds[MAP_LAYERS];

	int tiles_iterated = 0;
	int total_tiles = static_cast<int>(map.size());

	for (auto it = map.begin(); it != map.end(); ++it) {
		auto tile = (*it).get();
		if (!tile || (!tile->ground && tile->items.empty())) {
			continue;
		}

		const auto& position = tile->getPosition();
		if (position.z < min_z || position.z > max_z) {
			continue;
		}

		auto& b = bounds[position.z];
		if (position.x < b.min_x) {
			b.min_x = position.x;
		}
		if (position.y < b.min_y) {
			b.min_y = position.y;
		}
		if (position.x > b.max_x) {
			b.max_x = position.x;
		}
		if (position.y > b.max_y) {
			b.max_y = position.y;
		}
	}

	wxString extension = m_format == MinimapExportFormat::Png ? "png" : "bmp";
	wxBitmapType type = m_format == MinimapExportFormat::Png ? wxBITMAP_TYPE_PNG : wxBITMAP_TYPE_BMP;

	FloorBounds global;
	if (m_uniformBounds) {
		for (int z = min_z; z <= max_z; z++) {
			const auto& b = bounds[z];
			if (!b.valid()) {
				continue;
			}
			if (b.min_x < global.min_x) global.min_x = b.min_x;
			if (b.min_y < global.min_y) global.min_y = b.min_y;
			if (b.max_x > global.max_x) global.max_x = b.max_x;
			if (b.max_y > global.max_y) global.max_y = b.max_y;
		}
		if (!global.valid()) {
			return true;
		}
	}

	for (int z = min_z; z <= max_z; z++) {
		const FloorBounds& b = m_uniformBounds ? global : bounds[z];
		if (!b.valid()) {
			continue;
		}
		if (!m_uniformBounds && !bounds[z].valid()) {
			continue;
		}

		int image_width = b.max_x - b.min_x + 1;
		int image_height = b.max_y - b.min_y + 1;
		size_t pixels_size = static_cast<size_t>(image_width) * image_height * PixelFormatRGB;

		uint8_t* pixels = new uint8_t[pixels_size];
		std::memset(pixels, 0, pixels_size);

		bool empty = true;
		for (auto it = map.begin(); it != map.end(); ++it) {
			auto tile = (*it).get();
			if (!tile || (!tile->ground && tile->items.empty())) {
				continue;
			}

			const auto& position = tile->getPosition();
			if (position.z != z) {
				continue;
			}

			if (m_updateLoadbar) {
				++tiles_iterated;
				if (tiles_iterated % 8192 == 0) {
					g_gui.SetLoadDone(int(tiles_iterated / double(total_tiles) * 90.0));
				}
			}

			uint8_t color = tile->getMiniMapColor();
			size_t index = (static_cast<size_t>(position.y - b.min_y) * image_width + (position.x - b.min_x)) * PixelFormatRGB;
			pixels[index] = (uint8_t)(static_cast<int>(color / 36) % 6 * 51);
			pixels[index + 1] = (uint8_t)(static_cast<int>(color / 6) % 6 * 51);
			pixels[index + 2] = (uint8_t)(color % 6 * 51);
			empty = false;
		}

		if (!empty || m_uniformBounds) {
			wxImage image(image_width, image_height, pixels, true);
			wxFileName file = wxString::Format("%s-%d.%s", name, z, extension);
			file.Normalize(wxPATH_NORM_ALL, directory);
			image.SaveFile(file.GetFullPath(), type);
		}

		delete[] pixels;
	}

	return true;
}

bool IOMinimap::exportAreaView(const std::string& directory, const std::string& name) {
	auto& map = m_editor->map;
	if (map.size() == 0) {
		return true;
	}

	int from_x = std::min(m_fromPos.x, m_toPos.x);
	int from_y = std::min(m_fromPos.y, m_toPos.y);
	int to_x = std::max(m_fromPos.x, m_toPos.x);
	int to_y = std::max(m_fromPos.y, m_toPos.y);

	int base_width = to_x - from_x + 1;
	int base_height = to_y - from_y + 1;
	int ground_z = GROUND_LAYER;

	wxString extension = m_format == MinimapExportFormat::Png ? "png" : "bmp";
	wxBitmapType type = m_format == MinimapExportFormat::Png ? wxBITMAP_TYPE_PNG : wxBITMAP_TYPE_BMP;

	int tiles_iterated = 0;
	int total_tiles = static_cast<int>(map.size());

	if (m_mergeFloors) {
		int total_offset = ground_z;
		int image_width = base_width + total_offset;
		int image_height = base_height + total_offset;
		size_t pixels_size = static_cast<size_t>(image_width) * image_height * PixelFormatRGB;

		uint8_t* pixels = new uint8_t[pixels_size];
		std::memset(pixels, 0, pixels_size);

		for (int z = ground_z; z >= 0; z--) {
			int offset = ground_z - z;
			int area_from_x = from_x - offset;
			int area_from_y = from_y - offset;
			int area_to_x = to_x - offset;
			int area_to_y = to_y - offset;

			for (auto it = map.begin(); it != map.end(); ++it) {
				auto tile = (*it).get();
				if (!tile || (!tile->ground && tile->items.empty())) {
					continue;
				}

				const auto& position = tile->getPosition();
				if (position.z != z) {
					continue;
				}

				if (position.x < area_from_x || position.x > area_to_x || position.y < area_from_y || position.y > area_to_y) {
					continue;
				}

				if (m_updateLoadbar) {
					++tiles_iterated;
					if (tiles_iterated % 8192 == 0) {
						g_gui.SetLoadDone(int(tiles_iterated / double(total_tiles) * 90.0));
					}
				}

				int img_x = position.x - area_from_x;
				int img_y = position.y - area_from_y;

				uint8_t color = tile->getMiniMapColor();
				size_t index = (static_cast<size_t>(img_y) * image_width + img_x) * PixelFormatRGB;
				pixels[index] = (uint8_t)(static_cast<int>(color / 36) % 6 * 51);
				pixels[index + 1] = (uint8_t)(static_cast<int>(color / 6) % 6 * 51);
				pixels[index + 2] = (uint8_t)(color % 6 * 51);
			}
		}

		wxImage image(image_width, image_height, pixels, true);
		wxFileName file = wxString::Format("%s-areaview.%s", name, extension);
		file.Normalize(wxPATH_NORM_ALL, directory);
		image.SaveFile(file.GetFullPath(), type);
		delete[] pixels;
	} else {
		for (int z = 0; z <= ground_z; z++) {
			int offset = m_uniformBounds ? 0 : (ground_z - z);
			int area_from_x = from_x - offset;
			int area_from_y = from_y - offset;
			int area_to_x = to_x - offset;
			int area_to_y = to_y - offset;
			int image_width = area_to_x - area_from_x + 1;
			int image_height = area_to_y - area_from_y + 1;
			size_t pixels_size = static_cast<size_t>(image_width) * image_height * PixelFormatRGB;

			uint8_t* pixels = new uint8_t[pixels_size];
			std::memset(pixels, 0, pixels_size);

			bool empty = true;
			for (auto it = map.begin(); it != map.end(); ++it) {
				auto tile = (*it).get();
				if (!tile || (!tile->ground && tile->items.empty())) {
					continue;
				}

				const auto& position = tile->getPosition();
				if (position.z != z) {
					continue;
				}

				if (position.x < area_from_x || position.x > area_to_x || position.y < area_from_y || position.y > area_to_y) {
					continue;
				}

				if (m_updateLoadbar) {
					++tiles_iterated;
					if (tiles_iterated % 8192 == 0) {
						g_gui.SetLoadDone(int(tiles_iterated / double(total_tiles) * 90.0));
					}
				}

				int img_x = position.x - area_from_x;
				int img_y = position.y - area_from_y;

				uint8_t color = tile->getMiniMapColor();
				size_t index = (static_cast<size_t>(img_y) * image_width + img_x) * PixelFormatRGB;
				pixels[index] = (uint8_t)(static_cast<int>(color / 36) % 6 * 51);
				pixels[index + 1] = (uint8_t)(static_cast<int>(color / 6) % 6 * 51);
				pixels[index + 2] = (uint8_t)(color % 6 * 51);
				empty = false;
			}

			if (!empty || m_uniformBounds) {
				wxImage image(image_width, image_height, pixels, true);
				wxFileName file = wxString::Format("%s-%d.%s", name, z, extension);
				file.Normalize(wxPATH_NORM_ALL, directory);
				image.SaveFile(file.GetFullPath(), type);
			}
			delete[] pixels;
		}
	}

	return true;
}

bool IOMinimap::exportSelection(const std::string& directory, const std::string& name) {
	int min_x = MAP_MAX_WIDTH + 1;
	int min_y = MAP_MAX_HEIGHT + 1;
	int min_z = MAP_MAX_LAYER + 1;
	int max_x = 0, max_y = 0, max_z = 0;

	const auto& selection = m_editor->selection;
	const auto& tiles = selection.getTiles();

	for (auto tile : tiles) {
		if (!tile || (!tile->ground && tile->items.empty())) {
			continue;
		}

		const auto& position = tile->getPosition();
		if (position.x < min_x) {
			min_x = position.x;
		}
		if (position.x > max_x) {
			max_x = position.x;
		}
		if (position.y < min_y) {
			min_y = position.y;
		}
		if (position.y > max_y) {
			max_y = position.y;
		}
		if (position.z < min_z) {
			min_z = position.z;
		}
		if (position.z > max_z) {
			max_z = position.z;
		}
	}

	int numtiles = (max_x - min_x) * (max_y - min_y);
	if (numtiles == 0) {
		m_error = "Selection is empty.";
		return false;
	}

	int image_width = max_x - min_x + 1;
	int image_height = max_y - min_y + 1;
	if (image_width > 2048 || image_height > 2048) {
		m_error = "Minimap size greater than 2048px.";
		return false;
	}

	size_t pixels_size = static_cast<size_t>(image_width) * image_height * PixelFormatRGB;
	uint8_t* pixels = new uint8_t[pixels_size];

	int tiles_iterated = 0;
	wxString extension = m_format == MinimapExportFormat::Png ? "png" : "bmp";
	wxBitmapType type = m_format == MinimapExportFormat::Png ? wxBITMAP_TYPE_PNG : wxBITMAP_TYPE_BMP;

	for (int z = min_z; z <= max_z; z++) {
		bool empty = true;
		std::memset(pixels, 0, pixels_size);
		for (auto tile : tiles) {
			if (tile->getZ() != z) {
				continue;
			}

			if (m_updateLoadbar) {
				++tiles_iterated;
				if (tiles_iterated % 8192 == 0) {
					g_gui.SetLoadDone(int(tiles_iterated / double(tiles.size()) * 90.0));
				}
			}

			if (!tile->ground && tile->items.empty()) {
				continue;
			}

			uint8_t color = tile->getMiniMapColor();
			size_t index = (static_cast<size_t>(tile->getY() - min_y) * image_width + (tile->getX() - min_x)) * PixelFormatRGB;
			pixels[index] = (uint8_t)(static_cast<int>(color / 36) % 6 * 51);
			pixels[index + 1] = (uint8_t)(static_cast<int>(color / 6) % 6 * 51);
			pixels[index + 2] = (uint8_t)(color % 6 * 51);
			empty = false;
		}

		if (!empty || m_uniformBounds) {
			wxImage image(image_width, image_height);
			image.SetData(pixels, true);
			wxFileName file = wxString::Format("%s-%d.%s", name, z, extension);
			file.Normalize(wxPATH_NORM_ALL, directory);
			image.SaveFile(file.GetFullPath(), type);
		}
	}

	delete[] pixels;
	return true;
}

void IOMinimap::readBlocks() {
	if (m_mode == MinimapExportMode::SelectedArea && (!m_editor || !m_editor->hasSelection())) {
		return;
	}

	bool selectionOnly = m_mode == MinimapExportMode::SelectedArea;
	readBlocksFromMap(m_editor->map, selectionOnly, 0, 90);
}

void IOMinimap::readBlocksFromMap(Map& map, bool selectionOnly, int progressBase, int progressSpan) {
	int tiles_iterated = 0;
	int total_tiles = static_cast<int>(map.size());

	for (auto it = map.begin(); it != map.end(); ++it) {
		auto tile = (*it).get();

		if (m_updateLoadbar) {
			++tiles_iterated;
			if (tiles_iterated % 8192 == 0 && total_tiles > 0) {
				int local = static_cast<int>(tiles_iterated / double(total_tiles) * progressSpan);
				g_gui.SetLoadDone(std::min(progressBase + local, progressBase + progressSpan));
			}
		}

		if (!tile || (!tile->ground && tile->items.empty())) {
			continue;
		}

		const auto& position = tile->getPosition();
		if (position.z < 0 || position.z > MAP_MAX_LAYER) {
			continue;
		}

		if (selectionOnly) {
			if (!tile->isSelected()) {
				continue;
			}
		} else if (m_floor != -1 && position.z != m_floor) {
			continue;
		}

		MinimapTile minimapTile;
		minimapTile.color = tile->getMiniMapColor();
		minimapTile.flags |= MinimapTileWasSeen;
		if (tile->isBlocking()) {
			minimapTile.flags |= MinimapTileNotWalkable;
		}

		uint16_t way_speed = 100;
		if (tile->ground) {
			way_speed = static_cast<uint16_t>(tile->ground->getDefinition().attribute(ItemAttributeKey::WaySpeed));
		}
		minimapTile.speed = static_cast<uint8_t>(std::min<int>(static_cast<int>(std::ceil(way_speed / 10.f)), 0xFF));

		auto& blocks = m_blocks[position.z];
		uint32_t index = getBlockIndex(position);
		auto inserted = blocks.try_emplace(index);
		MinimapBlock& block = inserted.first->second;
		// Later maps overwrite tiles that share an absolute position.
		block.updateTile(position.x, position.y, minimapTile);
	}
}

bool IOMinimap::saveOtmm(const wxFileName& file) {
	readBlocks();
	return writeOtmmFile(file);
}

bool IOMinimap::writeOtmmFile(const wxFileName& file) {
	try {
		FileWriteHandle writer(file.GetFullPath().ToStdString());
		if (!writer.isOk()) {
			m_error = "Could not open the OTMM file for writing.";
			return false;
		}

		uint32_t flags = 0;

		// The "OTMM 1.0" description string is serialized as a U16 length prefix
		// (2 bytes) followed by the 8 raw bytes. The full header therefore has a
		// fixed, deterministic size, so the data-start offset can be written
		// directly instead of seeking back to patch it (FileWriteHandle has no
		// seek support, unlike the original implementation).
		const std::string description = "OTMM 1.0";
		const uint16_t dataStart = static_cast<uint16_t>(4 + 2 + 2 + 4 + 2 + description.size());

		// header
		writer.addU32(OTMM_SIGNATURE);
		writer.addU16(dataStart);
		writer.addU16(OTMM_VERSION);
		writer.addU32(flags);

		// version 1 header
		writer.addString(description);

		const unsigned long blockSize = MMBLOCK_SIZE * MMBLOCK_SIZE * sizeof(MinimapTile);
		std::vector<uint8_t> buffer(compressBound(blockSize));
		constexpr int COMPRESS_LEVEL = 3;

		for (uint8_t z = 0; z <= MAP_MAX_LAYER; ++z) {
			for (auto& it : m_blocks[z]) {
				uint32_t index = it.first;
				auto& block = it.second;

				// write index pos
				uint16_t x = static_cast<uint16_t>((index % (65536 / MMBLOCK_SIZE)) * MMBLOCK_SIZE);
				uint16_t y = static_cast<uint16_t>((index / (65536 / MMBLOCK_SIZE)) * MMBLOCK_SIZE);
				writer.addU16(x);
				writer.addU16(y);
				writer.addU8(z);

				unsigned long len = buffer.size();
				int ret = compress2(buffer.data(), &len, reinterpret_cast<const uint8_t*>(block.getTiles().data()), blockSize, COMPRESS_LEVEL);
				if (ret != Z_OK) {
					m_error = "Failed to compress minimap block data.";
					return false;
				}
				writer.addU16(static_cast<uint16_t>(len));
				writer.addRAW(buffer.data(), len);
			}
			m_blocks[z].clear();
		}

		// end of file is an invalid pos
		writer.addU16(65535);
		writer.addU16(65535);
		writer.addU8(255);

		if (!writer.isOk()) {
			m_error = "An error occurred while writing the OTMM file.";
			return false;
		}

		writer.close();
	} catch (std::exception& e) {
		m_error = std::string("Failed to save OTMM minimap: ") + e.what();
		return false;
	}

	return true;
}

bool IOMinimap::mergeMaps(const std::vector<std::string>& otbm_files, const std::string& directory, const std::string& name) {
	if (otbm_files.empty()) {
		m_error = "No maps were selected.";
		return false;
	}

	// .otmm stores absolute coordinates; merging only makes sense across all
	// floors, ignoring any single-floor / selection / area-view filtering.
	m_floor = -1;

	const int total = static_cast<int>(otbm_files.size());
	// The progress bar runs 0..100. Reserve the final slice for compressing
	// and writing the .otmm file; the rest is split evenly across the maps,
	// and each map's slice is split between loading and block extraction.
	const int loadSpan = 92;

	for (int i = 0; i < total; ++i) {
		const std::string& file_path = otbm_files[i];

		wxFileName fn(wxString(file_path.c_str(), wxConvUTF8));

		MapVersion ver;
		if (!IOMapOTBM::getVersionInfo(fn, ver)) {
			m_error = "Could not read the OTBM header of: " + file_path;
			if (m_updateLoadbar) {
				g_gui.SetLoadScale(0, 100);
			}
			return false;
		}

		// Map i owns the global progress slice [slotBegin, slotEnd]; the first
		// half covers the OTBM load (which drives SetLoadDone internally), the
		// second half covers extracting the tiles into minimap blocks.
		int slotBegin = static_cast<int>(i / double(total) * loadSpan);
		int slotEnd = static_cast<int>((i + 1) / double(total) * loadSpan);
		int slotMid = slotBegin + (slotEnd - slotBegin) / 2;

		// Each map is loaded into its own standalone instance so the editor's
		// currently-open map is never touched. The instance is destroyed at
		// the end of the iteration, keeping peak memory to one map at a time.
		Map standalone;
		IOMapOTBM loader(ver);

		if (m_updateLoadbar) {
			// Confine the loader's internal 0..100 progress to this map's
			// load half of the global bar.
			g_gui.SetLoadScale(slotBegin, slotMid);
		}

		if (!loader.loadMap(standalone, fn)) {
			m_error = "Failed to load \"" + file_path + "\": " + loader.getError().ToStdString();
			if (m_updateLoadbar) {
				g_gui.SetLoadScale(0, 100);
			}
			return false;
		}

		if (m_updateLoadbar) {
			g_gui.SetLoadScale(0, 100);
		}

		readBlocksFromMap(standalone, false, slotMid, slotEnd - slotMid);
	}

	if (m_updateLoadbar) {
		g_gui.SetLoadDone(loadSpan);
	}

	return writeOtmmFile(wxFileName(directory, name + ".otmm"));
}
