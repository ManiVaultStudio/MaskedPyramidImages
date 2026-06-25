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

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(TIFF REQUIRED)

if(MV_MIPYR_USE_OPENMP)
    find_package(OpenMP)

    if(OpenMP_CXX_FOUND)
        message(STATUS "UMAPAnalysisPlugin: Found OpenMP")
    endif()
endif()

# -----------------------------------------------------------------------------
# Plugins
# -----------------------------------------------------------------------------

set(PyramidTiffData_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PyramidTiffData/src")

add_subdirectory(PyramidTiffData)
add_subdirectory(PyramidTiffLoader)
add_subdirectory(test)
