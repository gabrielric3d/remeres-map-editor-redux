#include "rendering/core/sprite_archive.h"

#include "app/definitions.h"
#include "io/filehandle.h"

#include <algorithm>
#include <format>
#include <utility>
#include <wx/filename.h>
#include <wx/string.h>

namespace {
	constexpr uint32_t kSpriteDataOffset = 3;

	// "SCAT" read as a little-endian u32, matching how the client and the
	// fragmenter tools write it.
	constexpr uint32_t kCatalogMagic = 0x54414353;
	// v2 appends each fragment's expected size after its filename; v1 stops right
	// after the name. Every field before that is identical, so one loop reads both.
	constexpr uint32_t kCatalogVersionMin = 1;
	constexpr uint32_t kCatalogVersionMax = 2;
	// Fragment header: signature + sprite count, i.e. where its offset table begins.
	constexpr uint32_t kFragmentHeaderSize = 8;

	bool readSpriteCount(FileReadHandle& file, bool is_extended, uint32_t& sprite_count) {
		if (is_extended) {
			return file.getU32(sprite_count);
		}

		uint16_t compact_count = 0;
		if (!file.getU16(compact_count)) {
			return false;
		}
		sprite_count = compact_count;
		return true;
	}

	bool readSpriteOffsets(FileReadHandle& file, uint32_t sprite_count, std::vector<uint32_t>& offsets) {
		offsets.assign(static_cast<size_t>(sprite_count) + 1, 0);
		for (uint32_t sprite_id = 1; sprite_id <= sprite_count; ++sprite_id) {
			if (!file.getU32(offsets[sprite_id])) {
				return false;
			}
		}
		return true;
	}
}

SpriteArchive::SpriteArchive(std::string filename, bool is_extended, uint32_t sprite_count, std::vector<uint32_t> sprite_offsets, std::vector<Fragment> fragments) :
	filename_(std::move(filename)),
	is_extended_(is_extended),
	sprite_count_(sprite_count),
	sprite_offsets_(std::move(sprite_offsets)),
	fragments_(std::move(fragments)) {
}

wxFileName SpriteArchive::resolveCatalogPath(const wxFileName& sprites_path) {
	// Every branch checks the file is actually there: callers treat a valid
	// wxFileName as "a catalog exists here", so returning an unchecked path would
	// make sourceExists() approve a client with no sprites at all.
	if (sprites_path.GetExt().IsSameAs("cat", false)) {
		return sprites_path.FileExists() ? sprites_path : wxFileName {};
	}

	wxFileName catalog(sprites_path);
	catalog.SetExt("cat");
	if (catalog.FileExists()) {
		return catalog;
	}

	return {};
}

bool SpriteArchive::sourceExists(const wxFileName& sprites_path) {
	return sprites_path.FileExists() || resolveCatalogPath(sprites_path).IsOk();
}

bool SpriteArchive::readSourceSignature(const wxFileName& sprites_path, uint32_t& signature) {
	signature = 0;

	const auto catalog_path = resolveCatalogPath(sprites_path);
	const auto& source = catalog_path.IsOk() ? catalog_path : sprites_path;

	FileReadHandle file(source.GetFullPath().ToStdString());
	if (!file.isOk()) {
		return false;
	}

	if (!catalog_path.IsOk()) {
		return file.getU32(signature);
	}

	// Catalog header: magic, version, signature.
	uint32_t magic = 0;
	uint32_t version = 0;
	if (!file.getU32(magic) || !file.getU32(version) || !file.getU32(signature)) {
		return false;
	}
	return magic == kCatalogMagic;
}

