cmake_minimum_required(VERSION 3.16)

project(rme_qt6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets REQUIRED)
find_package(Boost REQUIRED)
find_package(glm REQUIRED)
find_package(spdlog REQUIRED)
find_package(LibArchive REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenGL REQUIRED)
find_package(nlohmann_json REQUIRED)

# Nanovg
add_library(nanovg STATIC
    source/ext/nanovg/src/nanovg.c
)
target_include_directories(nanovg PUBLIC source/ext/nanovg/src)

# Pugixml (using the copied sources)
add_library(pugixml STATIC
    source/ext/pugixml.cpp
)
target_include_directories(pugixml PUBLIC source/ext)


set(RME_SOURCES
    source/main.cpp
    source/app/application.cpp
    source/ui/main_window.cpp
    source/util/common.cpp
    # Add other sources here as we port them
)

set(RME_HEADERS
    source/app/application.h
    source/app/main.h
    source/app/definitions.h
    source/ui/main_window.h
)

add_executable(rme_qt6
    ${RME_SOURCES}
    ${RME_HEADERS}
)

target_include_directories(rme_qt6 PRIVATE
    source
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(rme_qt6 PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Boost::boost
    glm::glm
    spdlog::spdlog
    LibArchive::LibArchive
    ZLIB::ZLIB
    OpenGL::GL
    nanovg
    pugixml
    nlohmann_json::nlohmann_json
)
