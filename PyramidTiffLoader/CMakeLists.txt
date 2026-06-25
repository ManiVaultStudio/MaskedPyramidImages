cmake_minimum_required(VERSION 3.22)

# -----------------------------------------------------------------------------
# PyramidTiffLoader Plugin
# -----------------------------------------------------------------------------
set(PYRAMIDTIFFLOADER "PyramidTiffLoader")
PROJECT(${PYRAMIDTIFFLOADER} 
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

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# Source files
# -----------------------------------------------------------------------------
set(SOURCES
    src/PyramidTiffLoader.h
    src/PyramidTiffLoader.cpp
)

set(AUX
    PluginInfo.json
)

source_group(Plugin FILES ${SOURCES})
source_group(Aux FILES ${AUX})

# -----------------------------------------------------------------------------
# CMake Target
# -----------------------------------------------------------------------------
add_library(${PYRAMIDTIFFLOADER} SHARED ${SOURCES} ${AUX})

# -----------------------------------------------------------------------------
# Target include directories
# -----------------------------------------------------------------------------
target_include_directories(${PYRAMIDTIFFLOADER} 
    PRIVATE 
        "${ManiVault_INCLUDE_DIR}"
        "${PyramidTiffData_INCLUDE_DIR}"
)

# -----------------------------------------------------------------------------
# Target library linking
# -----------------------------------------------------------------------------
target_link_libraries(${PYRAMIDTIFFLOADER}
    PRIVATE
        Qt6::Widgets
        Qt6::WebEngineWidgets
        ManiVault::Core
        ManiVault::PyramidTiffData
        fmt::fmt
)

if(MV_MIPYR_USE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(${PYRAMIDTIFFLOADER} PRIVATE OpenMP::OpenMP_CXX)
endif()

# -----------------------------------------------------------------------------
# Target properties
# -----------------------------------------------------------------------------
target_compile_features(${PYRAMIDTIFFLOADER} PRIVATE cxx_std_20)

set_target_properties(${PYRAMIDTIFFLOADER} PROPERTIES 
    AUTOMOC ON
    UNITY_BUILD ${MV_UNITY_BUILD}
    FOLDER "MaskedImagePyramidPlugins"
)

# Instruction sets
mv_check_and_set_AVX(${PYRAMIDTIFFLOADER} ${MV_MIPYR_USE_AVX})

# File name suffix
mv_handle_plugin_config(${PYRAMIDTIFFLOADER} OFF)

# -----------------------------------------------------------------------------
# Target installation
# -----------------------------------------------------------------------------
install(TARGETS ${PYRAMIDTIFFLOADER}
    RUNTIME DESTINATION Plugins COMPONENT PLUGINS # Windows .dll
    LIBRARY DESTINATION Plugins COMPONENT PLUGINS # Linux/Mac .so
)

add_custom_command(TARGET ${PYRAMIDTIFFLOADER} POST_BUILD
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
    set_property(TARGET ${PYRAMIDTIFFLOADER} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<IF:$<CONFIG:DEBUG>,${ManiVault_INSTALL_DIR}/Debug,$<IF:$<CONFIG:RELWITHDEBINFO>,${ManiVault_INSTALL_DIR}/RelWithDebInfo,${ManiVault_INSTALL_DIR}/Release>>)
    set_property(TARGET ${PYRAMIDTIFFLOADER} PROPERTY VS_DEBUGGER_COMMAND $<IF:$<CONFIG:DEBUG>,"${ManiVault_INSTALL_DIR}/Debug/ManiVault Studio.exe",$<IF:$<CONFIG:RELWITHDEBINFO>,"${ManiVault_INSTALL_DIR}/RelWithDebInfo/ManiVault Studio.exe","${ManiVault_INSTALL_DIR}/Release/ManiVault Studio.exe">>)
endif()