namespace {
	// Reads the catalog and every fragment's offset table, flattening them into a
	// single id-indexed table. Sprite pixels are NOT read here: readCompressed
	// still pulls them one at a time, which is the whole point of splitting a
	// ~1 GB .spr in the first place.
	bool loadFragmentedArchive(const wxFileName& catalog_path, uint32_t& sprite_count, std::vector<uint32_t>& offsets, std::vector<SpriteArchive::Fragment>& fragments, wxString& error, std::vector<std::string>& warnings) {
		FileReadHandle catalog(catalog_path.GetFullPath().ToStdString());
		if (!catalog.isOk()) {
			error = wxString::FromUTF8(std::format("Failed to open sprite catalog {} for reading: {}", catalog_path.GetFullPath().utf8_string(), catalog.getErrorMessage()));
			return false;
		}

		uint32_t magic = 0;
		uint32_t version = 0;
		uint32_t signature = 0;
		uint32_t file_count = 0;
		if (!catalog.getU32(magic) || !catalog.getU32(version) || !catalog.getU32(signature) || !catalog.getU32(sprite_count) || !catalog.getU32(file_count)) {
			error = "Failed to read sprite catalog header.";
			return false;
		}
		(void)signature;

		if (magic != kCatalogMagic) {
			error = wxString::FromUTF8(std::format("{} is not a sprite catalog (bad magic).", catalog_path.GetFullPath().utf8_string()));
			return false;
		}
		if (version < kCatalogVersionMin || version > kCatalogVersionMax) {
			error = wxString::FromUTF8(std::format("Unsupported sprite catalog version {}.", version));
			return false;
		}
		if (sprite_count > MAX_SPRITES) {
			error = wxString::FromUTF8(std::format("Sprite count {} exceeds MAX_SPRITES={}.", sprite_count, MAX_SPRITES));
			return false;
		}
		if (file_count == 0) {
			error = "Sprite catalog lists no fragment files.";
			return false;
		}

		struct CatalogEntry {
			uint32_t start_id = 0;
			uint32_t end_id = 0;
			uint32_t sprites_offset = 0;
			uint32_t file_size = 0;
			std::string filename;
		};

		std::vector<CatalogEntry> entries(file_count);
		for (uint32_t i = 0; i < file_count; ++i) {
			auto& entry = entries[i];
			if (!catalog.getU32(entry.start_id) || !catalog.getU32(entry.end_id) || !catalog.getU32(entry.sprites_offset) || !catalog.getString(entry.filename)) {
				error = "Failed to read sprite catalog entries.";
				return false;
			}
			if (version >= 2 && !catalog.getU32(entry.file_size)) {
				error = "Failed to read sprite catalog entries.";
				return false;
			}
			if (entry.end_id < entry.start_id) {
				error = wxString::FromUTF8(std::format("Sprite catalog has an inverted range {}-{} for '{}'.", entry.start_id, entry.end_id, entry.filename));
				return false;
			}
		}

		std::sort(entries.begin(), entries.end(), [](const CatalogEntry& a, const CatalogEntry& b) {
			return a.start_id < b.start_id;
		});

		offsets.assign(static_cast<size_t>(sprite_count) + 1, 0);
		fragments.clear();
		fragments.reserve(file_count);

		const auto base_dir = catalog_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);

		for (const auto& entry : entries) {
			// Catalog names are relative to the catalog and use '/' regardless of
			// platform; wxFileName parses that fine on Windows too.
			wxFileName fragment_path(base_dir + wxString::FromUTF8(entry.filename));
			fragment_path.Normalize(wxPATH_NORM_DOTS);
			const auto fragment_full = fragment_path.GetFullPath().ToStdString();

			FileReadHandle fragment(fragment_full);
			if (!fragment.isOk()) {
				error = wxString::FromUTF8(std::format("Failed to open sprite fragment {}: {}", fragment_full, fragment.getErrorMessage()));
				return false;
			}

			// A fragment cut short by an interrupted copy or a half-applied update
			// would otherwise feed garbage offsets to the RLE decoder. v1 catalogs
			// leave file_size at 0 and skip the check.
			if (entry.file_size != 0 && fragment.size() != static_cast<size_t>(entry.file_size)) {
				error = wxString::FromUTF8(std::format("Sprite fragment {} is {} bytes but the catalog expects {} - truncated or stale.", fragment_full, fragment.size(), entry.file_size));
				return false;
			}

			uint32_t fragment_signature = 0;
			uint32_t fragment_count = 0;
			if (!fragment.getU32(fragment_signature) || !fragment.getU32(fragment_count)) {
				error = wxString::FromUTF8(std::format("Failed to read header of sprite fragment {}.", fragment_full));
				return false;
			}

			const uint32_t expected_count = entry.end_id - entry.start_id + 1;
			if (fragment_count != expected_count) {
				error = wxString::FromUTF8(std::format("Sprite fragment {} holds {} sprites but the catalog expects {}.", fragment_full, fragment_count, expected_count));
				return false;
			}

			const uint32_t table_offset = entry.sprites_offset != 0 ? entry.sprites_offset : kFragmentHeaderSize;
			if (!fragment.seek(table_offset)) {
				error = wxString::FromUTF8(std::format("Failed to seek the offset table of sprite fragment {}.", fragment_full));
				return false;
			}

			// Fragment offset tables are always u32, independent of is_extended:
			// the format fixes them that way, and the client reads them with
			// getU32() unconditionally.
			for (uint32_t local = 0; local < fragment_count; ++local) {
				uint32_t sprite_offset = 0;
				if (!fragment.getU32(sprite_offset)) {
					error = wxString::FromUTF8(std::format("Failed to read the offset table of sprite fragment {}.", fragment_full));
					return false;
				}
				const uint32_t sprite_id = entry.start_id + local;
				if (sprite_id >= 1 && sprite_id <= sprite_count) {
					offsets[sprite_id] = sprite_offset;
				}
			}

			fragments.push_back(SpriteArchive::Fragment { entry.start_id, entry.end_id, fragment_full });
		}

