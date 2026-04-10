//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "rendering/core/lua_table_parser.h"

#include <fstream>
#include <sstream>
#include <cctype>
#include <spdlog/spdlog.h>

namespace LuaTableParser {

// ============================================================================
// Internal parser implementation
// ============================================================================

namespace {

class Parser {
public:
	explicit Parser(const std::string& input) :
		src_(input), pos_(0) { }

	std::unordered_map<std::string, LuaValue> parseTopLevel() {
		std::unordered_map<std::string, LuaValue> result;

		while (pos_ < src_.size()) {
			skipWhitespaceAndComments();
			if (pos_ >= src_.size()) {
				break;
			}

			// Check for "return" keyword (skip it, parse the value after)
			if (matchKeyword("return")) {
				skipWhitespaceAndComments();
				// "return VARNAME" or "return { ... }"
				// Just skip return statements - we already captured the assignment
				// Skip to end of line or parse the expression
				skipToNextStatement();
				continue;
			}

			// Check for "local" keyword
			bool is_local = false;
			if (matchKeyword("local")) {
				is_local = true;
				skipWhitespaceAndComments();
			}

			// Check for "function" keyword (skip entire function block)
			if (matchKeyword("function")) {
				skipFunctionBlock();
				continue;
			}

			// Try to read an identifier
			std::string name = readIdentifier();
			if (name.empty()) {
				// Not an assignment, skip this line
				skipToNextStatement();
				continue;
			}

			skipWhitespaceAndComments();

			// Check for "=" (assignment)
			if (pos_ < src_.size() && src_[pos_] == '=') {
				pos_++;
				skipWhitespaceAndComments();

				LuaValue val = parseValue();
				if (val.type != LuaValue::NIL) {
					result[name] = std::move(val);
				}
			} else {
				// Not an assignment (could be a function call, etc.), skip
				skipToNextStatement();
			}
		}

		return result;
	}

private:
	LuaValue parseValue() {
		skipWhitespaceAndComments();
		if (pos_ >= src_.size()) {
			return {};
		}

		char c = src_[pos_];

		// String literal
		if (c == '"' || c == '\'') {
			return parseString();
		}

		// Table
		if (c == '{') {
			return parseTable();
		}

		// Number (including negative)
		if (std::isdigit(c) || (c == '-' && pos_ + 1 < src_.size() && std::isdigit(src_[pos_ + 1]))) {
			return parseNumber();
		}

		// Identifier or function call (e.g., Position(x,y,z), true, false, nil)
		if (std::isalpha(c) || c == '_') {
			std::string ident = readIdentifier();
			skipWhitespaceAndComments();

			// Boolean literals
			if (ident == "true") {
				LuaValue v;
				v.type = LuaValue::NUMBER;
				v.number = 1;
				return v;
			}
			if (ident == "false" || ident == "nil") {
				LuaValue v;
				v.type = LuaValue::NUMBER;
				v.number = 0;
				return v;
			}

			// Function call: Identifier(...)
			if (pos_ < src_.size() && src_[pos_] == '(') {
				return parseFunctionCall(ident);
			}

			// Just an identifier reference - return as string
			LuaValue v;
			v.type = LuaValue::STRING;
			v.string_val = ident;
			return v;
		}

		return {};
	}

	LuaValue parseString() {
		char quote = src_[pos_++];
		std::string result;
		while (pos_ < src_.size() && src_[pos_] != quote) {
			if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
				pos_++;
				switch (src_[pos_]) {
					case 'n': result += '\n'; break;
					case 't': result += '\t'; break;
					case '\\': result += '\\'; break;
					default: result += src_[pos_]; break;
				}
			} else {
				result += src_[pos_];
			}
			pos_++;
		}
		if (pos_ < src_.size()) {
			pos_++; // skip closing quote
		}

		LuaValue v;
		v.type = LuaValue::STRING;
		v.string_val = std::move(result);
		return v;
	}

	LuaValue parseNumber() {
		size_t start = pos_;
		if (src_[pos_] == '-') {
			pos_++;
		}
		while (pos_ < src_.size() && (std::isdigit(src_[pos_]) || src_[pos_] == '.')) {
			pos_++;
		}

		double val = 0;
		try {
			val = std::stod(src_.substr(start, pos_ - start));
		} catch (...) {
			// ignore
		}

		LuaValue v;
		v.type = LuaValue::NUMBER;
		v.number = val;
		return v;
	}

	LuaValue parseTable() {
		pos_++; // skip '{'
		LuaValue table;
		table.type = LuaValue::TABLE;

		int array_index = 1;

		while (pos_ < src_.size()) {
			skipWhitespaceAndComments();
			if (pos_ >= src_.size()) {
				break;
			}

			if (src_[pos_] == '}') {
				pos_++;
				break;
			}

			// Check for [number] = value  (e.g., [29712] = { ... })
			if (src_[pos_] == '[') {
				pos_++; // skip '['
				skipWhitespaceAndComments();

				// Read the index
				LuaValue indexVal = parseValue();
				skipWhitespaceAndComments();

				if (pos_ < src_.size() && src_[pos_] == ']') {
					pos_++; // skip ']'
				}
				skipWhitespaceAndComments();

				if (pos_ < src_.size() && src_[pos_] == '=') {
					pos_++; // skip '='
					skipWhitespaceAndComments();
				}

				LuaValue val = parseValue();
				skipCommaOrSemicolon();

				// Use the number as string key
				std::string key;
				if (indexVal.type == LuaValue::NUMBER) {
					key = std::to_string(static_cast<int64_t>(indexVal.number));
				} else if (indexVal.type == LuaValue::STRING) {
					key = indexVal.string_val;
				}

				if (!key.empty()) {
					table.named_fields.emplace_back(std::move(key), std::move(val));
				}
				continue;
			}

			// Check for identifier = value
			if (std::isalpha(src_[pos_]) || src_[pos_] == '_') {
				size_t saved_pos = pos_;
				std::string ident = readIdentifier();
				skipWhitespaceAndComments();

				if (pos_ < src_.size() && src_[pos_] == '=') {
					// Named field: ident = value
					pos_++; // skip '='
					skipWhitespaceAndComments();
					LuaValue val = parseValue();
					skipCommaOrSemicolon();
					table.named_fields.emplace_back(std::move(ident), std::move(val));
					continue;
				} else {
					// Not a named field, restore position and parse as array value
					pos_ = saved_pos;
				}
			}

			// Array element (anonymous value)
			LuaValue val = parseValue();
			if (val.type != LuaValue::NIL) {
				table.array_fields.push_back(std::move(val));
			} else {
				// Can't parse, skip character to avoid infinite loop
				pos_++;
			}
			skipCommaOrSemicolon();
		}

		return table;
	}

