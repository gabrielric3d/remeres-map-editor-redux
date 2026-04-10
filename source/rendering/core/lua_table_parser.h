//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_CORE_LUA_TABLE_PARSER_H_
#define RME_RENDERING_CORE_LUA_TABLE_PARSER_H_

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace LuaTableParser {

// Represents a parsed Lua value
struct LuaValue {
	enum Type { NIL, NUMBER, STRING, TABLE, FUNCTION_CALL };
	Type type = NIL;
	double number = 0;
	std::string string_val;
	std::string func_name;              // for Position(x,y,z) calls
	std::vector<double> func_args;      // for Position(x,y,z) calls
	std::vector<std::pair<std::string, LuaValue>> named_fields; // key = value
	std::vector<LuaValue> array_fields;                         // sequential values
};

// Parse a Lua file and extract top-level variable assignments
// e.g., "DARKNESS_ZONES = { ... }" or "CUSTOM_ITEM_LIGHTS = { ... }"
// Supports both "local VAR = ..." and "VAR = ..."
std::unordered_map<std::string, LuaValue> parseFile(const std::string& filepath);

// Parse from string content directly
std::unordered_map<std::string, LuaValue> parseString(const std::string& content);

// Helper: get int from a table LuaValue by key
int getInt(const LuaValue& val, const std::string& key, int defaultVal = 0);

// Helper: get string from a table LuaValue by key
std::string getString(const LuaValue& val, const std::string& key, const std::string& defaultVal = "");

// Helper: find a named field in a table LuaValue (returns nullptr if not found)
const LuaValue* findField(const LuaValue& val, const std::string& key);

} // namespace LuaTableParser

#endif
