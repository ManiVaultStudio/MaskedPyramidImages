cmake_minimum_required(VERSION 3.22)

project(pyramidstacktools LANGUAGES CXX)

add_executable(pyramidstackmerge)

target_sources(pyramidstackmerge 
    PRIVATE 
        main.cpp
        UtilsRoiArrangement.cpp
        ../PyramidTiffData/src/OmeTiffPyramid.cpp
        ../PyramidTiffData/src/PolygonData.cpp
        ../PyramidTiffData/src/CommonTypesAndTransformations.cpp
        ../PyramidTiffData/src/UtilsJson.cpp
)

target_include_directories(pyramidstackmerge
    PRIVATE
        ../PyramidTiffData/src
)

target_compile_features(pyramidstackmerge PRIVATE cxx_std_20)

target_link_libraries(pyramidstackmerge
    PRIVATE
        fmt::fmt
        TIFF::TIFF
        jsoncons::jsoncons
)

if(MV_MIPYR_USE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(pyramidstackmerge PRIVATE OpenMP::OpenMP_CXX)
endif()

mv_check_and_set_AVX(pyramidstackmerge ${MV_MIPYR_USE_AVX})

