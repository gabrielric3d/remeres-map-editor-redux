#ifndef RME_CLIENT_ASSET_DETECTOR_H_
#define RME_CLIENT_ASSET_DETECTOR_H_

#include <optional>
#include <string>
#include <vector>

#include <wx/filename.h>

#include "app/client_version.h"

struct ClientAssetDetectionResult {
	std::optional<std::string> metadata_file_name;
	std::optional<std::string> sprites_file_name;
	std::optional<uint32_t> dat_signature;
	std::optional<uint32_t> spr_signature;
	std::optional<DatFormat> dat_format;
	std::optional<bool> transparency;
	std::optional<bool> extended;
	std::optional<bool> frame_durations;
	std::optional<bool> frame_groups;
	std::vector<std::string> warnings;
};

class ClientAssetDetector {
public:
	// Par de arquivos que substitui .dat/.spr nos assets 12+/13.
	struct ProtobufAssetPaths {
		wxFileName metadata; // appearances-<hash>.dat
		wxFileName sprites; // catalog-content.json
	};

	[[nodiscard]] static ClientAssetDetectionResult detect(const ClientVersion& client);

	// Onde estao os assets 12+/13 de uma pasta de cliente. Publico porque a
	// carga (ClientVersion::hasValidPaths) precisa resolver os mesmos arquivos
	// que a deteccao: o nome do appearances carrega um hash do conteudo, entao
	// nem o editor nem o usuario podem fixa-lo no clients.toml.
	// Os caminhos voltam preenchidos mesmo quando o arquivo nao existe -- sao o
	// melhor palpite, para a mensagem de erro dizer onde se procurou.
	[[nodiscard]] static ProtobufAssetPaths resolveProtobufPaths(const wxFileName& client_path, const std::string& configured_metadata_file);

private:
	// Caminho dos assets 12+/13 (appearances + catalog-content.json).
	[[nodiscard]] static ClientAssetDetectionResult detectProtobufAssets(const ClientVersion& client, const wxFileName& client_path);
};

#endif
