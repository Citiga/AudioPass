cmake_minimum_required(VERSION 3.25)

project(AudioPass
    VERSION 0.90.0
    DESCRIPTION "Microphone through VST2 and VST3 into a virtual cable"
    LANGUAGES CXX
)

if(PROJECT_VERSION_PATCH EQUAL 0)
    set(AUDIOPASS_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
else()
    set(AUDIOPASS_VERSION "${PROJECT_VERSION}")
endif()
set(AUDIOPASS_VERSION_QUAD "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.0")

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "AudioPass builds on Windows with MSVC.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BV_WX "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wxWidgets")
set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst3sdk")

include(FetchContent)
if(POLICY CMP0168)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0168 NEW)
endif()
set(FETCHCONTENT_QUIET OFF)

if(NOT EXISTS "${BV_WX}/CMakeLists.txt")
    find_package(Git REQUIRED)
    message(STATUS "Fetching wxWidgets 3.2.11 into third_party/wxWidgets")
    FetchContent_Populate(wxwidgets
        GIT_REPOSITORY https://github.com/wxWidgets/wxWidgets.git
        GIT_TAG v3.2.11
        GIT_SHALLOW TRUE
        GIT_SUBMODULES_RECURSE TRUE
        SOURCE_DIR "${BV_WX}"
    )
endif()
if(NOT EXISTS "${SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp")
    find_package(Git REQUIRED)
    message(STATUS "Fetching the VST3 SDK 3.8.1 into third_party/vst3sdk")
    FetchContent_Populate(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.8.1_build_84
        GIT_SHALLOW TRUE
        GIT_SUBMODULES base cmake pluginterfaces public.sdk
        GIT_SUBMODULES_RECURSE TRUE
        SOURCE_DIR "${SDK_ROOT}"
    )
endif()

if(POLICY CMP0168)
    cmake_policy(POP)
endif()

if(NOT EXISTS "${BV_WX}/CMakeLists.txt")
    message(FATAL_ERROR "wxWidgets 3.2.11 could not be fetched into third_party/wxWidgets.")
endif()
if(NOT EXISTS "${SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp")
    message(FATAL_ERROR "The VST3 SDK 3.8.1 could not be fetched into third_party/vst3sdk.")
endif()

set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
set(wxBUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(wxBUILD_TESTS OFF CACHE BOOL "" FORCE)
set(wxBUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(wxBUILD_PRECOMP ON CACHE BOOL "" FORCE)
set(wxBUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(wxUSE_DPI_AWARE_MANIFEST "none" CACHE STRING "" FORCE)

add_subdirectory("${BV_WX}" "${CMAKE_BINARY_DIR}/_wx" EXCLUDE_FROM_ALL)

list(APPEND CMAKE_MODULE_PATH "${SDK_ROOT}/cmake/modules")
include(SMTG_VST3_SDK)
smtg_setup_platform_toolset()
smtg_setup_symbol_visibility()
set(public_sdk_SOURCE_DIR "${SDK_ROOT}/public.sdk")
set(pluginterfaces_SOURCE_DIR "${SDK_ROOT}/pluginterfaces")
smtg_create_lib_base_target()
smtg_create_pluginterfaces_target()
smtg_create_public_sdk_common_target()
smtg_create_public_sdk_hosting_target()

set(ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/AudioPass.ico")
string(REPLACE "\\" "/" ICON_PATH "${ICON_PATH}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest.in"
    "${CMAKE_CURRENT_BINARY_DIR}/AudioPass.manifest"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/AudioPass.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/AudioPass.rc"
    @ONLY
)

set(BV_SOURCES
    src/main.cpp
    src/Seh.cpp
    src/Session.cpp
    src/audio/Engine.cpp
    src/plugins/Vst2Plugin.cpp
    src/plugins/Vst3Plugin.cpp
    src/plugins/Scanner.cpp
    src/ui/Widgets.cpp
    src/ui/EditorFrame.cpp
    src/ui/PluginPicker.cpp
    src/ui/MainFrame.cpp
    src/ui/AboutDialog.cpp
    src/ui/SettingsDialog.cpp
    "${SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp"
    "${SDK_ROOT}/public.sdk/source/vst/hosting/plugprovider.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/AudioPass.manifest"
)

add_executable(AudioPass WIN32 ${BV_SOURCES})

target_sources(AudioPass PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/AudioPass.rc")
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/AudioPass.rc" PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/AudioPass.ico"
)

set_source_files_properties(
    "${SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp"
    PROPERTIES CXX_STANDARD 17
)

target_include_directories(AudioPass PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${SDK_ROOT}"
)

target_compile_definitions(AudioPass PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

target_compile_options(AudioPass PRIVATE /utf-8 /W3 /MP)

target_compile_features(AudioPass PRIVATE cxx_std_23)

target_link_libraries(AudioPass PRIVATE
    wx::core
    wx::base
    sdk_hosting
    ole32
    uuid
    avrt
    propsys
    dwmapi
    shell32
    advapi32
)

set_target_properties(AudioPass PROPERTIES
    OUTPUT_NAME "AudioPass"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/RelWithDebInfo"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Configure and build. The first configure fetches wxWidgets 3.2.11 and the
# VST3 SDK 3.8.1 into third_party when those directories are missing.
#   cmake -S . -B build -G "Visual Studio 18 2026" -A x64
#   cmake --build build --config Release
