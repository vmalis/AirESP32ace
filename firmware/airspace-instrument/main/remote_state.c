// AirESP32ace
// Created by VM, August 2026

#include "remote.h"
#include <string.h>

void app_remote_merge_status(app_remote_status_t *current, const app_remote_status_t *incoming,
                             bool tracker_owns_updates, bool launches_own_updates)
{
    if (!current || !incoming || current == incoming) return;
    if (!tracker_owns_updates && !launches_own_updates) {
        memcpy(current, incoming, sizeof(*current));
        return;
    }

    const uint32_t previous_revision = current->revision;
    const uint32_t previous_map_revision = current->map_revision;
    const bool stale_slot = incoming->tracked_flight_slot != current->tracked_flight_slot;
    uint8_t *to = (uint8_t *)current;
    const uint8_t *from = (const uint8_t *)incoming;
    const size_t launch_start = offsetof(app_remote_status_t, launch_valid);
    const size_t launch_end = offsetof(app_remote_status_t, tracked_flight_origin_terminal);

    if (!tracker_owns_updates) {
        memcpy(to, from, launch_start);
        memcpy(to + launch_end, from + launch_end, sizeof(*current) - launch_end);
    } else {

    current->style = incoming->style;
    /* A late writer for the previous flight must not restore its map/flag
     * inventory over the newly active slot. All runtime flight fields stay
     * device-owned, regardless of whether the slot changed. */
    if (!stale_slot) {
        const size_t start = offsetof(app_remote_status_t, map_valid);
        const size_t end = offsetof(app_remote_status_t, radar_map_valid);
        memcpy(to + start, from + start, end - start);
    }
    const size_t metadata_start = offsetof(app_remote_status_t, radar_map_valid);
    const size_t metadata_end = offsetof(app_remote_status_t, tracked_route_geometry_version);
    memcpy(to + metadata_start, from + metadata_start, metadata_end - metadata_start);
    if (stale_slot) current->map_revision = previous_map_revision;

    }
    if (!launches_own_updates)
        memcpy(to + launch_start, from + launch_start, launch_end - launch_start);
    if (current->revision <= previous_revision) current->revision = previous_revision + 1;
}
