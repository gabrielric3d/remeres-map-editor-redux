#include "item_definitions/core/item_definition_store.h"

#include <stdexcept>

// flagMask subiu para o header junto com isFlagSet (que e inline agora); os
// escritores de flag aqui continuam usando o mesmo helper.
using item_definitions_detail::flagMask;

ItemDefinitionStore g_item_definitions;

ItemDefinitionView::ItemDefinitionView(const ItemDefinitionStore* store, DefinitionId index) :
	store_(store), index_(index) {
}


void ItemDefinitionStore::clear() {
	identity_ = {};
	flags_ = {};
	attributes_ = {};
	text_ = {};
	visual_ = {};
	editor_ = {};
	server_to_index_.fill(0);
	client_to_servers_.clear();
	empty_client_results_.clear();
	max_server_id_ = 0;
	MajorVersion = 0;
	MinorVersion = 0;
	BuildNumber = 0;
}

void ItemDefinitionStore::resetBrushData() {
	const size_t count = identity_.server_ids.size();
	for (size_t i = 0; i < count; ++i) {
		// Reset editor brush pointers
		editor_.data[i] = ItemEditorData {};

		// Reset brush-related flags
		flags_.masks[i] &= ~(
			flagMask(ItemFlag::IsBorder) |
			flagMask(ItemFlag::IsOptionalBorder) |
			flagMask(ItemFlag::IsWall) |
			flagMask(ItemFlag::IsBrushDoor) |
			flagMask(ItemFlag::IsTable) |
			flagMask(ItemFlag::IsCarpet) |
			flagMask(ItemFlag::WallHateMe) |
			flagMask(ItemFlag::HasRaw) |
			flagMask(ItemFlag::InOtherTileset)
		);

		// Reset brush-related attributes
		attributes_.border_groups[i] = 0;
		attributes_.border_alignments[i] = BORDER_NONE;
	}
}

void ItemDefinitionStore::reserve(size_t count) {
	identity_.server_ids.reserve(count);
	identity_.groups.reserve(count);
	identity_.types.reserve(count);
	flags_.masks.reserve(count);
	attributes_.volumes.reserve(count);
	attributes_.max_text_lengths.reserve(count);
	attributes_.slot_positions.reserve(count);
	attributes_.weapon_types.reserve(count);
	attributes_.classifications.reserve(count);
	attributes_.border_base_ground_ids.reserve(count);
	attributes_.border_groups.reserve(count);
	attributes_.weights.reserve(count);
	attributes_.attacks.reserve(count);
	attributes_.defenses.reserve(count);
	attributes_.armors.reserve(count);
	attributes_.charges.reserve(count);
	attributes_.rotate_to.reserve(count);
	attributes_.way_speeds.reserve(count);
	attributes_.always_on_top_orders.reserve(count);
	attributes_.border_alignments.reserve(count);
	text_.names.reserve(count);
	text_.editor_suffixes.reserve(count);
	text_.descriptions.reserve(count);
	visual_.client_ids.reserve(count);
	editor_.data.reserve(count);
}

void ItemDefinitionStore::append(ResolvedItemDefinitionRow row) {
	const DefinitionId index = static_cast<DefinitionId>(identity_.server_ids.size());
	identity_.server_ids.push_back(row.server_id);
	identity_.groups.push_back(row.group);
	identity_.types.push_back(row.type);
	flags_.masks.push_back(row.flags);
	attributes_.volumes.push_back(row.volume);
	attributes_.max_text_lengths.push_back(row.max_text_len);
	attributes_.slot_positions.push_back(row.slot_position);
	attributes_.weapon_types.push_back(row.weapon_type);
	attributes_.classifications.push_back(row.classification);
	attributes_.border_base_ground_ids.push_back(row.border_base_ground_id);
	attributes_.border_groups.push_back(row.border_group);
	attributes_.weights.push_back(row.weight);
	attributes_.attacks.push_back(row.attack);
	attributes_.defenses.push_back(row.defense);
	attributes_.armors.push_back(row.armor);
	attributes_.charges.push_back(row.charges);
	attributes_.rotate_to.push_back(row.rotate_to);
	attributes_.way_speeds.push_back(row.way_speed);
	attributes_.always_on_top_orders.push_back(row.always_on_top_order);
	attributes_.border_alignments.push_back(row.border_alignment);
	text_.names.push_back(std::move(row.name));
	text_.editor_suffixes.push_back(std::move(row.editor_suffix));
	text_.descriptions.push_back(std::move(row.description));
	visual_.client_ids.push_back(row.client_id);
	editor_.data.emplace_back();

	server_to_index_[row.server_id] = index + 1;
	if (row.client_id != 0) {
		client_to_servers_[row.client_id].push_back(row.server_id);
	}
	max_server_id_ = std::max(max_server_id_, row.server_id);
}

bool ItemDefinitionStore::exists(ServerItemId server_id) const {
	return server_to_index_[server_id] != 0;
}

ItemDefinitionView ItemDefinitionStore::get(ServerItemId server_id) const {
	const DefinitionId stored_index = server_to_index_[server_id];
	if (stored_index == 0) {
		return {};
	}
	return ItemDefinitionView(this, stored_index - 1);
}

