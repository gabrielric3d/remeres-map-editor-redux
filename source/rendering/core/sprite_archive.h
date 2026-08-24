#ifndef RME_RENDERING_CORE_SPRITE_ARCHIVE_H_
#define RME_RENDERING_CORE_SPRITE_ARCHIVE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class wxFileName;
class wxString;

class SpriteArchive {
public:
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

private:
	SpriteArchive(std::string filename, bool is_extended, uint32_t sprite_count, std::vector<uint32_t> sprite_offsets, std::vector<Fragment> fragments = {});

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
};

#endif
