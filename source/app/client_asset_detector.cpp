#include "app/client_asset_detector.h"

#include <wx/dir.h>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include "app/definitions.h"
#include "io/filehandle.h"
#include "util/json.h"
#include "rendering/core/sprite_archive.h"
#include "item_definitions/core/item_definition_fragments.h"
#include "item_definitions/formats/dat/dat_item_parser.h"

namespace {
	constexpr size_t kMaxSampleOffsets = 24;

	struct ResolvedClientFile {
		wxFileName path;
		std::string filename;
		bool exists = false;
	};

	struct DatProbeCandidate {
		DatFormat format = DAT_FORMAT_UNKNOWN;
		bool extended = false;
		bool frame_durations = false;
		bool frame_groups = false;
		size_t warning_count = 0;
	};

	struct SpriteProbeCandidate {
		bool extended = false;
		uint32_t sprite_count = 0;
		int opaque_matches = 0;
		int alpha_matches = 0;
	};

	ResolvedClientFile resolveClientFile(const wxFileName& client_path, const std::string& configured_name, const std::string& fallback_name) {
		const auto sanitizeFileName = [](const std::string& raw_name) {
			return wxFileName(wxString::FromUTF8(raw_name)).GetFullName().ToStdString();
		};

		ResolvedClientFile resolved;
		resolved.filename = sanitizeFileName(configured_name);
		resolved.path = wxFileName(client_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), wxString::FromUTF8(resolved.filename));
		if (resolved.path.FileExists()) {
			resolved.exists = true;
			return resolved;
		}

