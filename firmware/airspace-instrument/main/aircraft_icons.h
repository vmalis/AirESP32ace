// AirESP32ace
// Created by VM, August 2026

#pragma once

#include "lvgl.h"
#include "data.h"

const lv_image_dsc_t *airspace_aircraft_map_icon_for_aircraft(const app_aircraft_t *aircraft);
const lv_image_dsc_t *airspace_aircraft_detail_icon_for_aircraft(const app_aircraft_t *aircraft);
const char *airspace_aircraft_icon_label_for_aircraft(const app_aircraft_t *aircraft);
bool airspace_aircraft_likely_commercial(const app_aircraft_t *aircraft);
const char *airspace_aircraft_flight_label_for_aircraft(const app_aircraft_t *aircraft);
