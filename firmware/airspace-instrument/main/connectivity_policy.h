// AirESP32ace
// Created by VM, November 2026
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Monotonic milliseconds; no dependency on SNTP and no retry ceiling.
 * Access is serialized by the network status mutex. */
typedef struct {
    bool pending;
    uint8_t failures;
    int64_t due_ms;
} app_wifi_retry_t;

static inline void app_wifi_retry_reset(app_wifi_retry_t *r, bool start, int64_t now)
{
    *r = (app_wifi_retry_t){.pending = start, .due_ms = now};
}

static inline void app_wifi_retry_failed(app_wifi_retry_t *r, int64_t now)
{
    const unsigned seconds[] = {1, 2, 4, 8, 16, 32, 60};
    unsigned i = r->failures;
    if (i < 6) r->failures++;
    r->pending = true;
    r->due_ms = now + 1000 * seconds[i < 6 ? i : 6];
}

static inline bool app_wifi_retry_begin(app_wifi_retry_t *r, int64_t now)
{
    if (!r->pending || now < r->due_ms) return false;
    /* If DHCP/driver never signals either success or disconnection, the
     * connection must still be retried. */
    r->due_ms = now + 60000;
    return true;
}