		if (sprite_count == 0) {
			warnings.push_back("Sprite catalog contains zero sprites.");
		}

		return true;
	}
}

std::shared_ptr<SpriteArchive> SpriteArchive::load(const wxFileName& path, bool is_extended, wxString& error, std::vector<std::string>& warnings) {
	// A catalog next to (or in place of) the .spr means the set was fragmented.
	if (const auto catalog_path = resolveCatalogPath(path); catalog_path.IsOk()) {
		uint32_t sprite_count = 0;
		std::vector<uint32_t> offsets;
		std::vector<Fragment> fragments;
		if (!loadFragmentedArchive(catalog_path, sprite_count, offsets, fragments, error, warnings)) {
			return nullptr;
		}
		return std::shared_ptr<SpriteArchive>(new SpriteArchive(catalog_path.GetFullPath().ToStdString(), is_extended, sprite_count, std::move(offsets), std::move(fragments)));
	}

	FileReadHandle file(path.GetFullPath().ToStdString());
	if (!file.isOk()) {
		error = wxString::FromUTF8(std::format("Failed to open {} for reading: {}", path.GetFullPath().utf8_string(), file.getErrorMessage()));
		return nullptr;
	}

	uint32_t signature = 0;
	uint32_t sprite_count = 0;
	if (!file.getU32(signature) || !readSpriteCount(file, is_extended, sprite_count)) {
		error = "Failed to read sprites header.";
		return nullptr;
	}
	(void)signature;
	if (sprite_count > MAX_SPRITES) {
		error = wxString::FromUTF8(std::format("Sprite count {} exceeds MAX_SPRITES={}.", sprite_count, MAX_SPRITES));
		return nullptr;
	}

	std::vector<uint32_t> offsets;
	if (!readSpriteOffsets(file, sprite_count, offsets)) {
		error = "Failed to read sprites index table.";
		return nullptr;
	}

	if (sprite_count == 0) {
		warnings.push_back("Sprite archive contains zero sprites.");
	}

	return std::shared_ptr<SpriteArchive>(new SpriteArchive(path.GetFullPath().ToStdString(), is_extended, sprite_count, std::move(offsets)));
}

const std::string* SpriteArchive::fileForSprite(uint32_t sprite_id) const {
	if (fragments_.empty()) {
		return &filename_;
	}

	// Ranges are sorted and disjoint, so the only candidate is the last fragment
	// whose start_id does not exceed the id.
	auto it = std::upper_bound(fragments_.begin(), fragments_.end(), sprite_id, [](uint32_t value, const Fragment& fragment) {
		return value < fragment.start_id;
	});
	if (it == fragments_.begin()) {
		return nullptr;
	}
	--it;

	// Gaps between fragments are legal, so an id can land past the end of one.
	if (sprite_id > it->end_id) {
		return nullptr;
	}
	return &it->filename;
}

bool SpriteArchive::readCompressed(uint32_t sprite_id, std::unique_ptr<uint8_t[]>& target, uint16_t& size) const {
	size = 0;
	target.reset();

	if (sprite_id == 0) {
		return true;
	}
	if (sprite_id >= sprite_offsets_.size()) {
		return false;
	}

	const uint32_t offset = sprite_offsets_[sprite_id];
	if (offset == 0) {
		return true;
	}

	// For a fragmented set the offset is relative to the fragment holding this
	// id, not to the catalog.
	const std::string* source = fileForSprite(sprite_id);
	if (source == nullptr) {
		return false;
	}

	FileReadHandle file(*source);
	if (!file.isOk()) {
		return false;
	}
	if (!file.seek(offset + kSpriteDataOffset)) {
		return false;
	}

	uint16_t compressed_size = 0;
	if (!file.getU16(compressed_size)) {
		return false;
	}

	auto buffer = std::make_unique<uint8_t[]>(compressed_size);
	if (!file.getRAW(buffer.get(), compressed_size)) {
		return false;
	}

	size = compressed_size;
	target = std::move(buffer);
	return true;
}
