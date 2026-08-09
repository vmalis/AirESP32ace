// AirESP32ace
// Created by VM, August 2026

#pragma once

#include <stdbool.h>

typedef struct {
    bool available;
    bool present;
    bool charging;
    int percent;
} app_power_battery_status_t;

void app_power_get_battery_status(app_power_battery_status_t *out);

