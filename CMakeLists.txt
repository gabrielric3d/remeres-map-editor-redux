cmake_minimum_required(VERSION 3.28)

# CMake 4.x removed compatibility with cmake_minimum_required < 3.5.
# Some vcpkg ports (e.g. glad) still use older minimum versions.
# This setting allows those ports to configure without errors.
if(POLICY CMP0174)
	cmake_policy(SET CMP0174 OLD)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version for compatibility with older vcpkg ports")

project(rme)

# Limit parallel compilation to avoid C1060 (compiler out of heap space).
# wxWidgets + heavy templates can consume several GB per cl.exe instance.
# Override with -DRME_PARALLEL_COMPILE=N at configure time if needed.
set(RME_PARALLEL_COMPILE "4" CACHE STRING "Max parallel compile jobs")
set(RME_PARALLEL_LINK    "2" CACHE STRING "Max parallel link jobs")

# Threads de geracao de codigo por cl.exe (/cgthreads, MSVC). O padrao do
# compilador e 4; cada thread mantem seu proprio heap do otimizador, entao
# reduzir para 2 corta o PICO de RAM por processo sem perder throughput (o
# paralelismo real vem do /MP acima, que ja distribui por arquivo).
# Baixe para 1 se a maquina ainda faltar memoria durante o build.
set(RME_CODEGEN_THREADS  "2" CACHE STRING "Codegen threads per cl.exe (/cgthreads); lower = less RAM")

if(MSVC)
	add_compile_options(/MP${RME_PARALLEL_COMPILE})
else()
	set(CMAKE_JOB_POOL_COMPILE compile_pool)
	set(CMAKE_JOB_POOL_LINK link_pool)
	set_property(GLOBAL PROPERTY JOB_POOLS
		compile_pool=${RME_PARALLEL_COMPILE}
		link_pool=${RME_PARALLEL_LINK})
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Explicitly set configuration types for multi-config generators
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel" CACHE STRING "" FORCE)

