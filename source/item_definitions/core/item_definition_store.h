#ifndef RME_ITEM_DEFINITION_STORE_H_
#define RME_ITEM_DEFINITION_STORE_H_

#include "item_definitions/core/item_definition_fragments.h"

#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <string_view>
#include <unordered_map>

class ItemDefinitionStore;

struct ItemIdentityTable {
	std::vector<ServerItemId> server_ids;
	std::vector<ItemGroup_t> groups;
	std::vector<ItemTypes_t> types;
};

struct ItemFlagTable {
	std::vector<uint64_t> masks;
};

struct ItemAttributeTable {
	std::vector<uint16_t> volumes;
	std::vector<uint16_t> max_text_lengths;
	std::vector<uint16_t> slot_positions;
	std::vector<uint8_t> weapon_types;
	std::vector<uint8_t> classifications;
	std::vector<uint16_t> border_base_ground_ids;
	std::vector<uint32_t> border_groups;
	std::vector<float> weights;
	std::vector<int> attacks;
	std::vector<int> defenses;
	std::vector<int> armors;
	std::vector<uint32_t> charges;
	std::vector<uint16_t> rotate_to;
	std::vector<uint16_t> way_speeds;
	std::vector<int> always_on_top_orders;
	std::vector<BorderType> border_alignments;
};

struct ItemTextTable {
	std::vector<std::string> names;
	std::vector<std::string> editor_suffixes;
	std::vector<std::string> descriptions;
};

struct ItemVisualTable {
	std::vector<ClientItemId> client_ids;
};

struct ItemEditorTable {
	std::vector<ItemEditorData> data;
};

class ItemDefinitionView {
public:
	ItemDefinitionView() = default;
	ItemDefinitionView(const ItemDefinitionStore* store, DefinitionId index);

	explicit operator bool() const {
		return isValid();
	}

	ServerItemId serverId() const;
	ClientItemId clientId() const;
	ItemGroup_t group() const;
	ItemTypes_t type() const;
	std::string_view name() const;
	std::string_view editorSuffix() const;
	std::string_view description() const;
	bool hasFlag(ItemFlag flag) const;
	int64_t attribute(ItemAttributeKey key) const;
	const ItemEditorData& editorData() const;

	bool isGroundTile() const;
	bool isSplash() const;
	bool isFluidContainer() const;
	bool isDepot() const;
	bool isMailbox() const;
	bool isTrashHolder() const;
	bool isContainer() const;
	bool isDoor() const;
	bool isMagicField() const;
	bool isTeleport() const;
	bool isBed() const;
	bool isKey() const;
	bool isPodium() const;
	bool isClientCharged() const;
	bool isExtraCharged() const;
	bool isTooltipable() const;
	bool isMetaItem() const;
	bool isFloorChange() const;

private:
	bool isValid() const;

	const ItemDefinitionStore* store_ = nullptr;
	DefinitionId index_ = 0;
};

class ItemDefinitionStore {
public:
	void clear();
	void resetBrushData();
	void reserve(size_t count);
	void append(ResolvedItemDefinitionRow row);

	bool exists(ServerItemId server_id) const;
	bool typeExists(ServerItemId server_id) const {
		return exists(server_id);
	}
	ItemDefinitionView get(ServerItemId server_id) const;
	std::optional<ServerItemId> findByClientId(ClientItemId client_id) const;
	std::span<const ServerItemId> findAllByClientId(ClientItemId client_id) const;
	std::span<const ServerItemId> allIds() const {
		return identity_.server_ids;
	}
	ServerItemId maxServerId() const {
		return max_server_id_;
	}
	uint16_t getMaxID() const {
		return maxServerId();
	}

	void setEditorData(ServerItemId server_id, const ItemEditorData& editor_data);
	ItemEditorData& mutableEditorData(ServerItemId server_id);
	const ItemEditorData& editorData(ServerItemId server_id) const;
	void setMetaItem(ServerItemId server_id, bool value);
	void ensureMetaItem(ServerItemId server_id);
	void setFlag(ServerItemId server_id, ItemFlag flag, bool value);
	void setAttribute(ServerItemId server_id, ItemAttributeKey key, int64_t value);
	void setGroup(ServerItemId server_id, ItemGroup_t group);
	void setType(ServerItemId server_id, ItemTypes_t type);
	void setName(ServerItemId server_id, std::string value);
	void setEditorSuffix(ServerItemId server_id, std::string value);
	void setDescription(ServerItemId server_id, std::string value);

