cmake_minimum_required(VERSION 3.22)

# -----------------------------------------------------------------------------
# PyramidTiffData Plugin
# -----------------------------------------------------------------------------
set(PYRAMIDTIFFDATA "PyramidTiffData")
PROJECT(${PYRAMIDTIFFDATA} 
        LANGUAGES CXX)

# -----------------------------------------------------------------------------
# CMake Options
# -----------------------------------------------------------------------------
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /DWIN32 /EHsc /MP /Zc:__cplusplus")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MD")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
endif()

# Include cmake tools
include(GenerateExportHeader)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# Source files
# -----------------------------------------------------------------------------
set(PYRAMID_TIFF_HEADERS
    src/PyramidTiffData.h
    src/PyramidInfoAction.h
)

set(PYRAMID_TIFF_SOURCES
    src/PyramidTiffData.cpp
    src/PyramidInfoAction.cpp
)

set(PYRAMID_UTILS_HEADERS
    src/OmeTiffPyramid.h
    src/PolygonData.h
)

set(PYRAMID_UTILS_SOURCES
    src/OmeTiffPyramid.cpp
    src/PolygonData.cpp
)

set(AUX
    PluginInfo.json
)

string(TOLOWER ${PYRAMIDTIFFDATA} PYRAMIDTIFFDATA_LOWER)
set(PYRAMIDTIFFDATA_EXPORT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/${PYRAMIDTIFFDATA_LOWER}_export.h")
list(APPEND PYRAMID_TIFF_HEADERS ${PYRAMIDTIFFDATA_EXPORT_HEADER})

source_group(Plugin FILES ${PYRAMID_TIFF_HEADERS} ${PYRAMID_TIFF_SOURCES} ${PYRAMID_UTILS_HEADERS} ${PYRAMID_UTILS_SOURCES})
source_group(Aux FILES ${AUX})

# -----------------------------------------------------------------------------
# CMake Target
# -----------------------------------------------------------------------------
add_library(${PYRAMIDTIFFDATA} SHARED ${PYRAMID_TIFF_HEADERS} ${PYRAMID_TIFF_SOURCES} ${PYRAMID_UTILS_HEADERS} ${PYRAMID_UTILS_SOURCES} ${AUX})
add_library(ManiVault::${PYRAMIDTIFFDATA} ALIAS ${PYRAMIDTIFFDATA})

# Generate a header file that contains the EXPORT macro for this library.
generate_export_header(${PYRAMIDTIFFDATA} EXPORT_FILE_NAME ${PYRAMIDTIFFDATA_EXPORT_HEADER})

# -----------------------------------------------------------------------------
# Target include directories
# -----------------------------------------------------------------------------
target_include_directories(${PYRAMIDTIFFDATA}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:"include/PyramidTiffData">
    PRIVATE
        "${ManiVault_INCLUDE_DIR}"
)

# -----------------------------------------------------------------------------
# Target library linking
# -----------------------------------------------------------------------------
target_link_libraries(${PYRAMIDTIFFDATA}
    PRIVATE
        Qt6::Widgets
        Qt6::WebEngineWidgets
        ManiVault::Core
        ManiVault::PointData
        ManiVault::ClusterData
        ManiVault::ImageData
        TIFF::TIFF
        nlohmann_json::nlohmann_json
        fmt::fmt
)

if(MV_MIPYR_USE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(${PYRAMIDTIFFDATA} PRIVATE OpenMP::OpenMP_CXX)
endif()

# -----------------------------------------------------------------------------
# Target properties
# -----------------------------------------------------------------------------
target_compile_features(${PYRAMIDTIFFDATA} PRIVATE cxx_std_20)

set_target_properties(${PYRAMIDTIFFDATA} PROPERTIES 
    AUTOMOC ON
    UNITY_BUILD ${MV_UNITY_BUILD}
    PUBLIC_HEADER "${PYRAMID_TIFF_HEADERS}"
    FOLDER "MaskedImagePyramidPlugins"
)

# Instruction sets
mv_check_and_set_AVX(${PYRAMIDTIFFDATA} ${MV_MIPYR_USE_AVX})

# File name suffix
mv_handle_plugin_config(${PYRAMIDTIFFDATA} OFF)

# -----------------------------------------------------------------------------
# Target installation
# -----------------------------------------------------------------------------
install(TARGETS ${PYRAMIDTIFFDATA}
    RUNTIME DESTINATION Plugins COMPONENT PLUGINS # Windows .dll
    LIBRARY DESTINATION Plugins COMPONENT PLUGINS # Linux/Mac .so
    ARCHIVE DESTINATION lib     COMPONENT LINKLIB # Windows .lib
    PUBLIC_HEADER DESTINATION "include/${PYRAMIDTIFFDATA}" COMPONENT HEADERS
)

set(PYRAMIDTIFFDATA_INSTALL_COMPONENTS
    PLUGINS
    LINKLIB
    HEADERS
)

foreach(component IN LISTS PYRAMIDTIFFDATA_INSTALL_COMPONENTS)
    add_custom_command(TARGET ${PYRAMIDTIFFDATA} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
        --install ${CMAKE_CURRENT_BINARY_DIR}
        --config $<CONFIGURATION>
        --component ${component}
        --prefix ${ManiVault_INSTALL_DIR}/$<CONFIGURATION>
    )
endforeach()

# -----------------------------------------------------------------------------
# Miscellaneous
# -----------------------------------------------------------------------------
# Automatically set the debug environment (command + working directory) for MSVC
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set_property(TARGET ${PYRAMIDTIFFDATA} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<IF:$<CONFIG:DEBUG>,${ManiVault_INSTALL_DIR}/Debug,$<IF:$<CONFIG:RELWITHDEBINFO>,${ManiVault_INSTALL_DIR}/RelWithDebInfo,${ManiVault_INSTALL_DIR}/Release>>)
    set_property(TARGET ${PYRAMIDTIFFDATA} PROPERTY VS_DEBUGGER_COMMAND $<IF:$<CONFIG:DEBUG>,"${ManiVault_INSTALL_DIR}/Debug/ManiVault Studio.exe",$<IF:$<CONFIG:RELWITHDEBINFO>,"${ManiVault_INSTALL_DIR}/RelWithDebInfo/ManiVault Studio.exe","${ManiVault_INSTALL_DIR}/Release/ManiVault Studio.exe">>)
endif()
