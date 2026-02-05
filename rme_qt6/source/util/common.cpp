#include "app/main.h"
#include "util/common.h"
#include <cstdio>
#include <format>
#include <random>
#include <regex>
#include <algorithm>
#include <QtGui/QClipboard>
#include <QtCore/QMimeData>

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
	if (auto pos = source.find_last_not_of(t); pos != std::string::npos)
		source.erase(pos + 1);
    else
        source.clear();
}

void trim_left(std::string& source, const std::string& t) {
    if (auto pos = source.find_first_not_of(t); pos != std::string::npos)
	    source.erase(0, pos);
    else
        source.clear();
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
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) return false;

    const QMimeData* mimeData = clipboard->mimeData();
    if (!mimeData || !mimeData->hasText()) return false;

    std::string input = mimeData->text().toStdString();
    if (input.empty()) return false;

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

QString b2yn(bool value) {
	return value ? "Yes" : "No";
}
