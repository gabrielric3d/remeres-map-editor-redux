//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_MAIN_H_
#define RME_MAIN_H_

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#ifdef _WIN32_WINNT
		#undef _WIN32_WINNT
	#endif
	#define _WIN32_WINNT 0x0501
#endif

#ifdef DEBUG_MEM
	#define _CRTDBG_MAP_ALLOC
	#include <stdlib.h>
	#include <crtdbg.h>

	#pragma warning(disable : 4291)
    inline void* __CRTDECL operator new(size_t _Size, const char* file, int line) {
        return ::operator new(_Size, _NORMAL_BLOCK, file, line);
    }
    inline void* __CRTDECL operator new[](size_t _Size, const char* file, int line) {
        return ::operator new[](_Size, _NORMAL_BLOCK, file, line);
    }
	#define newd new (__FILE__, __LINE__)
#else
	#define newd new
#endif

// Boost libraries
#include <boost/utility.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/asio.hpp>

#include "app/definitions.h"

#include <glad/glad.h>

// Qt Headers
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QDebug>
#include <QtCore/QObject>
#include <QtGui/QIcon>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>

// Standard C++
#include <math.h>
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
#include <time.h>
#include <fstream>
#include <filesystem>

// Compatibility layer for wxWidgets removal
using wxString = QString;
using wxArrayString = QStringList;
using StringVector = std::vector<std::string>;

class FileName : public std::filesystem::path {
public:
    using std::filesystem::path::path;
    FileName() = default;
    FileName(const std::filesystem::path& p) : std::filesystem::path(p) {}
    FileName(const char* p) : std::filesystem::path(p) {}
    FileName(const std::string& p) : std::filesystem::path(p) {}
    FileName(const wxString& p) : std::filesystem::path(p.toStdString()) {}

    // wxFileName compatibility
    bool FileExists() const { return std::filesystem::exists(*this) && std::filesystem::is_regular_file(*this); }
    bool DirExists() const { return std::filesystem::exists(*this) && std::filesystem::is_directory(*this); }

    std::string GetPath() const { return parent_path().string(); }
    std::string GetFullName() const { return filename().string(); }
    std::string GetName() const { return stem().string(); }

    std::string GetExt() const {
        std::string e = extension().string();
        if(e.size() > 0 && e[0] == '.') return e.substr(1);
        return e;
    }

    void Assign(const std::string& s) { *this = s; }

    bool IsOk() const { return !empty(); }

    // Returns QString to be compatible with wxString expectations in some places?
    // Or std::string.
    // wxFileName::GetFullPath returns wxString.
    wxString GetFullPath() const { return QString::fromStdString(string()); }
};

using wxFileName = FileName;

#include <assert.h>
#define _MSG(msg) !bool(msg)
#ifdef __DEBUG__
	#define ASSERT assert
#else
	#define ASSERT(...)
#endif

#include "util/json.h"
#include "util/con_vector.h"
#include "util/common.h"
#include "app/threads.h"

#include "app/rme_forward_declarations.h"

// Define a macro for FromDIP since it was used in wx
#define FROM_DIP(widget, size) size

// Define wxID_ANY for compatibility
#define wxID_ANY -1
#define wxANY_ID wxID_ANY

// Stub wx classes/macros found in common usage, to be refactored
#define wxT(x) QString(x)
#define _(x) QString(x)
#define wxEmptyString QString()
#define wxMessageBox(msg, title, style, parent) qDebug() << "MessageBox:" << title << ":" << msg

// wxWindowDisabler stub
class wxWindowDisabler {
public:
    wxWindowDisabler(QWidget* win = nullptr) {}
    ~wxWindowDisabler() {}
};

// wxStopWatch stub
class wxStopWatch {
public:
    void Start() {}
    long Time() const { return 0; }
};

// wxConvUTF8 stub (dummy object)
class WxConvUTF8 {};
inline WxConvUTF8 wxConvUTF8;

#endif
