#include "item_definitions/formats/godot/godot_things_reader.h"

#include "appearances.pb.h"

#include <array>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>

#include <wx/filename.h>

namespace {
	using namespace canary::protobuf::appearances;

	constexpr std::array<char, 4> kMagic { 'B', 'R', 'T', 'H' };
	// bit n ligado = o campo n do AppearanceFlags esta presente (e verdadeiro, se for bool)
	constexpr int kFlagBytes = 10;
	// objeto, outfit, efeito, missil -- a ordem do appearances.proto
	constexpr int kCategories = 4;

	// Leitura little-endian com a borda conferida: passar do fim liga failed e devolve zero, e
	// quem le confere failed depois de cada aparencia, em vez de a cada campo.
	struct Cursor {
		const uint8_t* data = nullptr;
		size_t size = 0;
		size_t pos = 0;
		bool failed = false;

		bool need(size_t count) {
			if (failed || count > size - pos) {
				failed = true;
				return false;
			}
			return true;
		}

		uint8_t u8() {
			return need(1) ? data[pos++] : 0;
		}

		int8_t i8() {
			return static_cast<int8_t>(u8());
		}

		uint16_t u16() {
			if (!need(2)) {
				return 0;
			}
			const auto value = static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
			pos += 2;
			return value;
		}

		uint32_t u32() {
			if (!need(4)) {
				return 0;
			}
			uint32_t value = 0;
			for (int byte = 3; byte >= 0; --byte) {
				value = (value << 8) | data[pos + static_cast<size_t>(byte)];
			}
			pos += 4;
			return value;
		}

		std::string text(size_t count) {
			if (!need(count)) {
				return {};
			}
			std::string value(reinterpret_cast<const char*>(data + pos), count);
			pos += count;
			return value;
		}
	};