	uint32_t MajorVersion = 0;
	uint32_t MinorVersion = 0;
	uint32_t BuildNumber = 0;

private:
	friend class ItemDefinitionView;

	DefinitionId indexOf(ServerItemId server_id) const;
	bool isFlagSet(DefinitionId index, ItemFlag flag) const;
	void setFlagAtIndex(DefinitionId index, ItemFlag flag, bool value);
	int64_t attributeValue(DefinitionId index, ItemAttributeKey key) const;
	void setAttributeAtIndex(DefinitionId index, ItemAttributeKey key, int64_t value);

	ItemIdentityTable identity_;
	ItemFlagTable flags_;
	ItemAttributeTable attributes_;
	ItemTextTable text_;
	ItemVisualTable visual_;
	ItemEditorTable editor_;

	std::array<DefinitionId, static_cast<size_t>(std::numeric_limits<ServerItemId>::max()) + 1> server_to_index_ {};
	std::unordered_map<ClientItemId, std::vector<ServerItemId>> client_to_servers_;
	mutable std::vector<ServerItemId> empty_client_results_;
	ServerItemId max_server_id_ = 0;
};

extern ItemDefinitionStore g_item_definitions;

// Definicoes inline dos acessores quentes.
//
// Elas moram AQUI, e nao no corpo da classe, porque ItemDefinitionView e declarada
// antes de ItemDefinitionStore -- la em cima o store ainda e tipo incompleto e
// nada disso compilaria.
//
// O motivo de estarem no header e que o build nao usa LTCG (ver o comentario em
// CMakeLists.txt): com os corpos no .cpp, cada pergunta feita a uma definicao vira
// uma chamada de verdade atravessando a fronteira de traducao, e cada uma refaz o
// isValid() (tres comparacoes de .size() em vetores diferentes) antes de uma unica
// leitura util. O caminho de desenho faz ~20 dessas por item, milhoes por frame.
// Visiveis aqui, o compilador funde os isValid() repetidos e hasFlag vira load+test.

namespace item_definitions_detail {
	constexpr uint64_t flagMask(ItemFlag flag) {
		return uint64_t { 1 } << static_cast<uint8_t>(flag);
	}
}

inline bool ItemDefinitionStore::isFlagSet(DefinitionId index, ItemFlag flag) const {
	return (flags_.masks[index] & item_definitions_detail::flagMask(flag)) != 0;
}

inline int64_t ItemDefinitionStore::attributeValue(DefinitionId index, ItemAttributeKey key) const {
	switch (key) {
		case ItemAttributeKey::Volume: return attributes_.volumes[index];
		case ItemAttributeKey::MaxTextLen: return attributes_.max_text_lengths[index];
		case ItemAttributeKey::SlotPosition: return attributes_.slot_positions[index];
		case ItemAttributeKey::WeaponType: return attributes_.weapon_types[index];
		case ItemAttributeKey::Classification: return attributes_.classifications[index];
		case ItemAttributeKey::BorderBaseGroundId: return attributes_.border_base_ground_ids[index];
		case ItemAttributeKey::BorderGroup: return attributes_.border_groups[index];
		case ItemAttributeKey::Weight: return static_cast<int64_t>(std::llround(attributes_.weights[index] * 1000.0f));
		case ItemAttributeKey::Attack: return attributes_.attacks[index];
		case ItemAttributeKey::Defense: return attributes_.defenses[index];
		case ItemAttributeKey::Armor: return attributes_.armors[index];
		case ItemAttributeKey::Charges: return attributes_.charges[index];
		case ItemAttributeKey::RotateTo: return attributes_.rotate_to[index];
		case ItemAttributeKey::WaySpeed: return attributes_.way_speeds[index];
		case ItemAttributeKey::AlwaysOnTopOrder: return attributes_.always_on_top_orders[index];
		case ItemAttributeKey::BorderAlignment: return static_cast<int64_t>(attributes_.border_alignments[index]);
	}
	return 0;
}

