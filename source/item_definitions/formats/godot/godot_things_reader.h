#ifndef RME_ITEM_DEFINITIONS_FORMATS_GODOT_GODOT_THINGS_READER_H_
#define RME_ITEM_DEFINITIONS_FORMATS_GODOT_GODOT_THINGS_READER_H_

#include <cstdint>
#include <string>
#include <vector>

class wxString;

namespace canary::protobuf::appearances {
	class Appearances;
}

// O indice do cliente Godot do battle royale: client-godot/assets/things/<versao>/things.bin,
// gravado pelo tools/convert_assets.py de la (o formato esta no docstring dele, e o leitor de
// referencia e o things/things.gd do cliente). Ele junta as duas pecas dos assets 12+/13 num
// arquivo so: o appearances, num binario proprio, e a lista das folhas -- que ficam ao lado,
// em sheets/<primeiro sprite>.png, no lugar do catalog-content.json e das folhas .bmp.lzma.
//
// Nao e uma copia do 1310-x2: a arte repintada do battle royale (o chao, a agua, as margens) e
// os padroes novos das quinas so existem aqui.
namespace GodotThings {
	constexpr uint16_t kFormat = 1;
	constexpr const char* kFileName = "things.bin";

	struct Sheet {
		uint32_t first_id = 0;
		uint32_t last_id = 0;
		uint16_t sprite_width = 0;
		uint16_t sprite_height = 0;
	};

	struct Header {
		uint16_t format = 0;
		uint32_t appearances_version = 0;
		uint16_t tile_pixels = 0;
		std::vector<Sheet> sheets;
	};

	// O arquivo comeca com a assinatura "BRTH".
	[[nodiscard]] bool isThingsFile(const std::string& path);

	// So o cabecalho e a tabela das folhas: o que o SpriteArchive precisa.
	[[nodiscard]] bool readHeader(const std::string& path, Header& header, std::string& error);

	// Objetos e outfits como a mensagem protobuf que o appearances.dat daria, para a traducao do
	// ProtobufItemParser servir igual. As flags do things.bin guardam o NUMERO do campo do
	// appearances.proto, e os campos 1 a 48 do proto do OTClient (de onde o conversor le) e do
	// proto deste editor sao os mesmos; o que passa de 48 este editor nao conhece e fica de fora.
	[[nodiscard]] bool readAppearances(const std::string& path, canary::protobuf::appearances::Appearances& appearances, std::string& error, std::vector<std::string>& warnings);

	// sheets/<first_id>.png, ao lado do indice, em UTF-8 (para o wxImage abrir com FromUTF8).
	[[nodiscard]] std::string sheetPath(const wxString& things_path, uint32_t first_id);
}

#endif