	bool readFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& error) {
		std::ifstream file(path, std::ios::in | std::ios::binary);
		if (!file.is_open()) {
			error = std::format("Failed to open {} for reading.", path);
			return false;
		}
		bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		return true;
	}

	bool parseHeader(Cursor& cursor, const std::string& path, GodotThings::Header& header, std::string& error) {
		if (!cursor.need(kMagic.size()) || std::memcmp(cursor.data, kMagic.data(), kMagic.size()) != 0) {
			error = std::format("{} is not a Godot things index (no BRTH signature).", path);
			return false;
		}
		cursor.pos += kMagic.size();

		header.format = cursor.u16();
		if (header.format != GodotThings::kFormat) {
			error = std::format("{} has format {}, this editor reads format {}: update the editor or convert the assets again.", path, header.format, GodotThings::kFormat);
			return false;
		}
		header.appearances_version = cursor.u32();
		header.tile_pixels = cursor.u16();

		const uint32_t sheet_count = cursor.u32();
		if (!cursor.need(static_cast<size_t>(sheet_count) * 12)) {
			error = std::format("{} is truncated in the sheet table.", path);
			return false;
		}
		header.sheets.resize(sheet_count);
		for (auto& sheet : header.sheets) {
			sheet.first_id = cursor.u32();
			sheet.last_id = cursor.u32();
			sheet.sprite_width = cursor.u16();
			sheet.sprite_height = cursor.u16();
		}
		return !cursor.failed;
	}

	// Os valores das flags compostas, na ordem em que o conversor os escreve (VALUES, em ordem de
	// campo). Cada leitura e uma instrucao propria: a ordem de avaliacao dos argumentos de uma
	// chamada nao e garantida em C++.
	void readFlagValues(Cursor& cursor, const uint8_t* bits, AppearanceFlags& flags) {
		const auto has = [bits](int field) {
			return ((bits[field >> 3] >> (field & 7)) & 1) != 0;
		};

		if (has(1)) {
			flags.mutable_bank()->set_waypoints(cursor.u16());
		}
		if (has(10)) {
			flags.mutable_write()->set_max_text_length(cursor.u16());
		}
		if (has(11)) {
			flags.mutable_write_once()->set_max_text_length_once(cursor.u16());
		}
		if (has(21)) {
			// No proto do OTClient o gancho e "south" (campo 1) e "east" (campo 2); no deste editor,
			// "direction" no campo 1. O appearances.dat lido direto por aqui daria o campo 1 como
			// direction e jogaria o 2 fora, e e isso que se repete.
			const uint8_t south = cursor.u8();
			cursor.u8(); // east
			auto* hook = flags.mutable_hook();
			if (HOOK_TYPE_IsValid(south)) {
				hook->set_direction(static_cast<HOOK_TYPE>(south));
			}
		}
		if (has(23)) {
			auto* light = flags.mutable_light();
			light->set_brightness(cursor.u16());
			light->set_color(cursor.u16());
		}
		if (has(26)) {
			auto* shift = flags.mutable_shift();
			shift->set_x(cursor.u16());
			shift->set_y(cursor.u16());
		}
		if (has(27)) {
			flags.mutable_height()->set_elevation(cursor.u16());
		}
		if (has(30)) {
			flags.mutable_automap()->set_color(cursor.u16());
		}
		if (has(31)) {
			flags.mutable_lenshelp()->set_id(cursor.u16());
		}
		if (has(34)) {
			flags.mutable_clothes()->set_slot(cursor.u8());
		}
		if (has(35)) {
			const uint8_t action = cursor.u8();
			auto* default_action = flags.mutable_default_action();
			if (PLAYER_ACTION_IsValid(action)) {
				default_action->set_action(static_cast<PLAYER_ACTION>(action));
			}
		}
		if (has(36)) {
			const uint8_t category = cursor.u8();
			auto* market = flags.mutable_market();
			if (ITEM_CATEGORY_IsValid(category)) {
				market->set_category(static_cast<ITEM_CATEGORY>(category));
			}
			market->set_trade_as_object_id(cursor.u16());
			market->set_show_as_object_id(cursor.u16());
		}
		if (has(41)) {
			flags.mutable_changedtoexpire()->set_former_object_typeid(cursor.u16());
		}
		if (has(44)) {
			flags.mutable_cyclopediaitem()->set_cyclopedia_type(cursor.u16());
		}
		if (has(48)) {
			flags.mutable_upgradeclassification()->set_upgrade_classification(cursor.u8());
		}
		if (has(72)) {
			cursor.u8(); // transparencylevel: o proto deste editor nao tem o campo
		}
	}

	// As flags que so tem o bit: os bools. As mensagens com valor ja sairam de readFlagValues, e
	// um campo que este proto nao conhece (os de 49 em diante) e ignorado.
	void applyFlagBits(const uint8_t* bits, AppearanceFlags& flags) {
		const auto* descriptor = AppearanceFlags::descriptor();
		const auto* reflection = flags.GetReflection();
		for (int field_number = 1; field_number < kFlagBytes * 8; ++field_number) {
			if (((bits[field_number >> 3] >> (field_number & 7)) & 1) == 0) {
				continue;
			}
			const auto* field = descriptor->FindFieldByNumber(field_number);
			if (!field || field->is_repeated() || field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_BOOL) {
				continue;
			}
			reflection->SetBool(&flags, field, true);
		}
	}

	void readFrameGroup(Cursor& cursor, FrameGroup& group) {
		const uint8_t fixed = cursor.u8();
		if (FIXED_FRAME_GROUP_IsValid(fixed)) {
			group.set_fixed_frame_group(static_cast<FIXED_FRAME_GROUP>(fixed));
		}

		auto* info = group.mutable_sprite_info();
		info->set_pattern_width(cursor.u8());
		info->set_pattern_height(cursor.u8());
		info->set_pattern_depth(cursor.u8());
		info->set_layers(cursor.u8());
		info->set_bounding_square(cursor.u16());
		info->set_is_opaque(cursor.u8() != 0);

		if (cursor.u8() != 0) {
			auto* animation = info->mutable_animation();
			animation->set_default_start_phase(cursor.u8());
			animation->set_synchronized(cursor.u8() != 0);
			animation->set_random_start_phase(cursor.u8() != 0);
			const int8_t loop_type = cursor.i8();
			if (ANIMATION_LOOP_TYPE_IsValid(loop_type)) {
				animation->set_loop_type(static_cast<ANIMATION_LOOP_TYPE>(loop_type));
			}
			animation->set_loop_count(cursor.u16());
			const uint16_t phases = cursor.u16();
			for (uint16_t phase_index = 0; phase_index < phases && !cursor.failed; ++phase_index) {
				auto* phase = animation->add_sprite_phase();
				phase->set_duration_min(cursor.u16());
				phase->set_duration_max(cursor.u16());
			}
		}

		const uint8_t boxes = cursor.u8();
		for (uint8_t box_index = 0; box_index < boxes && !cursor.failed; ++box_index) {
			auto* box = info->add_bounding_box_per_direction();
			box->set_x(cursor.u16());
			box->set_y(cursor.u16());
			box->set_width(cursor.u16());
			box->set_height(cursor.u16());
		}

		const uint32_t sprites = cursor.u32();
		if (!cursor.need(static_cast<size_t>(sprites) * 4)) {
			return;
		}
		info->mutable_sprite_id()->Reserve(static_cast<int>(sprites));
		for (uint32_t sprite_index = 0; sprite_index < sprites; ++sprite_index) {
			info->add_sprite_id(cursor.u32());
		}
	}

	bool readAppearance(Cursor& cursor, uint32_t id, Appearance& appearance) {
		appearance.set_id(id);

		uint8_t bits[kFlagBytes];
		for (auto& byte : bits) {
			byte = cursor.u8();
		}
		const std::string name = cursor.text(cursor.u16());
		if (!name.empty()) {
			appearance.set_name(name);
		}

		auto* flags = appearance.mutable_flags();
		readFlagValues(cursor, bits, *flags);
		applyFlagBits(bits, *flags);

		const uint8_t groups = cursor.u8();
		for (uint8_t group_index = 0; group_index < groups && !cursor.failed; ++group_index) {
			readFrameGroup(cursor, *appearance.add_frame_group());
		}
		return !cursor.failed;
	}
}

