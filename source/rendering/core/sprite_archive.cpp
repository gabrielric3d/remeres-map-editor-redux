#include "rendering/core/sprite_archive.h"

#include "app/definitions.h"
#include "io/filehandle.h"
#include "rendering/core/image.h"
#include "util/json.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <utility>
#include <lzma.h>
#include <spdlog/spdlog.h>
#include <wx/filename.h>
#include <wx/string.h>

namespace {
	constexpr uint32_t kSpriteDataOffset = 3;

	// --- Folhas de sprite dos assets 12+/13 ---------------------------------
	// Cada arquivo .bmp.lzma e uma folha 384x384 com sprites lado a lado.
	constexpr int kSheetDimension = 384;
	constexpr int kSheetBytes = kSheetDimension * kSheetDimension * 4;
	constexpr int kBmpHeaderPadding = 122;
	// Teto de folhas decodificadas em memoria: 64 x 384x384x4 = ~36 MB.
	constexpr size_t kDecodedSheetCacheLimit = 64;
	constexpr std::array<uint8_t, 5> kProtobufSheetMagic { 0x70, 0x0A, 0xFA, 0x80, 0x24 };

	std::pair<int, int> protobufSourceDimensions(SpriteArchive::ProtobufSpriteLayout layout) {
		switch (layout) {
			case SpriteArchive::ProtobufSpriteLayout::OneByOne:
				return { 32, 32 };
			case SpriteArchive::ProtobufSpriteLayout::OneByTwo:
				return { 32, 64 };
			case SpriteArchive::ProtobufSpriteLayout::TwoByOne:
				return { 64, 32 };
			case SpriteArchive::ProtobufSpriteLayout::TwoByTwo:
				return { 64, 64 };
		}
		return { 32, 32 };
	}

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
	fragments_(std::move(fragments)),
	backend_(Backend::Legacy) {
}

SpriteArchive::SpriteArchive(std::string filename, uint32_t sprite_count, std::vector<ProtobufSheet> sheets, std::vector<int32_t> sheet_lookup) :
	filename_(std::move(filename)),
	sprite_count_(sprite_count),
	backend_(Backend::Protobuf),
	protobuf_sheets_(std::move(sheets)),
	protobuf_sheet_lookup_(std::move(sheet_lookup)) {
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

// ---------------------------------------------------------------------------
// Assets 12+/13: catalog-content.json + folhas .bmp.lzma
//
// Caminho inteiramente separado do .spr acima. O cliente novo nao guarda um
// blob RLE por sprite; guarda folhas BMP de 384x384 comprimidas em LZMA cru, e
// cada sprite e um recorte da folha. Por isso o resultado sai como RGBA pronto
// em readRGBA(), enquanto o .spr continua saindo comprimido em readCompressed().
// ---------------------------------------------------------------------------

std::shared_ptr<SpriteArchive> SpriteArchive::loadProtobuf(const wxFileName& catalog_path, wxString& error, std::vector<std::string>& warnings) {
	std::ifstream file(catalog_path.GetFullPath().ToStdString(), std::ios::in | std::ios::binary);
	if (!file.is_open()) {
		error = wxString::FromUTF8(std::format("Failed to open protobuf catalog {}.", catalog_path.GetFullPath().utf8_string()));
		return nullptr;
	}

	json::json document = json::json::parse(file, nullptr, false);
	if (document.is_discarded() || !document.is_array()) {
		error = "Invalid protobuf catalog-content.json document.";
		return nullptr;
	}

	std::vector<ProtobufSheet> sheets;
	uint32_t sprite_count = 0;
	for (const auto& entry : document) {
		if (!entry.is_object() || entry.value("type", std::string {}) != "sprite") {
			continue;
		}

		ProtobufSheet sheet;
		sheet.first_id = entry.value("firstspriteid", 0u);
		sheet.last_id = entry.value("lastspriteid", 0u);
		sheet.layout = static_cast<ProtobufSpriteLayout>(entry.value("spritetype", 0));
		sheet.path = wxFileName(catalog_path.GetPath(), wxString::FromUTF8(entry.value("file", std::string {}))).GetFullPath().ToStdString();
		if (sheet.last_id < sheet.first_id || sheet.path.empty()) {
			warnings.push_back("Skipping invalid protobuf sprite sheet entry in catalog-content.json.");
			continue;
		}
		if (sheet.last_id > MAX_SPRITES) {
			error = wxString::FromUTF8(std::format(
				"Protobuf sprite sheet {} exceeds MAX_SPRITES={} with last sprite id {}.",
				sheet.path,
				MAX_SPRITES,
				sheet.last_id
			));
			return nullptr;
		}

		sprite_count = std::max(sprite_count, sheet.last_id);
		sheets.push_back(std::move(sheet));
	}

	if (sheets.empty()) {
		error = "No protobuf sprite sheets were found in catalog-content.json.";
		return nullptr;
	}

	// Tabela direta id -> indice da folha. Vale a memoria: e consultada uma vez
	// por sprite carregado, e as faixas do catalogo nao sao contiguas.
	std::vector<int32_t> sheet_lookup(static_cast<size_t>(sprite_count) + 1, -1);
	for (size_t index = 0; index < sheets.size(); ++index) {
		for (uint32_t sprite_id = sheets[index].first_id; sprite_id <= sheets[index].last_id && sprite_id < sheet_lookup.size(); ++sprite_id) {
			sheet_lookup[sprite_id] = static_cast<int32_t>(index);
		}
	}

	return std::shared_ptr<SpriteArchive>(new SpriteArchive(catalog_path.GetFullPath().ToStdString(), sprite_count, std::move(sheets), std::move(sheet_lookup)));
}

ImageDimensions SpriteArchive::spriteDimensions(uint32_t sprite_id) const {
	if (backend_ != Backend::Protobuf || sprite_id == 0 || sprite_id >= protobuf_sheet_lookup_.size()) {
		return {};
	}

	const int32_t sheet_index = protobuf_sheet_lookup_[sprite_id];
	if (sheet_index < 0 || static_cast<size_t>(sheet_index) >= protobuf_sheets_.size()) {
		return {};
	}

	const auto [width, height] = protobufSourceDimensions(protobuf_sheets_[static_cast<size_t>(sheet_index)].layout);
	return ImageDimensions {
		static_cast<uint16_t>(width),
		static_cast<uint16_t>(height),
	};
}

bool SpriteArchive::loadSheetPixels(const ProtobufSheet& sheet) const {
	if (sheet.decoded_pixels) {
		return true;
	}

	std::ifstream file(sheet.path, std::ios::binary | std::ios::in);
	if (!file.is_open()) {
		spdlog::error("SpriteArchive: failed to open protobuf sprite sheet {}", sheet.path);
		return false;
	}

	file.seekg(0, std::ios::end);
	const std::streamsize size = file.tellg();
	if (size <= 0) {
		spdlog::error("SpriteArchive: protobuf sprite sheet {} is empty", sheet.path);
		return false;
	}

	file.seekg(0, std::ios::beg);
	std::vector<uint8_t> buffer(static_cast<size_t>(size));
	if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
		spdlog::error("SpriteArchive: failed to read protobuf sprite sheet {}", sheet.path);
		return false;
	}

	// Cabecalho CipSoft: zeros de padding, a assinatura, e um campo de tamanho
	// em varint antes do cabecalho LZMA propriamente dito.
	size_t position = 0;
	while (position < buffer.size() && buffer[position] == 0x00) {
		++position;
	}
	if (position >= buffer.size()) {
		spdlog::error("SpriteArchive: protobuf sprite sheet {} is missing the CipSoft header", sheet.path);
		return false;
	}

	if (position + kProtobufSheetMagic.size() > buffer.size()
		|| !std::equal(kProtobufSheetMagic.begin(), kProtobufSheetMagic.end(), buffer.begin() + static_cast<std::ptrdiff_t>(position))) {
		spdlog::error("SpriteArchive: protobuf sprite sheet {} has an invalid header", sheet.path);
		return false;
	}

	position += kProtobufSheetMagic.size();
	while (position < buffer.size() && (buffer[position++] & 0x80) == 0x80) {
	}
	if (position + 13 > buffer.size()) {
		spdlog::error("SpriteArchive: protobuf sprite sheet {} has an incomplete LZMA header", sheet.path);
		return false;
	}

	// LZMA1 cru: os parametros vem no proprio cabecalho, nao ha container .xz.
	const uint8_t lclppb = buffer[position++];
	lzma_options_lzma options {};
	options.lc = lclppb % 9;
	const int remainder = lclppb / 9;
	options.lp = remainder % 5;
	options.pb = remainder / 5;

	uint32_t dictionary_size = 0;
	for (uint8_t byte_index = 0; byte_index < 4; ++byte_index) {
		dictionary_size += static_cast<uint32_t>(buffer[position++]) << (byte_index * 8);
	}
	options.dict_size = dictionary_size;

	position += 8; // campo de tamanho descomprimido, que nao usamos

	lzma_stream stream = LZMA_STREAM_INIT;
	lzma_filter filters[2] = {
		lzma_filter { LZMA_FILTER_LZMA1, &options },
		lzma_filter { LZMA_VLI_UNKNOWN, nullptr }
	};

	if (lzma_raw_decoder(&stream, filters) != LZMA_OK) {
		spdlog::error("SpriteArchive: failed to initialize LZMA decoder for {}", sheet.path);
		return false;
	}

	auto decompressed = std::make_unique<uint8_t[]>(kSheetBytes + kBmpHeaderPadding);
	stream.next_in = buffer.data() + position;
	stream.avail_in = buffer.size() - position;
	stream.next_out = decompressed.get();
	stream.avail_out = kSheetBytes + kBmpHeaderPadding;

	const lzma_ret ret = lzma_code(&stream, LZMA_RUN);
	lzma_end(&stream);
	if (ret != LZMA_STREAM_END || stream.total_out < static_cast<uint64_t>(kBmpHeaderPadding) + kSheetBytes) {
		spdlog::error("SpriteArchive: failed to decode protobuf sprite sheet {} (lzma ret={})", sheet.path, static_cast<int>(ret));
		return false;
	}

	uint32_t pixel_offset = 0;
	std::memcpy(&pixel_offset, decompressed.get() + 10, sizeof(uint32_t));
	if (pixel_offset > static_cast<uint32_t>(kBmpHeaderPadding)) {
		spdlog::error("SpriteArchive: protobuf sprite sheet {} has an invalid BMP pixel offset", sheet.path);
		return false;
	}

	// BMP guarda as linhas de baixo para cima; a folha e desvirada aqui, uma
	// vez por decodificacao, para o recorte por sprite sair direto.
	auto decoded_pixels = std::make_shared<std::vector<uint8_t>>(kSheetBytes, 0);
	uint8_t* pixel_data = decompressed.get() + pixel_offset;
	for (int row = 0; row < kSheetDimension; ++row) {
		const int source_row = kSheetDimension - row - 1;
		std::memcpy(decoded_pixels->data() + row * kSheetDimension * 4, pixel_data + source_row * kSheetDimension * 4, static_cast<size_t>(kSheetDimension) * 4);
	}

	sheet.decoded_pixels = std::move(decoded_pixels);
	++decoded_sheet_count_;
	return true;
}

void SpriteArchive::pruneDecodedSheetCache(int32_t keep_sheet_index) const {
	while (decoded_sheet_count_ > kDecodedSheetCacheLimit) {
		size_t oldest_index = protobuf_sheets_.size();
		uint64_t oldest_tick = std::numeric_limits<uint64_t>::max();

		for (size_t index = 0; index < protobuf_sheets_.size(); ++index) {
			if (static_cast<int32_t>(index) == keep_sheet_index) {
				continue;
			}

			const auto& sheet = protobuf_sheets_[index];
			if (!sheet.decoded_pixels || sheet.last_access_tick == 0 || sheet.last_access_tick >= oldest_tick) {
				continue;
			}

			oldest_tick = sheet.last_access_tick;
			oldest_index = index;
		}

		if (oldest_index == protobuf_sheets_.size()) {
			return;
		}

		protobuf_sheets_[oldest_index].releaseDecodedPixels();
		--decoded_sheet_count_;
	}
}

bool SpriteArchive::readRGBA(uint32_t sprite_id, std::unique_ptr<uint8_t[]>& target, ImageDimensions& dimensions) const {
	target.reset();
	dimensions = {};

	if (backend_ != Backend::Protobuf) {
		return false;
	}

	if (sprite_id == 0) {
		// Sprite vazio: o id 0 e o "sem sprite" do cliente.
		target = std::make_unique<uint8_t[]>(dimensions.pixelCount() * 4);
		std::fill(target.get(), target.get() + dimensions.pixelCount() * 4, 0);
		return true;
	}

	if (sprite_id >= protobuf_sheet_lookup_.size()) {
		return false;
	}

	const int32_t sheet_index = protobuf_sheet_lookup_[sprite_id];
	if (sheet_index < 0 || static_cast<size_t>(sheet_index) >= protobuf_sheets_.size()) {
		return false;
	}

	// O preloader roda fora da thread principal, e duas threads podem cair na
	// mesma folha ao mesmo tempo.
	std::lock_guard<std::mutex> lock(protobuf_mutex_);
	auto& sheet = protobuf_sheets_[static_cast<size_t>(sheet_index)];
	if (!loadSheetPixels(sheet) || !sheet.decoded_pixels) {
		return false;
	}
	sheet.last_access_tick = ++protobuf_sheet_access_tick_;
	pruneDecodedSheetCache(sheet_index);

	const auto [source_width, source_height] = protobufSourceDimensions(sheet.layout);
	dimensions = ImageDimensions {
		static_cast<uint16_t>(source_width),
		static_cast<uint16_t>(source_height),
	};
	target = std::make_unique<uint8_t[]>(dimensions.pixelCount() * 4);
	std::fill(target.get(), target.get() + dimensions.pixelCount() * 4, 0);

	const uint32_t sprite_offset = sprite_id - sheet.first_id;
	const int columns = kSheetDimension / source_width;
	const int sprite_row = static_cast<int>(sprite_offset / static_cast<uint32_t>(columns));
	const int rows = kSheetDimension / source_height;
	if (sprite_row >= rows) {
		spdlog::error("SpriteArchive: sprite {} is outside the bounds of sheet {}", sprite_id, sheet.path);
		target.reset();
		dimensions = {};
		return false;
	}

	const int sprite_column = static_cast<int>(sprite_offset % static_cast<uint32_t>(columns));
	const auto* pixels = sheet.decoded_pixels->data();

	// BGRA na folha, RGBA na saida.
	for (int row = 0; row < source_height; ++row) {
		const size_t source_offset = (static_cast<size_t>(sprite_row * source_height + row) * kSheetDimension + static_cast<size_t>(sprite_column * source_width)) * 4;
		const size_t destination_offset = static_cast<size_t>(row) * dimensions.width * 4;
		for (int column = 0; column < source_width; ++column) {
			const size_t source_pixel = source_offset + static_cast<size_t>(column) * 4;
			const size_t destination_pixel = destination_offset + static_cast<size_t>(column) * 4;
			target[destination_pixel + 0] = pixels[source_pixel + 2];
			target[destination_pixel + 1] = pixels[source_pixel + 1];
			target[destination_pixel + 2] = pixels[source_pixel + 0];
			target[destination_pixel + 3] = pixels[source_pixel + 3];
		}
	}

	return true;
}
