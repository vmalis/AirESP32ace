// AirESP32ace
// Created by VM, August 2026

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Room for atomically replacing tracker/config/launch state after assets. */
#define APP_STORAGE_STATE_RESERVE (128U * 1024U)

bool app_storage_is_retired_radar_map(const char *name);
bool app_storage_can_replace(uint64_t free_bytes, uint64_t replaced_bytes,
                             uint64_t incoming_bytes);

typedef size_t (*app_storage_sector_reader)(void *ctx, size_t offset, uint8_t *out, size_t len);
/* For a private, unmounted logical volume only. Any incomplete or failed read
 * invalidates the proof; a blank boot sector is not a blank volume. */
bool app_storage_is_erased(size_t volume_size, uint8_t *scratch, size_t sector_size,
                           app_storage_sector_reader read_sector, void *ctx);