# Compiler-specific flags
if(MSVC)
	set(CMAKE_CXX_FLAGS                "/W3 /wd4996 /wd4800 /wd4100 /wd4706 /EHsc /cgthreads${RME_CODEGEN_THREADS}")
	set(CMAKE_CXX_FLAGS_DEBUG          "/Od /Zi /D__DEBUG__")
	set(CMAKE_CXX_FLAGS_MINSIZEREL     "/O1 /DNDEBUG")
	set(CMAKE_CXX_FLAGS_RELEASE        "/O2 /DNDEBUG")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/O2 /Zi /DNDEBUG")
	# LTCG desativado: reduz uso de RAM no link (~6 GB → ~1 GB).
	# set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
	set(CMAKE_CXX_FLAGS                "-Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-deprecated-declarations -Wno-overloaded-virtual -Wno-strict-aliasing -Wno-sign-compare -Wno-unused-function -Wunused-result")
	set(CMAKE_CXX_FLAGS_DEBUG          "-O0 -g -D__DEBUG__")
	set(CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
	set(CMAKE_CXX_FLAGS_RELEASE        "-O3 -DNDEBUG")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
endif()

find_package(OpenGL REQUIRED)


if(WIN32)
    set(Boost_THREADAPI win32)
endif()
find_package(Boost 1.34.0 COMPONENTS thread system REQUIRED)

find_package(wxWidgets 3.2 COMPONENTS html aui gl adv core net base propgrid REQUIRED)


find_package(ZLIB REQUIRED)


# wxWidgets_USE_FILE is set by vcpkg/system wxWidgets, but empty with Conan
if(wxWidgets_USE_FILE)
	include(${wxWidgets_USE_FILE})
endif()

find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(unofficial-lua CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)

# nanovg: try vcpkg/Conan first, fallback to ext/nanovg
find_package(nanovg CONFIG QUIET)
if(NOT nanovg_FOUND)
	message(STATUS "nanovg not found via package manager, using ext/nanovg...")
	
	# Build nanovg from ext/nanovg source
	add_library(nanovg_lib STATIC
		${CMAKE_CURRENT_SOURCE_DIR}/ext/nanovg/src/nanovg.c
	)
	target_include_directories(nanovg_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ext/nanovg/src)
	add_library(nanovg::nanovg ALIAS nanovg_lib)
endif()

include(source/CMakeLists.txt)
# Removed WIN32 to enable Console subsystem for persistent debugging logs
add_executable(rme ${rme_H} ${rme_SRC})

set_target_properties(rme PROPERTIES CXX_STANDARD 20)
set_target_properties(rme PROPERTIES CXX_STANDARD_REQUIRED ON)
target_compile_definitions(rme PRIVATE WXWIN_COMPATIBILITY_3_3 NANOVG_GL3)

# ------------------------------------------------------------------
# Reduz o PICO de RAM na compilacao dos poucos arquivos-fonte gigantes.
# Estes dialogs/telas tem 2000-3600 linhas e o otimizador /O2 do MSVC
# consome varios GB de RAM em CADA um. Como nao sao codigo de desempenho
# critico (UI/dialogos/scripts), compilamos com /O1 (otimiza tamanho, bem
# mais leve para o otimizador). Isso corta o pico de RAM exatamente nos
# arquivos que fazem o PC travar/paginar durante o build, sem tocar na
# velocidade do resto do projeto. Nao se aplica em Debug (mantem /Od).
# ------------------------------------------------------------------
if(MSVC)
	set(RME_HEAVY_SOURCES
		${CMAKE_CURRENT_SOURCE_DIR}/source/ui/dialogs/border_editor_dialog.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/source/ui/dialogs/area_decoration_dialog.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/source/lua/lua_dialog.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/source/editor/area_decoration.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/source/ui/dialogs/structure_manager_window.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/source/ui/dialogs/doodad_editor_dialog.cpp
	)
	# /O1 sobrepoe o /O2 herdado das flags de Release/RelWithDebInfo (o ultimo
	# vence). O warning D9025 (overriding /O2 with /O1) e esperado e inofensivo.
	set_source_files_properties(${RME_HEAVY_SOURCES} PROPERTIES
		COMPILE_OPTIONS "$<$<NOT:$<CONFIG:Debug>>:/O1>")
endif()

# Copy assets to build directory
add_custom_command(TARGET rme POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	"${CMAKE_CURRENT_SOURCE_DIR}/source/assets"
	"$<TARGET_FILE_DIR:rme>/assets"
	COMMENT "Copying assets to output directory..."
)


# Link against wxWidgets (works with both vcpkg and Conan)
if(TARGET wxWidgets::wxWidgets)
	# Conan provides a single wxWidgets::wxWidgets target
	target_link_libraries(rme PRIVATE wxWidgets::wxWidgets)
else()
	# vcpkg/system provides wxWidgets_LIBRARIES variable
	target_link_libraries(rme PRIVATE ${wxWidgets_LIBRARIES})
endif()

# Include directories (for legacy find modules)
target_include_directories(rme PRIVATE
	${Boost_INCLUDE_DIRS}
	${OPENGL_INCLUDE_DIR}

	${ZLIB_INCLUDE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/source
)

# Link other libraries (works with both vcpkg targets and Conan targets)
if(TARGET Boost::boost)
	target_link_libraries(rme PRIVATE Boost::boost Boost::thread Boost::system)
else()
	target_link_libraries(rme PRIVATE ${Boost_LIBRARIES})
endif()


if(TARGET ZLIB::ZLIB)
	target_link_libraries(rme PRIVATE ZLIB::ZLIB)
else()
	target_link_libraries(rme PRIVATE ${ZLIB_LIBRARIES})
endif()

target_link_libraries(rme PRIVATE ${OPENGL_LIBRARIES})
target_link_libraries(rme PRIVATE glad::glad)
target_link_libraries(rme PRIVATE glm::glm)
target_link_libraries(rme PRIVATE nanovg::nanovg)
target_link_libraries(rme PRIVATE spdlog::spdlog)
target_link_libraries(rme PRIVATE tomlplusplus::tomlplusplus)
target_link_libraries(rme PRIVATE unofficial::lua::lua)
target_link_libraries(rme PRIVATE cpr::cpr)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "rme")



