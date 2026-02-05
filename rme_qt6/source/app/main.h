#ifndef RME_MAIN_H_
#define RME_MAIN_H_

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#ifdef _WIN32_WINNT
		#undef _WIN32_WINNT
	#endif
	#define _WIN32_WINNT 0x0501
#endif

// Memory debugging
#ifdef DEBUG_MEM
	#define _CRTDBG_MAP_ALLOC
	#include <stdlib.h>
	#include <crtdbg.h>
	#pragma warning(disable : 4291)
    // Simplified newd
	#define newd new
#else
	#define newd new
#endif

// Boost libraries
#include <boost/utility.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/asio.hpp>

// Definitions
#include "app/definitions.h"

// Qt Includes
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QWidget>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtCore/QList>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>

// Standard C++
#include <cmath>
#include <list>
#include <vector>
#include <map>
#include <string>
#include <istream>
#include <ostream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <set>
#include <queue>
#include <stdexcept>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <cassert>

// PugiXML
#include "ext/pugixml.hpp"

// Libarchive
#ifdef OTGZ_SUPPORT
	#include <archive.h>
	#include <archive_entry.h>
#endif

// Macros
#define _MSG(msg) !bool(msg)
#ifdef __DEBUG__
	#define ASSERT assert
#else
	#define ASSERT(x)
#endif

// Type definitions
using StringVector = std::vector<std::string>;
using FileName = std::filesystem::path;

// Utilities
#include "util/json.h"
#include "util/con_vector.h"
#include "util/common.h"
#include "app/threads.h"

#include "app/rme_forward_declarations.h"

// Helper for Qt porting
#define FROM_DIP(widget, size) size

#endif