		resolved.filename = sanitizeFileName(fallback_name);
		resolved.path = wxFileName(client_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), wxString::FromUTF8(resolved.filename));
		resolved.exists = resolved.path.FileExists();
		return resolved;
	}

	std::optional<uint32_t> readSignature(const wxFileName& path, std::string_view label, std::vector<std::string>& warnings) {
		FileReadHandle file(path.GetFullPath().ToStdString());
		if (!file.isOk()) {
			const auto message = std::format("{} signature detection failed: could not open {}", label, path.GetFullPath().ToStdString());
			spdlog::warn(message);
			warnings.push_back(message);
			return std::nullopt;
		}

		uint32_t signature = 0;
		if (!file.getU32(signature)) {
			const auto message = std::format("{} signature detection failed: could not read header from {}", label, path.GetFullPath().ToStdString());
			spdlog::warn(message);
			warnings.push_back(message);
			return std::nullopt;
		}

		return signature;
	}

	std::vector<DatProbeCandidate> probeDatCandidates(const wxFileName& dat_path, uint32_t dat_signature) {
		static constexpr std::array formats = {
			DAT_FORMAT_74,
			DAT_FORMAT_755,
			DAT_FORMAT_78,
			DAT_FORMAT_86,
			DAT_FORMAT_96,
			DAT_FORMAT_1010,
			DAT_FORMAT_1050,
			DAT_FORMAT_1057,
		};

		const DatItemParser parser;
		std::vector<DatProbeCandidate> successes;

		for (const auto format : formats) {
			for (const bool extended : { false, true }) {
				const std::array frame_duration_options = { false, true };
				const std::array frame_group_options = { false, true };

				for (const bool frame_durations : frame_duration_options) {
					for (const bool frame_groups : frame_group_options) {
						OtbVersion otb {};
						ClientVersion probe_client(otb, "detector-probe", "");
						probe_client.setExtended(extended);
						probe_client.setFrameDurations(frame_durations);
						probe_client.setFrameGroups(frame_groups);
						probe_client.setClientData(dat_signature, 0, format);

						ItemDefinitionLoadInput input;
						input.dat_path = dat_path;
						input.client_version = &probe_client;

						DatCatalog catalog;
						wxString error;
						std::vector<std::string> warnings;
						if (!parser.parseCatalog(input, catalog, error, warnings)) {
							continue;
						}

						successes.push_back(DatProbeCandidate {
							.format = format,
							.extended = extended,
							.frame_durations = frame_durations,
							.frame_groups = frame_groups,
							.warning_count = warnings.size(),
						});
					}
				}
			}
		}

		if (successes.empty()) {
			return successes;
		}

		const auto min_warning_count = std::ranges::min(successes, {}, &DatProbeCandidate::warning_count).warning_count;
		std::erase_if(successes, [min_warning_count](const DatProbeCandidate& candidate) {
			return candidate.warning_count != min_warning_count;
		});
		return successes;
	}

	bool readSpriteCount(FileReadHandle& file, bool extended, uint32_t& sprite_count) {
		if (extended) {
			return file.getU32(sprite_count);
		}

		uint16_t compact_count = 0;
		if (!file.getU16(compact_count)) {
			return false;
		}
		sprite_count = compact_count;
		return true;
	}

	bool validateSpriteRunEncoding(std::span<const uint8_t> blob, uint8_t colored_bpp) {
		size_t read = 0;
		size_t written_pixels = 0;
		constexpr size_t kSpritePixels = SPRITE_PIXELS * SPRITE_PIXELS;

		while (read + 4 <= blob.size() && written_pixels < kSpritePixels) {
			const uint16_t transparent = static_cast<uint16_t>(blob[read] | (blob[read + 1] << 8));
			read += 2;
			const uint16_t colored = static_cast<uint16_t>(blob[read] | (blob[read + 1] << 8));
			read += 2;

			if (written_pixels + transparent + colored > kSpritePixels) {
				return false;
			}
			const size_t colored_bytes = static_cast<size_t>(colored) * colored_bpp;
			if (read + colored_bytes > blob.size()) {
				return false;
			}

			read += colored_bytes;
			written_pixels += transparent + colored;
		}

		return written_pixels == kSpritePixels && read == blob.size();
	}

	std::optional<std::vector<uint32_t>> collectSpriteSamples(FileReadHandle& file, bool extended, uint32_t& sprite_count) {
		if (!readSpriteCount(file, extended, sprite_count)) {
			return std::nullopt;
		}
		if (sprite_count == 0 || sprite_count > MAX_SPRITES) {
			return std::nullopt;
		}

		const size_t header_size = 4 + (extended ? sizeof(uint32_t) : sizeof(uint16_t)) + static_cast<size_t>(sprite_count) * sizeof(uint32_t);
		if (header_size > file.size()) {
			return std::nullopt;
		}

		std::vector<uint32_t> sample_offsets;
		sample_offsets.reserve(kMaxSampleOffsets);

		uint32_t previous_non_zero_offset = 0;
		for (uint32_t sprite_id = 1; sprite_id <= sprite_count; ++sprite_id) {
			uint32_t offset = 0;
			if (!file.getU32(offset)) {
				return std::nullopt;
			}
			if (offset == 0) {
				continue;
			}
			if (offset < header_size || offset > file.size() - 5) {
				return std::nullopt;
			}
			if (previous_non_zero_offset != 0 && offset < previous_non_zero_offset) {
				return std::nullopt;
			}
			previous_non_zero_offset = offset;
			if (sample_offsets.size() < kMaxSampleOffsets) {
				sample_offsets.push_back(offset);
			}
		}

		return sample_offsets;
	}

	std::optional<SpriteProbeCandidate> probeSpriteCandidate(const wxFileName& spr_path, bool extended) {
		FileReadHandle file(spr_path.GetFullPath().ToStdString());
		if (!file.isOk()) {
			return std::nullopt;
		}

		uint32_t signature = 0;
		if (!file.getU32(signature)) {
			return std::nullopt;
		}

		uint32_t sprite_count = 0;
		auto sample_offsets = collectSpriteSamples(file, extended, sprite_count);
		if (!sample_offsets.has_value()) {
			return std::nullopt;
		}

		SpriteProbeCandidate candidate;
		candidate.extended = extended;
		candidate.sprite_count = sprite_count;

		for (const uint32_t offset : *sample_offsets) {
			if (!file.seek(offset + 3)) {
				return std::nullopt;
			}

			uint16_t compressed_size = 0;
			if (!file.getU16(compressed_size)) {
				return std::nullopt;
			}
			if (file.tell() + compressed_size > file.size()) {
				return std::nullopt;
			}

			std::vector<uint8_t> blob(compressed_size);
			if (!file.getRAW(blob.data(), compressed_size)) {
				return std::nullopt;
			}

			if (validateSpriteRunEncoding(blob, 3)) {
				candidate.opaque_matches += 1;
			}
			if (validateSpriteRunEncoding(blob, 4)) {
				candidate.alpha_matches += 1;
			}
		}

		return candidate;
	}

	void assignConsensus(const std::vector<DatProbeCandidate>& candidates, ClientAssetDetectionResult& result) {
		if (candidates.empty()) {
			return;
		}

		const auto same_extended = std::ranges::all_of(candidates, [&](const DatProbeCandidate& candidate) {
			return candidate.extended == candidates.front().extended;
		});
		if (same_extended) {
			result.extended = candidates.front().extended;
		}

		const auto same_frame_durations = std::ranges::all_of(candidates, [&](const DatProbeCandidate& candidate) {
			return candidate.frame_durations == candidates.front().frame_durations;
		});
		if (same_frame_durations) {
			result.frame_durations = candidates.front().frame_durations;
		}

		const auto same_frame_groups = std::ranges::all_of(candidates, [&](const DatProbeCandidate& candidate) {
			return candidate.frame_groups == candidates.front().frame_groups;
		});
		if (same_frame_groups) {
			result.frame_groups = candidates.front().frame_groups;
		}

		const auto same_format = std::ranges::all_of(candidates, [&](const DatProbeCandidate& candidate) {
			return candidate.format == candidates.front().format;
		});
		if (same_format) {
			result.dat_format = candidates.front().format;
		}
	}
}

