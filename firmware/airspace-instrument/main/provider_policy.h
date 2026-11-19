// AirESP32ace
// Created by VM, November 2026
#pragma once
#include "data.h"

/* One leaky bucket per identity type. Standard: 1920/day plus an 8-credit
 * burst per board; anonymous: <185/day plus 8. Counts attempted requests,
 * including date-line splits. Resetting hardware resets the RAM bucket. */
typedef struct { int64_t updated; int balance; bool initialized; } app_sky_budget_t;
static inline bool app_sky_reserve(app_sky_budget_t *b, int64_t now, int cost,
                                   bool auth, bool reserve_for_tracker)
{
    int period = auth ? 45 : 470;
    int cap = 8 * period;
    if (!b->initialized) {
        b->updated = now;
        b->balance = cap;
        b->initialized = true;
    }
    int64_t elapsed = now > b->updated ? now - b->updated : 0;
    b->updated = now;
    b->balance = elapsed >= cap - b->balance ? cap : b->balance + (int)elapsed;
    if (cost < 1 || cost > 4 || b->balance < (cost + (reserve_for_tracker ? 4 : 0)) * period)
        return false;
    b->balance -= cost * period;
    return true;
}

static inline app_api_health_state_t app_provider_health(int code, bool valid)
{
    if (valid && code >= 200 && code < 300) return APP_API_HEALTH_VALID;
    if (code == -1 || code == 401 || code == 403) return APP_API_HEALTH_INVALID;
    if (code == 429) return APP_API_HEALTH_LIMITED;
    return APP_API_HEALTH_UNAVAILABLE;
}

/* Filter on read, not just after another network response: outage and sleep
 * must not leave the final targets/orbit looking live indefinitely. */
static inline void app_expire_observations(app_data_status_t *s, int64_t now)
{
    unsigned delta = 0;
    if (now < 1700000000) return;
    if (s->aircraft_valid) {
        int kept = 0;
        for (int i = 0; i < s->aircraft_count && i < APP_MAX_AIRCRAFT; i++) {
            int observed = s->aircraft[i].position_unix;
            if (observed <= 0 || now - observed > 180 || observed > now + 60) {
                delta++;
                continue;
            }
            if (i != kept) s->aircraft[kept] = s->aircraft[i];
            kept++;
        }
        s->aircraft_count = kept;
        if (!kept && now - s->aircraft_fetched_unix > 180) {
            s->aircraft_valid = false;
            delta += 17;
        }
    }
    if (s->iss_valid && (s->iss_track_count < 1 || s->iss_track_count > APP_ISS_TRACK_POINTS ||
        now > s->iss_track_timestamp[s->iss_track_count-1] + (int64_t)60)) {
        s->iss_valid = false;
        delta += 34;
    }
    if (now - s->weather_fetched_unix > 1800 && s->weather_valid) {
        s->weather_valid = false;
        s->weather_air_quality_valid = false;
        delta += 68;
    }
    if (s->metar_valid && now - s->metar_observed_unix > 7200) {
        s->metar_valid = false;
        delta += 136;
    }
    if ((s->radar_valid || s->weather_radar_overlay_valid) && now - s->radar_frame_unix > 3600) {
        s->radar_valid = s->weather_radar_overlay_valid = false;
        s->weather_radar_frame_count = 0;
        delta += 272;
    }
    if (s->state == APP_DATA_READY && !s->aircraft_valid && !s->weather_valid &&
        !s->metar_valid && !s->iss_valid && !s->radar_valid) s->state = APP_DATA_ERROR;
    /* A generation has room for all of its independently expiring fields. */
    s->revision = (s->revision << 9) | delta;
}