std::optional<ServerItemId> ItemDefinitionStore::findByClientId(ClientItemId client_id) const {
	const auto it = client_to_servers_.find(client_id);
	if (it == client_to_servers_.end() || it->second.empty()) {
		return std::nullopt;
	}
	return it->second.front();
}

std::span<const ServerItemId> ItemDefinitionStore::findAllByClientId(ClientItemId client_id) const {
	const auto it = client_to_servers_.find(client_id);
	if (it == client_to_servers_.end()) {
		return empty_client_results_;
	}
	return it->second;
}

void ItemDefinitionStore::setEditorData(ServerItemId server_id, const ItemEditorData& editor_data) {
	editor_.data[indexOf(server_id)] = editor_data;
}

ItemEditorData& ItemDefinitionStore::mutableEditorData(ServerItemId server_id) {
	return editor_.data[indexOf(server_id)];
}

const ItemEditorData& ItemDefinitionStore::editorData(ServerItemId server_id) const {
	return editor_.data[indexOf(server_id)];
}

void ItemDefinitionStore::setMetaItem(ServerItemId server_id, bool value) {
	setFlagAtIndex(indexOf(server_id), ItemFlag::MetaItem, value);
}

void ItemDefinitionStore::ensureMetaItem(ServerItemId server_id) {
	if (exists(server_id)) {
		setMetaItem(server_id, true);
		return;
	}

	ResolvedItemDefinitionRow row;
	row.server_id = server_id;
	row.flags = flagMask(ItemFlag::MetaItem);
	append(std::move(row));
}

void ItemDefinitionStore::setFlag(ServerItemId server_id, ItemFlag flag, bool value) {
	setFlagAtIndex(indexOf(server_id), flag, value);
}

void ItemDefinitionStore::setAttribute(ServerItemId server_id, ItemAttributeKey key, int64_t value) {
	setAttributeAtIndex(indexOf(server_id), key, value);
}

void ItemDefinitionStore::setGroup(ServerItemId server_id, ItemGroup_t group) {
	identity_.groups[indexOf(server_id)] = group;
}

void ItemDefinitionStore::setType(ServerItemId server_id, ItemTypes_t type) {
	identity_.types[indexOf(server_id)] = type;
}

void ItemDefinitionStore::setName(ServerItemId server_id, std::string value) {
	text_.names[indexOf(server_id)] = std::move(value);
}

void ItemDefinitionStore::setEditorSuffix(ServerItemId server_id, std::string value) {
	text_.editor_suffixes[indexOf(server_id)] = std::move(value);
}

void ItemDefinitionStore::setDescription(ServerItemId server_id, std::string value) {
	text_.descriptions[indexOf(server_id)] = std::move(value);
}

DefinitionId ItemDefinitionStore::indexOf(ServerItemId server_id) const {
	const DefinitionId stored_index = server_to_index_[server_id];
	if (stored_index == 0) {
		throw std::out_of_range("Unknown item definition id");
	}
	return stored_index - 1;
}

void ItemDefinitionStore::setFlagAtIndex(DefinitionId index, ItemFlag flag, bool value) {
	if (value) {
		flags_.masks[index] |= flagMask(flag);
	} else {
		flags_.masks[index] &= ~flagMask(flag);
	}
}

void ItemDefinitionStore::setAttributeAtIndex(DefinitionId index, ItemAttributeKey key, int64_t value) {
	switch (key) {
		case ItemAttributeKey::Volume: attributes_.volumes[index] = static_cast<uint16_t>(value); break;
		case ItemAttributeKey::MaxTextLen: attributes_.max_text_lengths[index] = static_cast<uint16_t>(value); break;
		case ItemAttributeKey::SlotPosition: attributes_.slot_positions[index] = static_cast<uint16_t>(value); break;
		case ItemAttributeKey::WeaponType: attributes_.weapon_types[index] = static_cast<uint8_t>(value); break;
		case ItemAttributeKey::Classification: attributes_.classifications[index] = static_cast<uint8_t>(value); break;
		case ItemAttributeKey::BorderBaseGroundId: attributes_.border_base_ground_ids[index] = static_cast<uint16_t>(value); break;
		case ItemAttributeKey::BorderGroup: attributes_.border_groups[index] = static_cast<uint32_t>(value); break;
		case ItemAttributeKey::Weight: attributes_.weights[index] = static_cast<float>(value) / 1000.0f; break;
		case ItemAttributeKey::Attack: attributes_.attacks[index] = static_cast<int>(value); break;
		case ItemAttributeKey::Defense: attributes_.defenses[index] = static_cast<int>(value); break;
		case ItemAttributeKey::Armor: attributes_.armors[index] = static_cast<int>(value); break;
		case ItemAttributeKey::Charges: attributes_.charges[index] = static_cast<uint32_t>(value); break;
		case ItemAttributeKey::RotateTo: attributes_.rotate_to[index] = static_cast<uint16_t>(value); break;
		case ItemAttributeKey::WaySpeed: attributes_.way_speeds[index] = static_cast<uint16_t>(value); break;
		case ItemAttributeKey::AlwaysOnTopOrder: attributes_.always_on_top_orders[index] = static_cast<int>(value); break;
		case ItemAttributeKey::BorderAlignment: attributes_.border_alignments[index] = static_cast<BorderType>(value); break;
	}
}