inline bool ItemDefinitionView::isValid() const {
	return store_ != nullptr && index_ < store_->identity_.server_ids.size() && index_ < store_->visual_.client_ids.size() &&
		index_ < store_->editor_.data.size();
}

inline ServerItemId ItemDefinitionView::serverId() const {
	return isValid() ? store_->identity_.server_ids[index_] : 0;
}

inline ClientItemId ItemDefinitionView::clientId() const {
	return isValid() ? store_->visual_.client_ids[index_] : 0;
}

inline ItemGroup_t ItemDefinitionView::group() const {
	return isValid() ? store_->identity_.groups[index_] : ITEM_GROUP_NONE;
}

inline ItemTypes_t ItemDefinitionView::type() const {
	return isValid() ? store_->identity_.types[index_] : ITEM_TYPE_NONE;
}

inline std::string_view ItemDefinitionView::name() const {
	static constexpr std::string_view empty;
	return isValid() ? std::string_view(store_->text_.names[index_]) : empty;
}

inline std::string_view ItemDefinitionView::editorSuffix() const {
	static constexpr std::string_view empty;
	return isValid() ? std::string_view(store_->text_.editor_suffixes[index_]) : empty;
}

inline std::string_view ItemDefinitionView::description() const {
	static constexpr std::string_view empty;
	return isValid() ? std::string_view(store_->text_.descriptions[index_]) : empty;
}

inline bool ItemDefinitionView::hasFlag(ItemFlag flag) const {
	return isValid() && store_->isFlagSet(index_, flag);
}

inline int64_t ItemDefinitionView::attribute(ItemAttributeKey key) const {
	return isValid() ? store_->attributeValue(index_, key) : 0;
}

inline const ItemEditorData& ItemDefinitionView::editorData() const {
	static const ItemEditorData empty;
	return isValid() ? store_->editor_.data[index_] : empty;
}

inline bool ItemDefinitionView::isGroundTile() const { return group() == ITEM_GROUP_GROUND; }
inline bool ItemDefinitionView::isSplash() const { return group() == ITEM_GROUP_SPLASH; }
inline bool ItemDefinitionView::isFluidContainer() const { return group() == ITEM_GROUP_FLUID; }
inline bool ItemDefinitionView::isDepot() const { return type() == ITEM_TYPE_DEPOT; }
inline bool ItemDefinitionView::isMailbox() const { return type() == ITEM_TYPE_MAILBOX; }
inline bool ItemDefinitionView::isTrashHolder() const { return type() == ITEM_TYPE_TRASHHOLDER; }
inline bool ItemDefinitionView::isContainer() const { return type() == ITEM_TYPE_CONTAINER; }
inline bool ItemDefinitionView::isDoor() const { return type() == ITEM_TYPE_DOOR; }
inline bool ItemDefinitionView::isMagicField() const { return type() == ITEM_TYPE_MAGICFIELD; }
inline bool ItemDefinitionView::isTeleport() const { return type() == ITEM_TYPE_TELEPORT; }
inline bool ItemDefinitionView::isBed() const { return type() == ITEM_TYPE_BED; }
inline bool ItemDefinitionView::isKey() const { return type() == ITEM_TYPE_KEY; }
inline bool ItemDefinitionView::isPodium() const { return type() == ITEM_TYPE_PODIUM; }
inline bool ItemDefinitionView::isClientCharged() const { return hasFlag(ItemFlag::ClientChargeable); }
inline bool ItemDefinitionView::isExtraCharged() const { return hasFlag(ItemFlag::ExtraChargeable); }
inline bool ItemDefinitionView::isTooltipable() const { return hasFlag(ItemFlag::Tooltipable); }
inline bool ItemDefinitionView::isMetaItem() const { return hasFlag(ItemFlag::MetaItem); }
inline bool ItemDefinitionView::isFloorChange() const { return hasFlag(ItemFlag::FloorChange); }

#endif
