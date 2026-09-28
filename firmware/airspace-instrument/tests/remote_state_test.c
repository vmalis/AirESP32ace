// AirESP32ace
// Created by VM, August 2026

#include "remote.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Deliberately use the former copy-based merge as an independent oracle in
 * this host-only test. Production must not allocate these large snapshots. */
static app_remote_status_t expected_merge(const app_remote_status_t *old,
                                          const app_remote_status_t *incoming)
{
    app_remote_status_t expected = *incoming;
    if (incoming->tracked_flight_slot != old->tracked_flight_slot) {
        expected.route_map_valid_mask = old->route_map_valid_mask;
        expected.route_flag_valid_mask = old->route_flag_valid_mask;
        expected.map_valid = old->map_valid;
        expected.map_bytes = old->map_bytes;
        expected.map_revision = old->map_revision;
        expected.route_flag_revision = old->route_flag_revision;
        /* The in-place version also retains alignment padding in this
         * device-owned range; padding is not a field-value difference. */
        const size_t padding_start = offsetof(app_remote_status_t, map_valid) + sizeof(old->map_valid);
        const size_t padding_end = offsetof(app_remote_status_t, map_bytes);
        memcpy((uint8_t *)&expected + padding_start, (const uint8_t *)old + padding_start,
               padding_end - padding_start);
    }
    const size_t starts[] = {
        offsetof(app_remote_status_t, route_progress),
        offsetof(app_remote_status_t, tracked_route_geometry_version),
        offsetof(app_remote_status_t, tracked_flight_origin_terminal),
    };
    const size_t ends[] = {
        offsetof(app_remote_status_t, map_valid),
        offsetof(app_remote_status_t, launch_valid),
        sizeof(expected),
    };
    for (unsigned i = 0; i < 3; i++)
        memcpy((uint8_t *)&expected + starts[i], (const uint8_t *)old + starts[i], ends[i] - starts[i]);
    if (expected.revision <= old->revision) expected.revision = old->revision + 1;
    return expected;
}

int main(void)
{
    /* ESP-IDF's USB TX ring accepts each write atomically. Large JSON replies
     * must be split below its capacity, including the observed 1939-byte
     * launch snapshot and the maximum configuration response. */
    assert(app_remote_usb_tx_chunk(0) == 0);
    const size_t response_sizes[] = {1, 255, 256, 257, 512, 1939, 24 * 1024};
    for (size_t i = 0; i < sizeof(response_sizes) / sizeof(response_sizes[0]); i++) {
        size_t sent = 0;
        while (sent < response_sizes[i]) {
            const size_t chunk = app_remote_usb_tx_chunk(response_sizes[i] - sent);
            assert(chunk > 0 && chunk < APP_REMOTE_USB_TX_BUFFER_BYTES);
            assert(chunk <= response_sizes[i] - sent);
            sent += chunk;
        }
        assert(sent == response_sizes[i]);
    }
    for (unsigned stale = 0; stale < 2; stale++) {
        for (unsigned newer = 0; newer < 2; newer++) {
            app_remote_status_t current, incoming = {0};
            memset(&current, 1, sizeof(current));
            current.style = APP_REMOTE_STYLE_CARTOGRAPHIC_GLASS;
            current.tracked_flight_slot = 2;
            current.revision = 100;
            current.route_progress = .62f;
            incoming.style = APP_REMOTE_STYLE_WEATHER;
            incoming.tracked_flight_slot = stale ? 1 : 2;
            incoming.revision = newer ? 110 : 50;
            incoming.map_revision = 10;
            incoming.route_map_valid_mask = 3;
            incoming.route_flag_valid_mask = 3;
            incoming.map_valid = true;
            incoming.map_bytes = APP_REMOTE_MAP_BYTES;
            incoming.launch_valid = true;
            strcpy(incoming.launch_mission, "New launch data");
            app_remote_status_t expected = expected_merge(&current, &incoming);
            app_remote_merge_status(&current, &incoming, true, false);
            for (size_t byte = 0; byte < sizeof(current); byte++) {
                if (((uint8_t *)&current)[byte] != ((uint8_t *)&expected)[byte])
                    fprintf(stderr, "stale=%u newer=%u offset=%zu actual=%u expected=%u\n",
                            stale, newer, byte, ((uint8_t *)&current)[byte], ((uint8_t *)&expected)[byte]);
            }
            assert(memcmp(&current, &expected, sizeof(current)) == 0);
            app_remote_merge_status(&current, &incoming, false, false);
            assert(memcmp(&current, &incoming, sizeof(current)) == 0);
            app_remote_merge_status(&current, &current, true, false);
            assert(memcmp(&current, &incoming, sizeof(current)) == 0);
        }
    }
    for (unsigned tracker = 0; tracker < 2; tracker++) {
        app_remote_status_t current = {0}, incoming = {0};
        current.launch_valid = true;
        current.launch_option_mask = 4;
        current.launch_options[2].net_unix = 1790599739;
        strcpy(current.launch_mission, "FLIGHT 14");
        incoming.style = APP_REMOTE_STYLE_WEATHER;
        incoming.route_progress = .8f;
        strcpy(incoming.launch_mission, "OBSOLETE COMPANION SNAPSHOT");
        app_remote_merge_status(&current, &incoming, tracker, true);
        assert(!strcmp(current.launch_mission, "FLIGHT 14"));
        assert(current.launch_valid && current.launch_option_mask == 4);
        assert(current.launch_options[2].net_unix == 1790599739);
        assert(current.style == APP_REMOTE_STYLE_WEATHER);
        assert(current.route_progress == (tracker ? 0 : .8f));
    }
    puts("Remote state tests passed (USB reply chunks, Tracker/launch ownership, stale slot, metadata, revisions).");
}
