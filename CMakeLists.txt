cmake_minimum_required(VERSION 3.22)

option(MV_MIPYR_USE_OPENMP "Use OpenMP - by default ON" ON)
option(MV_MIPYR_USE_AVX "Use AVX if available - by default OFF" OFF)
option(MV_MIPYR_BUILD_TESTS "Build MaskedPyramidImages tests" OFF)
option(MV_MIPYR_BUILD_TOOLS "Build MaskedPyramidImages tools" OFF)

option(MV_UNITY_BUILD "Combine target source files into batches for faster compilation" OFF)

if(MV_MIPYR_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tools")
    message(STATUS "Adding feature to vcpkg: tools")
endif()

# -----------------------------------------------------------------------------
# MaskedPyramidImages Plugins
# -----------------------------------------------------------------------------
set(PROJECT "MaskedPyramidImages")
PROJECT(${PROJECT} LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Common dependencies
# -----------------------------------------------------------------------------
find_package(Qt6 COMPONENTS Widgets WebEngineWidgets REQUIRED)

find_package(ManiVault COMPONENTS Core PointData ClusterData ImageData CONFIG QUIET)

# ManiVault core drags in zstd and zlib. When build in one project, e.g. in the "DevBundle"
# we want to use the already existing zlib_static target and introduce it to vcpkg dependencies

if(TARGET ZLIB::ZLIB)
    # Already provided (e.g. by parent project)
elseif(TARGET zlibstatic)
    message(STATUS "MaskedPyramidImages: Creating alias ZLIB::ZLIB -> zlibstatic")
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
elseif(TARGET zlib_static)
    message(STATUS "MaskedPyramidImages: Creating alias ZLIB::ZLIB -> zlib_static")
    add_library(ZLIB::ZLIB ALIAS zlib_static)
elseif(TARGET zlib)
    message(STATUS "MaskedPyramidImages: Creating alias ZLIB::ZLIB -> zlib")
    add_library(ZLIB::ZLIB ALIAS zlib)
elseif(TARGET ZLIB::ZLIBSTATIC)
    message(STATUS "MaskedPyramidImages: Creating alias ZLIB::ZLIB -> ZLIB::ZLIBSTATIC")
    add_library(ZLIB::ZLIB ALIAS ZLIB::ZLIBSTATIC)
else()
  find_package(ZLIB REQUIRED)
endif()

if(TARGET libzstd_static)
    message(STATUS "MaskedPyramidImages: Creating alias zstd::libzstd_static -> libzstd_static")
    add_library(zstd::libzstd_static ALIAS libzstd_static)
else()
     find_package(zstd CONFIG REQUIRED)
endif()

find_package(fmt CONFIG REQUIRED)
find_package(TIFF REQUIRED)
find_package(jsoncons REQUIRED)
find_package(rapidcsv CONFIG REQUIRED)

if(MV_MIPYR_USE_OPENMP)
    find_package(OpenMP)

    if(OpenMP_CXX_FOUND)
        message(STATUS "MaskedPyramidImages: Found OpenMP")
    endif()
endif()

if(UNIX AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    find_package(TBB REQUIRED)
endif()

# -----------------------------------------------------------------------------
# Plugins
# -----------------------------------------------------------------------------

set(PyramidTiffData_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PyramidTiffData/src")

add_subdirectory(PyramidTiffData)
add_subdirectory(PyramidTiffLoader)
add_subdirectory(PyramidClusterExporter)

if(MV_MIPYR_BUILD_TESTS)
    add_subdirectory(test)
endif()

if(MV_MIPYR_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
