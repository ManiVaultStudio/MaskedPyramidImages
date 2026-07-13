cmake_minimum_required(VERSION 3.22)

option(MV_MIPYR_USE_OPENMP "Use OpenMP - by default ON" ON)
option(MV_MIPYR_USE_AVX "Use AVX if available - by default OFF" OFF)

option(MV_UNITY_BUILD "Combine target source files into batches for faster compilation" OFF)

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

# Force FindZLIB to discover vcpkg's copy and create the ZLIB::ZLIB imported target
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(ZLIB_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}" CACHE PATH "vcpkg zlib root" FORCE)
    set(ZLIB_INCLUDE_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include" CACHE PATH "" FORCE)
endif()

find_package(ZLIB REQUIRED)

# Workaround: FindZLIB on Linux CI can set ZLIB_FOUND=1 but leave
# ZLIB_LIBRARIES empty and fails to create the ZLIB::ZLIB imported target.
# Detect and repair this case before find_package(TIFF) runs.
if(NOT TARGET ZLIB::ZLIB)
    message(STATUS "MaskedPyramidImages: ZLIB::ZLIB target missing despite ZLIB_FOUND=${ZLIB_FOUND}, creating manually")
    find_library(ZLIB_LIBRARY_MANUAL NAMES z zlib zlib_static REQUIRED)
    add_library(ZLIB::ZLIB UNKNOWN IMPORTED)
    set_target_properties(ZLIB::ZLIB PROPERTIES
        IMPORTED_LOCATION             "${ZLIB_LIBRARY_MANUAL}"
        INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIRS}"
    )
endif()

# ManiVault core drags in zstd. When build in one project, e.g. in the "DevBundle"
# we want to use the already existing zlib_static target and introduce it to vcpkg dependencies
if(TARGET libzstd_static)
    message(STATUS "MaskedPyramidImages: Creating alias zstd::libzstd_static -> libzstd_static")
    add_library(zstd::libzstd_static ALIAS libzstd_static)
endif()

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(TIFF REQUIRED)
find_package(jsoncons REQUIRED)

if(MV_MIPYR_USE_OPENMP)
    find_package(OpenMP)

    if(OpenMP_CXX_FOUND)
        message(STATUS "MaskedPyramidImages: Found OpenMP")
    endif()
endif()

# -----------------------------------------------------------------------------
# Plugins
# -----------------------------------------------------------------------------

set(PyramidTiffData_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PyramidTiffData/src")

add_subdirectory(PyramidTiffData)
add_subdirectory(PyramidTiffLoader)
add_subdirectory(test)
