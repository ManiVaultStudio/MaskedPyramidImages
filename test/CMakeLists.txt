cmake_minimum_required(VERSION 3.22)

project(tifftest LANGUAGES CXX)

add_executable(tifftest)

target_sources(tifftest 
    PRIVATE 
        main.cpp
        ../PyramidTiffData/src/OmeTiffPyramid.cpp
        ../PyramidTiffData/src/PolygonData.cpp
)

target_include_directories(tifftest
    PRIVATE
        ../PyramidTiffData/src
)

target_compile_features(tifftest PRIVATE cxx_std_20)

target_link_libraries(tifftest
    PRIVATE
        fmt::fmt
        TIFF::TIFF
        nlohmann_json::nlohmann_json
        jsoncons::jsoncons
)
