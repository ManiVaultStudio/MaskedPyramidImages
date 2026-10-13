cmake_minimum_required(VERSION 3.22)

# -----------------------------------------------------------------------------
# ClusterExporter Plugin
# -----------------------------------------------------------------------------
set(PYRAMIDCLUSTEREXPORTER "PyramidClusterExporter")
PROJECT(${PYRAMIDCLUSTEREXPORTER} 
        LANGUAGES CXX)

# -----------------------------------------------------------------------------
# CMake Options
# -----------------------------------------------------------------------------
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# Source files
# -----------------------------------------------------------------------------
set(PYRAMID_CLUSTER_EXPORTER_HEADERS
    src/PyramidClusterExporter.h
)

set(PYRAMID_CLUSTER_EXPORTER_SOURCES
    src/PyramidClusterExporter.cpp
)

set(AUX
    PluginInfo.json
)

source_group(Plugin FILES ${PYRAMID_CLUSTER_EXPORTER_HEADERS} ${PYRAMID_CLUSTER_EXPORTER_SOURCES})
source_group(Aux FILES ${AUX})

# -----------------------------------------------------------------------------
# CMake Target
# -----------------------------------------------------------------------------
add_library(${PYRAMIDCLUSTEREXPORTER} SHARED ${PYRAMID_CLUSTER_EXPORTER_HEADERS} ${PYRAMID_CLUSTER_EXPORTER_SOURCES} ${AUX})

# -----------------------------------------------------------------------------
# Target include directories
# -----------------------------------------------------------------------------
target_include_directories(${PYRAMIDCLUSTEREXPORTER}
    PRIVATE
        "${ManiVault_INCLUDE_DIR}"
)

# -----------------------------------------------------------------------------
# Target library linking
# -----------------------------------------------------------------------------
target_link_libraries(${PYRAMIDCLUSTEREXPORTER}
    PRIVATE
        Qt6::Widgets
        Qt6::WebEngineWidgets
        ManiVault::Core
        ManiVault::PointData
        ManiVault::ClusterData
        rapidcsv::rapidcsv
        fmt::fmt
)

if(MV_MIPYR_USE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(${PYRAMIDCLUSTEREXPORTER} PRIVATE OpenMP::OpenMP_CXX)
endif()

if(UNIX AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(${PYRAMIDCLUSTEREXPORTER} PRIVATE TBB::tbb)
endif()

# -----------------------------------------------------------------------------
# Target properties
# -----------------------------------------------------------------------------
target_compile_features(${PYRAMIDCLUSTEREXPORTER} PRIVATE cxx_std_20)

set_target_properties(${PYRAMIDCLUSTEREXPORTER} PROPERTIES 
    AUTOMOC ON
    UNITY_BUILD ${MV_UNITY_BUILD}
    FOLDER "MaskedImagePyramidPlugins"
)

# Instruction sets
mv_check_and_set_AVX(${PYRAMIDCLUSTEREXPORTER} ${MV_MIPYR_USE_AVX})

# File name suffix
mv_handle_plugin_config(${PYRAMIDCLUSTEREXPORTER} OFF)

# -----------------------------------------------------------------------------
# Target installation
# -----------------------------------------------------------------------------
install(TARGETS ${PYRAMIDCLUSTEREXPORTER}
    RUNTIME DESTINATION Plugins COMPONENT PLUGINS # Windows .dll
    LIBRARY DESTINATION Plugins COMPONENT PLUGINS # Linux/Mac .so
)

add_custom_command(TARGET ${PYRAMIDCLUSTEREXPORTER} POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
    --install ${CMAKE_CURRENT_BINARY_DIR}
    --config $<CONFIGURATION>
    --component PLUGINS
    --prefix ${ManiVault_INSTALL_DIR}/$<CONFIGURATION>
)

# -----------------------------------------------------------------------------
# Miscellaneous
# -----------------------------------------------------------------------------
# Automatically set the debug environment (command + working directory) for MSVC
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set_property(TARGET ${PYRAMIDCLUSTEREXPORTER} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<IF:$<CONFIG:DEBUG>,${ManiVault_INSTALL_DIR}/Debug,$<IF:$<CONFIG:RELWITHDEBINFO>,${ManiVault_INSTALL_DIR}/RelWithDebInfo,${ManiVault_INSTALL_DIR}/Release>>)
    set_property(TARGET ${PYRAMIDCLUSTEREXPORTER} PROPERTY VS_DEBUGGER_COMMAND $<IF:$<CONFIG:DEBUG>,"${ManiVault_INSTALL_DIR}/Debug/ManiVault Studio.exe",$<IF:$<CONFIG:RELWITHDEBINFO>,"${ManiVault_INSTALL_DIR}/RelWithDebInfo/ManiVault Studio.exe","${ManiVault_INSTALL_DIR}/Release/ManiVault Studio.exe">>)
endif()
