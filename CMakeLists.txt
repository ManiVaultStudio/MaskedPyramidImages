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

message(STATUS "Test: ZLIB_FOUND=${ZLIB_FOUND} ZLIB_LIBRARIES=${ZLIB_LIBRARIES}")

if(NOT TARGET ZLIB::ZLIB)
    message(FATAL_ERROR "ZLIB::ZLIB target missing! ZLIB_FOUND=${ZLIB_FOUND} ZLIB_LIBRARIES=${ZLIB_LIBRARIES}")
else()
    message(STATUS "ZLIB::ZLIB found with ZLIB_FOUND=${ZLIB_FOUND} ZLIB_LIBRARIES=${ZLIB_LIBRARIES}")
endif()

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(TIFF REQUIRED)

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
