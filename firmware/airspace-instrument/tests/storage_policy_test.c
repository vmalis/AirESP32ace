// AirESP32ace
// Created by VM, August 2026

#include "storage_policy.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    const char *retired[] = {"RSJC005.RGB", "rsfo100.rgb", "rA005.rgb", "RABCD100.RGB"};
    const char *preserved[] = {"RSJC010.RGB", "RSJC020.RGB", "RSJC050.RGB",
        "rg.rgb", "rf3.rgb", "fo.rgb", "tracker.bin", "config.jsn", "launches.bin",
        "r005.rgb", "RSJC105.RGB", "RSJC005.BAK", "rABCDE005.rgb", "r../005.rgb",
        "r_A005.rgb", "RSJC005.RGB.usb", "", NULL};
    for (size_t i = 0; i < sizeof(retired) / sizeof(*retired); i++)
        assert(app_storage_is_retired_radar_map(retired[i]));
    for (size_t i = 0; i < sizeof(preserved) / sizeof(*preserved); i++)
        assert(!app_storage_is_retired_radar_map(preserved[i]));
    const uint64_t map = 466U * 466U * 2U;
    assert(!app_storage_can_replace(188416, 0, map)); // Observed fourth-flight failure.
    assert(app_storage_can_replace(3U * 1024U * 1024U, 0, map));
    assert(app_storage_can_replace(APP_STORAGE_STATE_RESERVE, map, map));
    assert(!app_storage_can_replace(APP_STORAGE_STATE_RESERVE - 1, map, map));
    assert(!app_storage_can_replace(1, 0, UINT64_MAX));
    assert(!app_storage_can_replace(UINT64_MAX, 1, map));
    puts("Storage policy tests passed (retired-only migration, live assets preserved, state reserve).");
}
