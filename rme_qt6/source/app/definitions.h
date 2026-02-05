#ifndef RME_DEFINITIONS_H_
#define RME_DEFINITIONS_H_

#include <cstdint>
#include <string>

// To avoid circular dependency with common.h if it is included here
// We just use standard string concatenation here or avoid it.
// We will depend on QCoreApplication for app name in code, but here we define constants.

#define __RME_APPLICATION_NAME_STR__ "OTAcademy Map Editor"

// Version info
// xxyyzzt (major, minor, subversion)
#define __RME_VERSION_MAJOR__ 4
#define __RME_VERSION_MINOR__ 1
#define __RME_SUBVERSION__ 2

#define __LIVE_NET_VERSION__ 5

#define MAKE_VERSION_ID(major, minor, subversion) \
	((major) * 10000000 + (minor) * 100000 + (subversion) * 1000)

#define __RME_VERSION_ID__ MAKE_VERSION_ID( \
	__RME_VERSION_MAJOR__,                  \
	__RME_VERSION_MINOR__,                  \
	__RME_SUBVERSION__                      \
)

#define __SITE_URL__ "https://github.com/OTAcademy/RME"

// #define __PRERELEASE__ 1
// Helper for version string construction
#define TO_STR_HELPER(x) #x
#define TO_STR(x) TO_STR_HELPER(x)

#ifdef __EXPERIMENTAL__
	#define __RME_VERSION__ std::string(std::to_string(__RME_VERSION_MAJOR__) + "." + std::to_string(__RME_VERSION_MINOR__) + "." + std::to_string(__RME_SUBVERSION__) + " BETA")
#elif __SNAPSHOT__
	#define __RME_VERSION__ std::string(std::to_string(__RME_VERSION_MAJOR__) + "." + std::to_string(__RME_VERSION_MINOR__) + "." + std::to_string(__RME_SUBVERSION__) + " - SNAPSHOT")
#elif __PRERELEASE__
	#define __RME_VERSION__ std::string(std::to_string(__RME_VERSION_MAJOR__) + "." + std::to_string(__RME_VERSION_MINOR__) + "." + std::to_string(__RME_SUBVERSION__) + " (Pre-release)")
#else
	#define __RME_VERSION__ std::string(std::to_string(__RME_VERSION_MAJOR__) + "." + std::to_string(__RME_VERSION_MINOR__) + "." + std::to_string(__RME_SUBVERSION__))
#endif
// OS

#define OTGZ_SUPPORT 1
#define ASSETS_NAME "Tibia"

#ifdef _MSC_VER
	#pragma warning(disable : 4996)
	#pragma warning(disable : 4800)
	#pragma warning(disable : 4100)
	#pragma warning(disable : 4706)
#endif

#ifndef FORCEINLINE
	#ifdef _MSC_VER
		#define FORCEINLINE __forceinline
	#else
		#define FORCEINLINE inline
	#endif
#endif

// Debug mode?
#if defined __DEBUG__ || defined _DEBUG || defined __DEBUG_MODE__
	#undef __DEBUG_MODE__
	#define __DEBUG_MODE__ 1
	#undef _DEBUG
	#define _DEBUG 1
	#undef __DEBUG__
	#define __DEBUG__ 1
#else
	#ifndef __RELEASE__
		#define __RELEASE__ 1
	#endif
	#ifndef NDEBUG
		#define NDEBUG 1
	#endif
#endif

#ifndef _DONT_USE_UPDATER_
	#if defined _WIN32 && !defined _USE_UPDATER_
		#define _USE_UPDATER_
	#endif
#endif

#ifndef _DONT_USE_PROCESS_COM
	#if defined _WIN32 && !defined _USE_PROCESS_COM
		#define _USE_PROCESS_COM
	#endif
#endif

// Mathematical constants
#define PI 3.14159265358979323846264338327950288419716939937510
#define DEG2RAD (PI / 180.0)
#define RAD2DEG (180.0 / DEG)

// The height of the map (there should be more checks for this...)
#define MAP_LAYERS 16

#define MAP_MAX_WIDTH 65000
#define MAP_MAX_HEIGHT 65000
#define MAP_MAX_LAYER 15

// Sanity limit for sprite counts
constexpr std::uint32_t MAX_SPRITES = 3000000;

// The size of the tile in pixels
constexpr int TileSize = 32;

// The default size of sprites
#define SPRITE_PIXELS 32
#define SPRITE_PIXELS_SIZE SPRITE_PIXELS* SPRITE_PIXELS

// The sea layer
#define GROUND_LAYER 7

constexpr int ClientMapWidth = 17;
constexpr int ClientMapHeight = 13;

// Qt Filter format: "Description (*.ext)"
#define MAP_LOAD_FILE_WILDCARD_OTGZ "OpenTibia Binary Map (*.otbm *.otgz)"
#define MAP_SAVE_FILE_WILDCARD_OTGZ "OpenTibia Binary Map (*.otbm);;Compressed OpenTibia Binary Map (*.otgz)"

#define MAP_LOAD_FILE_WILDCARD "OpenTibia Binary Map (*.otbm)"
#define MAP_SAVE_FILE_WILDCARD "OpenTibia Binary Map (*.otbm)"

// Lights
constexpr int MaxLightIntensity = 8;
constexpr int PixelFormatRGB = 3;
constexpr int PixelFormatRGBA = 4;

// increment & decrement definitions
#define IMPLEMENT_INCREMENT_OP(Type)                     \
	namespace {                                          \
		Type& operator++(Type& type) {                   \
			return (type = static_cast<Type>(type + 1)); \
		}                                                \
		Type operator++(Type& type, int) {               \
			return static_cast<Type>((++type) - 1);      \
		}                                                \
	}

#define IMPLEMENT_DECREMENT_OP(Type)                     \
	namespace {                                          \
		Type& operator--(Type& type) {                   \
			return (type = static_cast<Type>(type - 1)); \
		}                                                \
		Type operator--(Type& type, int) {               \
			return static_cast<Type>((--type) + 1);      \
		}                                                \
	}

#endif
