// AirESP32ace
// Created by VM, August 2026

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_config.h"

typedef struct {
    const char *key;
    const char *label;
    const char *lat;
    const char *lon;
    const char *timezone;
    const char *metar;
    const char *radar_half_degrees;
} app_location_preset_t;

size_t app_location_preset_count(void);
const app_location_preset_t *app_location_preset_at(size_t index);
const app_location_preset_t *app_location_preset_find(const char *key);
bool app_location_preset_is_builtin(const char *key);
const app_location_preset_t *app_location_preset_for_config(const app_config_t *config);
bool app_location_apply_preset(app_config_t *config, const char *key);
void app_location_set_custom_data(const char *serialized);