namespace {
	// O nome que o catalog-content.json declara, que e a unica fonte autoritativa:
	// e por ele que o proprio cliente escolhe o arquivo.
	std::string appearancesFromCatalog(const wxFileName& client_path) {
		wxFileName catalog(client_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), "catalog-content.json");
		if (!catalog.FileExists()) {
			return {};
		}

		std::ifstream file(catalog.GetFullPath().ToStdString(), std::ios::in | std::ios::binary);
		if (!file.is_open()) {
			return {};
		}

		const auto document = json::json::parse(file, nullptr, false);
		if (document.is_discarded() || !document.is_array()) {
			return {};
		}

		for (const auto& entry : document) {
			if (!entry.is_object() || !entry.contains("type") || !entry.contains("file")) {
				continue;
			}
			if (entry["type"] == "appearances" && entry["file"].is_string()) {
				return entry["file"].get<std::string>();
			}
		}
		return {};
	}

	// Acha o appearances-<hash>.dat. Em ordem: o que o catalogo declara, o nome
	// configurado, e por ultimo o primeiro "appearances*.dat" do diretorio.
	//
	// ⛔ O catalogo vem primeiro porque o nome carrega o hash do conteudo: uma
	// pasta que ja foi regravada tem mais de um appearances, e o glob pega
	// qualquer um deles — na pratica, a versao errada, escolhida pela ordem do
	// sistema de arquivos. O catalogo aponta para o que vale.
	wxFileName findAppearancesFile(const wxFileName& client_path, const std::string& configured_name) {
		const auto from_catalog = appearancesFromCatalog(client_path);
		if (!from_catalog.empty()) {
			wxFileName declared(client_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), wxString::FromUTF8(from_catalog));
			if (declared.FileExists()) {
				return declared;
			}
			spdlog::warn("catalog-content.json names {}, which is not in the client path; falling back to a directory scan.", from_catalog);
		}

		if (!configured_name.empty()) {
			wxFileName configured(client_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), wxString::FromUTF8(configured_name));
			if (configured.FileExists()) {
				return configured;
			}
		}

		wxDir dir(client_path.GetPath());
		if (!dir.IsOpened()) {
			return {};
		}

		wxString name;
		if (dir.GetFirst(&name, "appearances*.dat", wxDIR_FILES)) {
			return wxFileName(client_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), name);
		}
		return {};
	}
}

ClientAssetDetectionResult ClientAssetDetector::detectProtobufAssets(const ClientVersion& client, const wxFileName& client_path) {
	ClientAssetDetectionResult result;

	const auto appearances = findAppearancesFile(client_path, client.getMetadataFile());
	if (!appearances.IsOk() || !appearances.FileExists()) {
		const auto message = "Client asset detection failed: no appearances file was found in the selected client path.";
		spdlog::warn(message);
		result.warnings.emplace_back(message);
	} else {
		result.metadata_file_name = appearances.GetFullName().ToStdString();
	}

	wxFileName catalog(client_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), "catalog-content.json");
	if (!catalog.FileExists()) {
		const auto message = "Client asset detection failed: catalog-content.json was not found in the selected client path.";
		spdlog::warn(message);
		result.warnings.emplace_back(message);
	} else {
		result.sprites_file_name = catalog.GetFullName().ToStdString();
	}

	// O formato novo nao carrega assinatura nem a flag "extended" -- e sempre
	// extended e sempre com alpha.
	result.dat_format = DAT_FORMAT_1057;
	result.extended = true;
	result.transparency = true;
	return result;
}

