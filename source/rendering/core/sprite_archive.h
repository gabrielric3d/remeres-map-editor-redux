#ifndef RME_RENDERING_CORE_SPRITE_ARCHIVE_H_
#define RME_RENDERING_CORE_SPRITE_ARCHIVE_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class wxFileName;
struct ImageDimensions;
class wxString;

class SpriteArchive {
public:
	// Como os sprites de uma folha 12+/13 estao dispostos nela.
	enum class ProtobufSpriteLayout : uint8_t {
		OneByOne = 0,
		OneByTwo = 1,
		TwoByOne = 2,
		TwoByTwo = 3,
	};

	// One file of a fragmented set, covering the id range [start_id, end_id].
	// The sprite offsets themselves stay in the shared table, so this only says
	// WHICH file to open for a given id.
	struct Fragment {
		uint32_t start_id = 0;
		uint32_t end_id = 0;
		std::string filename;
	};

	[[nodiscard]] static std::shared_ptr<SpriteArchive> load(const wxFileName& path, bool is_extended, wxString& error, std::vector<std::string>& warnings);

	// A large .spr can be split into a ".cat" catalog plus N fragment files (see
	// the client's SpriteManager::loadFragmentedSpr). Given the configured
	// sprites path, returns the catalog to use, or an empty wxFileName when this
	// is a plain single-file .spr.
	//
	// Accepts a .cat directly and also resolves a .spr path whose sibling .cat
	// exists — the catalog wins, exactly like the client does, so a fragmented
	// set is picked up without anyone having to reconfigure paths.
	[[nodiscard]] static wxFileName resolveCatalogPath(const wxFileName& sprites_path);

	// True when a usable sprite source exists: the .spr itself, or a catalog.
	// Once a set is fragmented the monolithic .spr is usually deleted to reclaim
	// the disk, so checking only for the .spr rejects a perfectly good client.
	[[nodiscard]] static bool sourceExists(const wxFileName& sprites_path);

	// Sprite signature from either layout. In a .spr it is the first u32; in a
	// catalog it sits at offset 8, after the magic and the version.
	[[nodiscard]] static bool readSourceSignature(const wxFileName& sprites_path, uint32_t& signature);

	[[nodiscard]] uint32_t spriteCount() const {
		return sprite_count_;
	}

	[[nodiscard]] bool isExtended() const {
		return is_extended_;
	}

	[[nodiscard]] const std::string& fileName() const {
		return filename_;
	}

	[[nodiscard]] bool isFragmented() const {
		return !fragments_.empty();
	}

	[[nodiscard]] size_t fragmentCount() const {
		return fragments_.size();
	}

	[[nodiscard]] bool readCompressed(uint32_t sprite_id, std::unique_ptr<uint8_t[]>& target, uint16_t& size) const;

	// --- Assets 12+/13 (catalog-content.json + folhas .bmp.lzma) -------------
	//
	// Um caminho a parte, ao lado do .spr: em vez de blobs RLE por sprite, o
	// cliente novo guarda folhas de 384x384 comprimidas em LZMA, e cada sprite
	// e um recorte da folha. Por isso este backend entrega RGBA pronto, nao o
	// blob comprimido que readCompressed() devolve.
	//
	// O mesmo caminho le o indice do cliente Godot do battle royale (things.bin,
	// GodotThings): as folhas sao PNG de 384x384 em sheets/<primeiro sprite>.png,
	// ja com a transparencia, e o tamanho de cada sprite vem na tabela do indice.
	[[nodiscard]] static std::shared_ptr<SpriteArchive> loadProtobuf(const wxFileName& catalog_path, wxString& error, std::vector<std::string>& warnings);

	[[nodiscard]] bool isProtobuf() const {
		return backend_ == Backend::Protobuf;
	}

	// Quantos pixels da folha fazem um pixel do editor. 1 nos assets do proprio
	// cliente; 2 no conjunto dobrado (things/1310-x2, 64 px por casa), que o
	// editor le de volta a 32 px por casa: a grade do mapa nao muda, so a
	// resolucao da arte.
	[[nodiscard]] int assetScale() const {
		return asset_scale_;
	}

	// 32x32 no .spr; nas folhas novas pode ser 32x64, 64x32 ou 64x64.
	[[nodiscard]] ImageDimensions spriteDimensions(uint32_t sprite_id) const;

	// Pixels RGBA do sprite. Só o backend protobuf; o legado continua passando
	// por readCompressed() + NormalImage::Decompress.
	//
	// native = false: na escala do editor (32 px por casa) -- e o que icones,
	// paleta, scanners e quem mais le pixels na CPU esperam. native = true: na
	// resolucao da folha (64 px por casa no conjunto dobrado), para o atlas do
	// mapa, que desenha o quad no tamanho da casa e deixa a GPU amostrar a arte
	// inteira. Com assetScale() == 1 as duas sao a mesma coisa.
	[[nodiscard]] bool readRGBA(uint32_t sprite_id, std::unique_ptr<uint8_t[]>& target, ImageDimensions& dimensions, bool native = false) const;

private:
	enum class Backend : uint8_t {
		Legacy,
		Protobuf,
	};

	struct ProtobufSheet {
		uint32_t first_id = 0;
		uint32_t last_id = 0;
		ProtobufSpriteLayout layout = ProtobufSpriteLayout::OneByOne;
		std::string path;
		// PNG do cliente Godot (caminho em UTF-8) em vez de .bmp.lzma.
		bool png = false;
		mutable std::shared_ptr<std::vector<uint8_t>> decoded_pixels;
		mutable uint64_t last_access_tick = 0;

		void releaseDecodedPixels() const {
			decoded_pixels.reset();
			last_access_tick = 0;
		}
	};

	SpriteArchive(std::string filename, bool is_extended, uint32_t sprite_count, std::vector<uint32_t> sprite_offsets, std::vector<Fragment> fragments = {});
	SpriteArchive(std::string filename, uint32_t sprite_count, std::vector<ProtobufSheet> sheets, std::vector<int32_t> sheet_lookup);

	[[nodiscard]] static std::shared_ptr<SpriteArchive> loadGodotThings(const wxFileName& things_path, wxString& error, std::vector<std::string>& warnings);
	// Decodifica a folha inteira (BGRA de cima para baixo). Nao toca em nada
	// do archive: roda FORA da trava, para os workers do preloader decodificarem
	// em paralelo e a thread principal nao esperar a folha de outro sprite.
	[[nodiscard]] std::shared_ptr<std::vector<uint8_t>> decodeSheetPixels(const ProtobufSheet& sheet) const;
	void pruneDecodedSheetCache(int32_t keep_sheet_index) const;

	// Which file holds this sprite. Returns nullptr when no fragment covers it;
	// for a single-file archive it always answers filename_.
	[[nodiscard]] const std::string* fileForSprite(uint32_t sprite_id) const;

	std::string filename_;
	bool is_extended_ = false;
	uint32_t sprite_count_ = 0;
	// Offset of each sprite WITHIN the file that holds it (1-based). For a
	// fragmented set that file is the fragment, not the catalog.
	std::vector<uint32_t> sprite_offsets_;
	// Empty for a single .spr; otherwise sorted by start_id and disjoint.
	std::vector<Fragment> fragments_;

	Backend backend_ = Backend::Legacy;
	int asset_scale_ = 1;
	mutable std::mutex protobuf_mutex_;
	mutable uint64_t protobuf_sheet_access_tick_ = 0;
	mutable size_t decoded_sheet_count_ = 0;
	std::vector<ProtobufSheet> protobuf_sheets_;
	std::vector<int32_t> protobuf_sheet_lookup_;
};

#endif
