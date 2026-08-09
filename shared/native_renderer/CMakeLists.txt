# AirESP32ace
# Created by VM, August 2026

cmake_minimum_required(VERSION 3.24)

project(AirspaceNativeRenderer LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(AIRSPACE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(AIRSPACE_FIRMWARE_MAIN
    "${AIRSPACE_ROOT}/firmware/airspace-instrument/main")
set(AIRSPACE_LVGL
    "${AIRSPACE_ROOT}/firmware/airspace-instrument/managed_components/lvgl__lvgl")

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h"
    CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory("${AIRSPACE_LVGL}" "${CMAKE_BINARY_DIR}/lvgl")

add_library(AirspaceRenderer SHARED
    "${AIRSPACE_FIRMWARE_MAIN}/main.c"
    "${AIRSPACE_FIRMWARE_MAIN}/aircraft_icons.c"
    "${AIRSPACE_FIRMWARE_MAIN}/aircraft_type_registry.c"
    "${AIRSPACE_FIRMWARE_MAIN}/api_logos.c"
    "${AIRSPACE_FIRMWARE_MAIN}/cargo_logos.c"
    "${AIRSPACE_FIRMWARE_MAIN}/clock_hand_assets.c"
    "${AIRSPACE_FIRMWARE_MAIN}/nasalization_countdown_50.c"
    "${AIRSPACE_FIRMWARE_MAIN}/nasalization_countdown_68.c"
    "${AIRSPACE_FIRMWARE_MAIN}/nasalization_prefix_34.c"
    "${AIRSPACE_FIRMWARE_MAIN}/nasalization_expected_22.c"
    "${AIRSPACE_FIRMWARE_MAIN}/detail_metric_icons.c"
    "${AIRSPACE_FIRMWARE_MAIN}/gate_arrow_assets.c"
    "${AIRSPACE_FIRMWARE_MAIN}/locations.c"
    "${AIRSPACE_FIRMWARE_MAIN}/menu_icons.c"
    "${AIRSPACE_FIRMWARE_MAIN}/muller_clock_22.c"
    "${AIRSPACE_FIRMWARE_MAIN}/muller_clock_34.c"
    desktop_stubs.c
)

target_compile_definitions(AirspaceRenderer PRIVATE
    AIRSPACE_DESKTOP_RENDERER=1
)

target_include_directories(AirspaceRenderer PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${AIRSPACE_FIRMWARE_MAIN}"
)

target_link_libraries(AirspaceRenderer PRIVATE lvgl m)

set_target_properties(AirspaceRenderer PROPERTIES
    OUTPUT_NAME "AirspaceRenderer"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@loader_path"
    MACOSX_RPATH TRUE
)
