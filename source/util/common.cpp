//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"

#include "util/common.h"
#include "math.h"

#include <cstdio>
#include <format>
#include <random>
#include <regex>
#include <algorithm>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

// random generator
std::mt19937& getRandomGenerator() {
	static std::random_device rd;
	thread_local std::mt19937 generator(rd());
	return generator;
}

int32_t uniform_random(int32_t minNumber, int32_t maxNumber) {
	static std::uniform_int_distribution<int32_t> uniformRand;
	if (minNumber == maxNumber) {
		return minNumber;
	} else if (minNumber > maxNumber) {
		std::swap(minNumber, maxNumber);
	}
	return uniformRand(getRandomGenerator(), std::uniform_int_distribution<int32_t>::param_type(minNumber, maxNumber));
}

int32_t uniform_random(int32_t maxNumber) {
	return uniform_random(0, maxNumber);
}

//
std::string i2s(const int _i) {
	return std::to_string(_i);
}

std::string f2s(const double _d) {
	return std::format("{:.6g}", _d);
}

int s2i(const std::string s) {
	return atoi(s.c_str());
}

double s2f(const std::string s) {
	return atof(s.c_str());
}

wxString i2ws(const int _i) {
	return QString::number(_i);
}

wxString f2ws(const double _d) {
	return QString::number(_d);
}

int ws2i(const wxString s) {
    bool ok;
    int val = s.toInt(&ok);
	return ok ? val : 0;
}

double ws2f(const wxString s) {
    bool ok;
    double val = s.toDouble(&ok);
	return ok ? val : 0.0;
}

void replaceString(std::string& str, std::string_view sought, std::string_view replacement) {
	size_t pos = 0;
	size_t soughtLen = sought.length();
	size_t replaceLen = replacement.length();
	while ((pos = str.find(sought, pos)) != std::string::npos) {
		str.replace(pos, soughtLen, replacement);
		pos += replaceLen;
	}
}

void trim_right(std::string& source, const std::string& t) {
	source.erase(source.find_last_not_of(t) + 1);
}

void trim_left(std::string& source, const std::string& t) {
	source.erase(0, source.find_first_not_of(t));
}

void to_lower_str(std::string& source) {
	std::transform(source.begin(), source.end(), source.begin(), tolower);
}

void to_upper_str(std::string& source) {
	std::transform(source.begin(), source.end(), source.begin(), toupper);
}

std::string as_lower_str(const std::string& other) {
	std::string ret = other;
	to_lower_str(ret);
	return ret;
}

std::string as_upper_str(const std::string& other) {
	std::string ret = other;
	to_upper_str(ret);
	return ret;
}

bool isFalseString(std::string& str) {
	if (str == "false" || str == "0" || str == "" || str == "no" || str == "not") {
		return true;
	}
	return false;
}

bool isTrueString(std::string& str) {
	return !isFalseString(str);
}

int random(int low, int high) {
	return uniform_random(low, high);
}

int random(int high) {
	return random(0, high);
}

std::wstring string2wstring(const std::string& utf8string) {
    return QString::fromStdString(utf8string).toStdWString();
}

std::string wstring2string(const std::wstring& widestring) {
    return QString::fromStdWString(widestring).toStdString();
}

bool posFromClipboard(Position& position, const int mapWidth /* = MAP_MAX_WIDTH */, const int mapHeight /* = MAP_MAX_HEIGHT */) {
    QClipboard *clipboard = QGuiApplication::clipboard();
    QString text = clipboard->text();

	if (text.isEmpty()) {
		return false;
	}

    std::string input = text.toStdString();
	bool done = false;
	std::smatch matches;
	static const std::regex expression = std::regex(R"(.*?(\d+).*?(\d+).*?(\d+).*?)", std::regex_constants::ECMAScript);
	if (std::regex_match(input, matches, expression)) {
		try {
			const int tmpX = std::stoi(matches.str(1));
			const int tmpY = std::stoi(matches.str(2));
			const int tmpZ = std::stoi(matches.str(3));

			const Position pastedPos = Position(tmpX, tmpY, tmpZ);
			if (pastedPos.isValid() && tmpX <= mapWidth && tmpY <= mapHeight) {
				position.x = tmpX;
				position.y = tmpY;
				position.z = tmpZ;
				done = true;
			}
		} catch (const std::out_of_range&) { }
	}

	return done;
}

wxString b2yn(bool value) {
	return value ? "Yes" : "No";
}

QColor colorFromEightBit(int color) {
	if (color <= 0 || color >= 216) {
		return QColor(0, 0, 0);
	}
	const uint8_t red = (uint8_t)(int(color / 36) % 6 * 51);
	const uint8_t green = (uint8_t)(int(color / 6) % 6 * 51);
	const uint8_t blue = (uint8_t)(color % 6 * 51);
	return QColor(red, green, blue);
}
