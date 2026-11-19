// AirESP32ace
// Created by VM, August 2026

#include "storage_policy.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>

bool app_storage_is_retired_radar_map(const char *name)
{
    if (name == NULL) return false;
    const size_t length = strlen(name);
    /* Only the old FAT 8.3 radar cache format: r<city>005.rgb or 100.rgb.
     * Never match route maps, arbitrary files, subdirectories or live ranges. */
    if (length < 9 || length > 12 || tolower((unsigned char)name[0]) != 'r' ||
        strcasecmp(name + length - 4, ".rgb") != 0) return false;
    const size_t range = length - 7;
    for (size_t i = 1; i < range; i++) {
        if (!isalnum((unsigned char)name[i])) return false;
    }
    return strncmp(name + range, "005", 3) == 0 ||
           strncmp(name + range, "100", 3) == 0;
}

bool app_storage_can_replace(uint64_t free_bytes, uint64_t replaced_bytes,
                             uint64_t incoming_bytes)
{
    if (incoming_bytes > UINT64_MAX - APP_STORAGE_STATE_RESERVE ||
        free_bytes > UINT64_MAX - replaced_bytes) return false;
    return free_bytes + replaced_bytes >= incoming_bytes + APP_STORAGE_STATE_RESERVE;
}
