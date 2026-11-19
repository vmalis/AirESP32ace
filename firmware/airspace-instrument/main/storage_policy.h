// AirESP32ace
// Created by VM, August 2026

#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Room for atomically replacing tracker/config/launch state after assets. */
#define APP_STORAGE_STATE_RESERVE (128U * 1024U)

bool app_storage_is_retired_radar_map(const char *name);
bool app_storage_can_replace(uint64_t free_bytes, uint64_t replaced_bytes,
                             uint64_t incoming_bytes);
