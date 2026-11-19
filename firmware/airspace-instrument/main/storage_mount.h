// AirESP32ace
// Created by VM, August 2026

#pragma once
#include "wear_levelling.h"

/* Mount the named data/FAT storage partition for the app's lifetime.
 * Errors leave the logical volume unpublished, not reformatted. */
esp_err_t app_storage_mount(const char *base_path, wl_handle_t *mounted_handle);
