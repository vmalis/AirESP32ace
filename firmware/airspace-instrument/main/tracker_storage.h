// AirESP32ace
// Created by VM, August 2026
#pragma once

#include "tracker.h"
#include <stdio.h>

typedef struct {
    unsigned count;
    int active;
    uint32_t asset_generation;
    app_tracker_flight_t flights[APP_TRACKER_SLOTS];
} tracker_store_t;

/* ESP32 little-endian durable record format; never writes a runtime struct.
 * V1 (v0.1.4) input is decoded by its frozen, measured Xtensa byte offsets. */
bool app_tracker_store_write(FILE *file, const tracker_store_t *queue);
bool app_tracker_store_read(FILE *file, tracker_store_t *queue);
