// AirESP32ace
// Created by VM, August 2026

#include "storage_policy.h"
#include "route_package_policy.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t data[64]; int calls, error_at, short_at; } flash_fixture;
static size_t read_fixture(void *ctx, size_t offset, uint8_t *out, size_t len)
{
    flash_fixture *f = ctx;
    assert(offset == (size_t)f->calls * len && offset <= sizeof(f->data) - len);
    ++f->calls;
    if ((int)offset == f->error_at) return 0;
    if ((int)offset == f->short_at) return len - 1;
    memcpy(out, &f->data[offset], len);
    return len;
}

static void test_format_gate(void)
{
    flash_fixture f = {.error_at = -1, .short_at = -1};
    uint8_t scratch[8];
    memset(f.data, 0xff, sizeof(f.data));
    assert(app_storage_is_erased(64, scratch, 8, read_fixture, &f) && f.calls == 8);
    /* A blank boot sector, perfectly clean intermediate sectors, or a
     * blank trailing sector cannot establish that the volume is disposable. */
    for (unsigned pos = 0; pos < sizeof(f.data); ++pos) {
        f.calls = 0;
        f.data[pos] = 0xfe;
        assert(!app_storage_is_erased(64, scratch, 8, read_fixture, &f));
        f.data[pos] = 0xff;
    }
    f.calls = 0; f.error_at = 56;
    assert(!app_storage_is_erased(64, scratch, 8, read_fixture, &f) && f.calls == 8);
    f.calls = 0; f.error_at = -1; f.short_at = 32;
    assert(!app_storage_is_erased(64, scratch, 8, read_fixture, &f) && f.calls == 5);
    f.calls = 0;
    assert(!app_storage_is_erased(0, scratch, 8, read_fixture, &f));
    assert(!app_storage_is_erased(63, scratch, 8, read_fixture, &f));
    assert(!app_storage_is_erased(64, scratch, 0, read_fixture, &f));
    assert(!app_storage_is_erased(64, scratch, 8, NULL, &f));
    assert(!app_storage_is_erased(64, NULL, 8, read_fixture, &f) && !f.calls);
}

int main(void)
{
    test_format_gate();
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
    uint32_t bank=0x1234;
    /* Simulate interruption after each of the first 15 assets arrived:
     * the durable mask must stay byte-for-byte unchanged on every failure. */
    for (unsigned n=0;n<16;n++) {
        unsigned partial=(1U<<n)-1;
        assert(!app_route_package_next(bank,0xffff,partial,partial,&bank));
        assert(bank==0x1234);
    }
    assert(app_route_package_next(bank,0xffff,0xffff,0x3012,&bank));
    assert(bank==(0x1234^0x3012));
    uint32_t committed=bank;
    assert(app_route_package_next(bank,0xffff,0xffff,0,&bank) && bank==committed);
    assert(!app_route_package_next(bank,0xf,0xf,0xff,&bank) && bank==committed);
    assert(app_route_package_next(bank,0,0,0,&bank) && bank==0);
    puts("Storage policy tests passed (fail-closed formatting, migration, reserve, atomic selection).");
}