ClientAssetDetectionResult ClientAssetDetector::detect(const ClientVersion& client) {
	ClientAssetDetectionResult result;

	const auto client_path = client.getClientPath();
	if (!client_path.DirExists()) {
		const auto message = "Client asset detection skipped: selected client path does not exist.";
		spdlog::warn(message);
		result.warnings.emplace_back(message);
		return result;
	}

	// Clientes 12+/13 nao tem .dat nem .spr: os objetos vem num
	// appearances-<hash>.dat (protobuf) e os sprites em folhas listadas pelo
	// catalog-content.json. As sondagens de assinatura e de "extended" abaixo
	// nao se aplicam a esse formato, entao ele tem caminho proprio.
	if (client.isProtobuf()) {
		return detectProtobufAssets(client, client_path);
	}

	// A pasta e de cliente 12+/13 mas a versao esta configurada como dat_otb.
	// Sem esta checagem o usuario recebe "SPR file was not found", que e
	// verdade e nao ajuda: o .spr nao existe nesse formato e nunca vai existir.
	// O que falta e o Configuration Type, e a mensagem tem de dizer isso.
	if (wxFileName(client_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), "catalog-content.json").FileExists()) {
		const auto message = "Client asset detection failed: this folder holds 12+/13 assets "
							 "(catalog-content.json and appearances-<hash>.dat), but this client "
							 "version is set to a DAT+SPR Configuration Type. Set it to "
							 "protobuf_otb (or protobuf_only) and select the folder again.";
		spdlog::warn(message);
		result.warnings.emplace_back(message);
		return result;
	}

	const auto dat_file = resolveClientFile(client_path, client.getMetadataFile(), std::string { ASSETS_NAME } + ".dat");
	auto spr_file = resolveClientFile(client_path, client.getSpritesFile(), std::string { ASSETS_NAME } + ".spr");

	// A fragmented set may have no .spr at all — only the catalog. Fall back to
	// it so detection reports the client as usable instead of "SPR not found".
	if (!spr_file.exists) {
		if (const auto catalog = SpriteArchive::resolveCatalogPath(spr_file.path); catalog.IsOk()) {
			spr_file.path = catalog;
			spr_file.filename = catalog.GetFullName().ToStdString();
			spr_file.exists = true;
		}
	}

	if (!dat_file.exists) {
		const auto message = "Client asset detection failed: DAT file was not found in the selected client path.";
		spdlog::warn(message);
		result.warnings.emplace_back(message);
	} else {
		result.metadata_file_name = dat_file.filename;
		result.dat_signature = readSignature(dat_file.path, "DAT", result.warnings);
	}

	if (!spr_file.exists) {
		const auto message = "Client asset detection failed: SPR file was not found in the selected client path.";
		spdlog::warn(message);
		result.warnings.emplace_back(message);
	} else {
		result.sprites_file_name = spr_file.filename;
		// Not readSignature(): in a catalog the first u32 is the "SCAT" magic, not
		// the sprite signature.
		uint32_t spr_signature = 0;
		if (SpriteArchive::readSourceSignature(spr_file.path, spr_signature)) {
			result.spr_signature = spr_signature;
		} else {
			const auto message = std::format("SPR signature detection failed: could not read header from {}", spr_file.path.GetFullPath().ToStdString());
			spdlog::warn(message);
			result.warnings.push_back(message);
		}
	}

	if (result.dat_signature.has_value()) {
		const auto dat_candidates = probeDatCandidates(dat_file.path, *result.dat_signature);
		if (dat_candidates.empty()) {
			const auto message = "Client asset detection could not derive DAT feature flags from the DAT binary.";
			spdlog::warn("{} DAT={:X}", message, *result.dat_signature);
			result.warnings.emplace_back(message);
		} else {
			assignConsensus(dat_candidates, result);
			spdlog::info("Client asset detection found {} structurally valid DAT candidate(s) for DAT={:X}.", dat_candidates.size(), *result.dat_signature);
		}
	}

	if (spr_file.exists) {
		std::vector<SpriteProbeCandidate> sprite_candidates;
		for (const bool extended : { false, true }) {
			if (auto candidate = probeSpriteCandidate(spr_file.path, extended)) {
				sprite_candidates.push_back(*candidate);
			}
		}

		if (sprite_candidates.empty()) {
			const auto message = "Client asset detection could not derive SPR structure from the SPR binary.";
			spdlog::warn("{}", message);
			result.warnings.emplace_back(message);
		} else {
			const auto best_candidate = std::ranges::max_element(sprite_candidates, {}, [](const SpriteProbeCandidate& candidate) {
				return candidate.opaque_matches + candidate.alpha_matches;
			});

			if (best_candidate != sprite_candidates.end() && (best_candidate->opaque_matches > 0 || best_candidate->alpha_matches > 0)) {
				if (!result.extended.has_value()) {
					result.extended = best_candidate->extended;
				} else if (result.extended != best_candidate->extended) {
					const auto message = "DAT and SPR probing disagree on the extended flag.";
					spdlog::warn("{}", message);
					result.warnings.emplace_back(message);
					result.extended.reset();
				}
			}

			if (best_candidate != sprite_candidates.end()) {
				if (best_candidate->alpha_matches > best_candidate->opaque_matches) {
					result.transparency = true;
				} else if (best_candidate->opaque_matches > best_candidate->alpha_matches) {
					result.transparency = false;
				}
			}
		}
	}

	return result;
}