	LuaValue parseFunctionCall(const std::string& name) {
		pos_++; // skip '('
		LuaValue v;
		v.type = LuaValue::FUNCTION_CALL;
		v.func_name = name;

		while (pos_ < src_.size()) {
			skipWhitespaceAndComments();
			if (pos_ >= src_.size()) {
				break;
			}

			if (src_[pos_] == ')') {
				pos_++;
				break;
			}

			if (src_[pos_] == ',') {
				pos_++;
				continue;
			}

			// Parse argument (should be numeric for Position)
			LuaValue arg = parseValue();
			if (arg.type == LuaValue::NUMBER) {
				v.func_args.push_back(arg.number);
			}
		}

		return v;
	}

	std::string readIdentifier() {
		size_t start = pos_;
		while (pos_ < src_.size() && (std::isalnum(src_[pos_]) || src_[pos_] == '_')) {
			pos_++;
		}
		return src_.substr(start, pos_ - start);
	}

	bool matchKeyword(const std::string& kw) {
		if (pos_ + kw.size() > src_.size()) {
			return false;
		}
		if (src_.compare(pos_, kw.size(), kw) != 0) {
			return false;
		}
		// Must be followed by non-alphanumeric
		size_t after = pos_ + kw.size();
		if (after < src_.size() && (std::isalnum(src_[after]) || src_[after] == '_')) {
			return false;
		}
		pos_ += kw.size();
		return true;
	}

	void skipWhitespaceAndComments() {
		while (pos_ < src_.size()) {
			// Whitespace
			if (std::isspace(src_[pos_])) {
				pos_++;
				continue;
			}

			// Single-line comment: -- (but not --[[ )
			if (pos_ + 1 < src_.size() && src_[pos_] == '-' && src_[pos_ + 1] == '-') {
				// Check for multi-line comment --[[ ... ]]
				if (pos_ + 3 < src_.size() && src_[pos_ + 2] == '[' && src_[pos_ + 3] == '[') {
					pos_ += 4;
					// Find closing ]]
					while (pos_ + 1 < src_.size()) {
						if (src_[pos_] == ']' && src_[pos_ + 1] == ']') {
							pos_ += 2;
							break;
						}
						pos_++;
					}
					continue;
				}

				// Single-line comment: skip to end of line
				while (pos_ < src_.size() && src_[pos_] != '\n') {
					pos_++;
				}
				continue;
			}

			break;
		}
	}

	void skipCommaOrSemicolon() {
		skipWhitespaceAndComments();
		if (pos_ < src_.size() && (src_[pos_] == ',' || src_[pos_] == ';')) {
			pos_++;
		}
	}

	void skipToNextStatement() {
		// Skip until newline or end
		while (pos_ < src_.size() && src_[pos_] != '\n') {
			pos_++;
		}
	}

	// NOTE: Does not handle keywords inside string literals -- only use with data-only Lua files
	void skipFunctionBlock() {
		// Skip until matching "end"
		int depth = 1;
		while (pos_ < src_.size() && depth > 0) {
			skipWhitespaceAndComments();
			if (pos_ >= src_.size()) {
				break;
			}

			if (matchKeyword("end")) {
				depth--;
			} else if (matchKeyword("function") || matchKeyword("if") || matchKeyword("for") || matchKeyword("while") || matchKeyword("do")) {
				depth++;
			} else {
				pos_++;
			}
		}
	}

	const std::string& src_;
	size_t pos_;
};

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

std::unordered_map<std::string, LuaValue> parseFile(const std::string& filepath) {
	std::ifstream file(filepath);
	if (!file.is_open()) {
		spdlog::warn("[LuaTableParser] Could not open file: {}", filepath);
		return {};
	}

	std::stringstream ss;
	ss << file.rdbuf();
	return parseString(ss.str());
}

std::unordered_map<std::string, LuaValue> parseString(const std::string& content) {
	Parser parser(content);
	return parser.parseTopLevel();
}

int getInt(const LuaValue& val, const std::string& key, int defaultVal) {
	const LuaValue* field = findField(val, key);
	if (field && field->type == LuaValue::NUMBER) {
		return static_cast<int>(field->number);
	}
	return defaultVal;
}

std::string getString(const LuaValue& val, const std::string& key, const std::string& defaultVal) {
	const LuaValue* field = findField(val, key);
	if (field && field->type == LuaValue::STRING) {
		return field->string_val;
	}
	return defaultVal;
}

const LuaValue* findField(const LuaValue& val, const std::string& key) {
	if (val.type != LuaValue::TABLE) {
		return nullptr;
	}
	for (const auto& [k, v] : val.named_fields) {
		if (k == key) {
			return &v;
		}
	}
	return nullptr;
}

} // namespace LuaTableParser
