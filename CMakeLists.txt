cmake_minimum_required(VERSION 3.22)
project(source-explorer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SE_BUILD_PLUGIN "Build the OBS plugin (needs libobs, obs-frontend-api, Qt6)" ON)
option(SE_BUILD_TESTS "Build the unit tests" ON)
set(SE_OBS_SDK_DIR "" CACHE PATH "Local OBS SDK folder (see cmake/ObsSdk.cmake); empty = find_package")

# ---------------------------------------------------------------------------
# core-payload: pure C++20, no OBS / Qt dependency. Testable anywhere.
# ---------------------------------------------------------------------------
add_library(se-payload STATIC
    src/core/json-value.cpp
    src/core/payload-generator.cpp
    src/core/transform-math.cpp
    src/core/tree-model.cpp
    src/core/quick-access.cpp)
target_include_directories(se-payload PUBLIC src)
if(MSVC)
    target_compile_options(se-payload PRIVATE /W4 /utf-8)
else()
    target_compile_options(se-payload PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SE_BUILD_TESTS)
    enable_testing()
    foreach(t payload_generator transform_math filter_requests properties_support source_tree frame_fit quick_access)
        add_executable(test_${t} tests/test_${t}.cpp)
        target_link_libraries(test_${t} PRIVATE se-payload)
        add_test(NAME ${t} COMMAND test_${t})
    endforeach()
endif()

# ---------------------------------------------------------------------------
# OBS plugin
# ---------------------------------------------------------------------------
if(SE_BUILD_PLUGIN)
    if(SE_OBS_SDK_DIR)
        include(cmake/ObsSdk.cmake)
    else()
        find_package(libobs REQUIRED)
        find_package(obs-frontend-api REQUIRED)
    endif()
    find_package(Qt6 REQUIRED COMPONENTS Widgets)

    set(CMAKE_AUTOMOC ON)

    # All UI code lives in one static library, shared by the plugin and the smoke test.
    add_library(se-ui STATIC
        src/ui/context-bar.cpp
        src/ui/context-bar.hpp
        src/ui/quick-access-bar.cpp
        src/ui/quick-access-bar.hpp
        src/ui/dock-placement.cpp
        src/ui/dock-placement.hpp
        src/ui/dock-style.hpp
        src/ui/explorer-dock.cpp
        src/ui/explorer-dock.hpp
        src/ui/scene-switch.cpp
        src/ui/scene-switch.hpp
        src/ui/source-tree-drawer.cpp
        src/ui/source-tree-drawer.hpp
        src/ui/panels/audio-panel.cpp
        src/ui/panels/audio-panel.hpp
        src/ui/panels/filters-panel.cpp
        src/ui/panels/filters-panel.hpp
        src/ui/panels/overview-panel.cpp
        src/ui/panels/overview-panel.hpp
        src/ui/panels/misc-panel.cpp
        src/ui/panels/misc-panel.hpp
        src/ui/panels/source-panel.cpp
        src/ui/panels/source-panel.hpp
        src/ui/panels/properties-panel.cpp
        src/ui/panels/properties-panel.hpp
        src/ui/panels/transform-panel.cpp
        src/ui/panels/transform-panel.hpp
        src/ui/widgets/alignment-grid.cpp
        src/ui/widgets/alignment-grid.hpp
        src/ui/widgets/flow-layout.cpp
        src/ui/widgets/flow-layout.hpp
        src/ui/widgets/command-menu.cpp
        src/ui/widgets/command-menu.hpp
        src/ui/widgets/reorder-list.cpp
        src/ui/widgets/reorder-list.hpp
        src/core/obs-raii.hpp
        src/core/scene-item-ops.cpp
        src/core/scene-item-ops.hpp
        src/core/obs-event-bridge.cpp
        src/core/obs-event-bridge.hpp
        src/util/clipboard.cpp
        src/util/clipboard.hpp)
    target_link_libraries(se-ui PUBLIC
        se-payload
        OBS::libobs
        OBS::obs-frontend-api
        Qt6::Widgets)
    set_target_properties(se-ui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(se-ui PRIVATE /W4 /utf-8)
    endif()

    add_library(source-explorer MODULE src/plugin-main.cpp)
    target_link_libraries(source-explorer PRIVATE se-ui)
    set_target_properties(source-explorer PROPERTIES
        PREFIX ""
        POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(source-explorer PRIVATE /W4 /utf-8)
    endif()

    # Start-up smoke test: builds the real dock and every panel against the real libobs (from an
    # installed OBS) on Qt's headless "minimal" platform, drives them, and fails on any crash.
    # Catches constructor-order bugs that unit tests of the pure code cannot see.
    set(SE_OBS_BIN_DIR "C:/Program Files/obs-studio/bin/64bit" CACHE PATH
        "Installed OBS bin dir used to run the smoke test (obs.dll, Qt DLLs)")
    if(SE_BUILD_TESTS AND WIN32 AND SE_OBS_SDK_DIR AND EXISTS "${SE_OBS_BIN_DIR}/obs.dll"
       AND EXISTS "${SE_OBS_SDK_DIR}/qt6/plugins/platforms/qminimal.dll")
        add_executable(smoke_dock tests/smoke_dock.cpp)
        target_link_libraries(smoke_dock PRIVATE se-ui)
        add_test(NAME smoke_dock COMMAND smoke_dock)
        set_tests_properties(smoke_dock PROPERTIES
            TIMEOUT 120
            ENVIRONMENT "QT_QPA_PLATFORM=minimal;QT_PLUGIN_PATH=${SE_OBS_SDK_DIR}/qt6/plugins"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${SE_OBS_BIN_DIR}")
    endif()
endif()
