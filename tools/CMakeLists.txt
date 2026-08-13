cmake_minimum_required(VERSION 3.22)

project(pyramidstacktools LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
find_package(CLI11 CONFIG REQUIRED)

# -----------------------------------------------------------------------------
# Executable
# -----------------------------------------------------------------------------
add_executable(pyramidstackmerge)

target_sources(pyramidstackmerge 
    PRIVATE 
        main.cpp
        UtilsRoiArrangement.cpp
        ../PyramidTiffData/src/OmeTiffPyramid.cpp
        ../PyramidTiffData/src/PolygonData.cpp
        ../PyramidTiffData/src/CommonTypesAndTransformations.cpp
        ../PyramidTiffData/src/UtilsJson.cpp
        ../PyramidTiffData/src/UtilsFiles.cpp
)

target_include_directories(pyramidstackmerge
    PRIVATE
        ../PyramidTiffData/src
)

# -----------------------------------------------------------------------------
# Linking
# -----------------------------------------------------------------------------
target_link_libraries(pyramidstackmerge
    PRIVATE
        fmt::fmt
        TIFF::TIFF
        jsoncons::jsoncons
        CLI11::CLI11
)

# -----------------------------------------------------------------------------
# Properties
# -----------------------------------------------------------------------------
if(MV_MIPYR_USE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(pyramidstackmerge PRIVATE OpenMP::OpenMP_CXX)
endif()

target_compile_features(pyramidstackmerge PRIVATE cxx_std_20)

mv_check_and_set_AVX(pyramidstackmerge ${MV_MIPYR_USE_AVX})

