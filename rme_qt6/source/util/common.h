#ifndef RME_COMMONS_H_
#define RME_COMMONS_H_

#include "app/main.h" // Includes Qt headers
#include "map/position.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <iomanip>
#include <string>
#include <string_view>
#include <random>

inline bool testFlags(size_t flags, size_t test) {
	return (flags & test) != 0;
}

int32_t uniform_random(int32_t minNumber, int32_t maxNumber);
int32_t uniform_random(int32_t maxNumber);

// Function-like convertions between float, int and doubles
std::string i2s(int i);
std::string f2s(double i);
int s2i(std::string s);
double s2f(std::string s);

// Qt replacements
inline QString i2qs(int i) { return QString::number(i); }
inline QString f2qs(double i) { return QString::number(i); }
inline int qs2i(const QString& s) { return s.toInt(); }
inline double qs2f(const QString& s) { return s.toDouble(); }

// wxString shims for compatibility during porting (if strictly needed, but better to avoid)
// We remove them to force compile errors on wx usage.

// replaces all instances of sought in str with replacement
void replaceString(std::string& str, std::string_view sought, std::string_view replacement);
// Removes all characters in t from source (from either start or beginning of the string)
void trim_right(std::string& source, const std::string& t);
void trim_left(std::string& source, const std::string& t);
// Converts the argument to lower/uppercase
void to_lower_str(std::string& source);
void to_upper_str(std::string& source);
std::string as_lower_str(const std::string& other);
std::string as_upper_str(const std::string& other);

bool isFalseString(std::string& str);
bool isTrueString(std::string& str);

int random(int high);
int random(int low, int high);

// Unicode conversions
std::wstring string2wstring(const std::string& utf8string);
std::string wstring2string(const std::wstring& widestring);

// Gets position values from ClipBoard - Ported to Qt
bool posFromClipboard(Position& position, const int mapWidth = MAP_MAX_WIDTH, const int mapHeight = MAP_MAX_HEIGHT);

// Returns 'yes' if the defined value is true or 'no' if it is false.
QString b2yn(bool v);

// wxColor colorFromEightBit(int color); // Disabled until we have a QColor variant if needed

// Standard math functions
template <class T>
inline T abs(T t) {
	return (t < 0 ? -t : t);
}

template <class T, class U>
inline T min(T t, U u) {
	return (t < u ? t : u);
}

template <class T, class U>
T max(T t, U u) {
	return (t > u ? t : u);
}

template <class T, class U, class V>
inline T min(T t, U u, V v) {
	T min = t;
	if (u < min) {
		min = u;
	}
	if (v < min) {
		min = v;
	}
	return min;
}

template <class T, class U, class V>
inline T max(T t, U u, V v) {
	T max = t;
	if (u > max) {
		max = u;
	}
	if (v > max) {
		max = v;
	}
	return max;
}

#endif