bool GodotThings::isThingsFile(const std::string& path) {
	std::ifstream file(path, std::ios::in | std::ios::binary);
	std::array<char, 4> magic {};
	return file.is_open() && file.read(magic.data(), magic.size()) && magic == kMagic;
}

bool GodotThings::readHeader(const std::string& path, Header& header, std::string& error) {
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes, error)) {
		return false;
	}
	Cursor cursor { bytes.data(), bytes.size() };
	header = {};
	if (!parseHeader(cursor, path, header, error)) {
		if (error.empty()) {
			error = std::format("{} is truncated in the header.", path);
		}
		return false;
	}
	return true;
}

bool GodotThings::readAppearances(const std::string& path, Appearances& appearances, std::string& error, std::vector<std::string>& warnings) {
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes, error)) {
		return false;
	}

	Cursor cursor { bytes.data(), bytes.size() };
	Header header;
	if (!parseHeader(cursor, path, header, error)) {
		if (error.empty()) {
			error = std::format("{} is truncated in the header.", path);
		}
		return false;
	}

	// id -> deslocamento no blob, por categoria
	std::array<std::vector<std::pair<uint32_t, uint32_t>>, kCategories> tables;
	for (auto& table : tables) {
		const uint32_t count = cursor.u32();
		if (!cursor.need(static_cast<size_t>(count) * 8)) {
			error = std::format("{} is truncated in the id tables.", path);
			return false;
		}
		table.resize(count);
		for (auto& [id, offset] : table) {
			id = cursor.u32();
			offset = cursor.u32();
		}
	}

	const uint32_t blob_size = cursor.u32();
	if (!cursor.need(blob_size)) {
		error = std::format("{} is truncated: the blob says {} bytes and fewer are left.", path, blob_size);
		return false;
	}
	const uint8_t* blob = cursor.data + cursor.pos;

	appearances.Clear();
	// Efeitos e misseis tambem estao no indice, mas o editor so desenha objetos e outfits.
	const std::array<google::protobuf::RepeatedPtrField<Appearance>*, 2> targets { appearances.mutable_object(), appearances.mutable_outfit() };
	for (size_t category = 0; category < targets.size(); ++category) {
		auto* target = targets[category];
		target->Reserve(static_cast<int>(tables[category].size()));
		for (const auto& [id, offset] : tables[category]) {
			if (offset >= blob_size) {
				warnings.push_back(std::format("Godot things index: appearance {} points outside the blob; skipped.", id));
				continue;
			}
			Cursor entry { blob, blob_size, offset };
			auto* appearance = target->Add();
			if (!readAppearance(entry, id, *appearance)) {
				error = std::format("{}: appearance {} (category {}) runs past the end of the blob -- the index is corrupt or was written by another format.", path, id, category);
				return false;
			}
		}
	}
	return true;
}

std::string GodotThings::sheetPath(const wxString& things_path, uint32_t first_id) {
	const wxFileName index(things_path);
	const wxString folder = index.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + "sheets" + wxFileName::GetPathSeparator();
	return wxFileName(folder, wxString::Format("%u.png", first_id)).GetFullPath().utf8_string();
}
