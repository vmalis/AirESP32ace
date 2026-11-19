// AirESP32ace
// Created by VM, August 2026
#pragma once

#include "remote.h"
#include "cJSON.h"

#define APP_LAUNCH_REQUESTS_PER_HOUR 12
typedef struct {
    app_remote_launch_t launch;
    int64_t checked, next_check, completed;
    int status_id, http_status;
    unsigned failures;
} app_launch_entry_t;

/* Pure parser/policy; shared by firmware and sanitizer-enabled host tests. */
int64_t app_launch_timestamp(const char *value);
bool app_launch_valid_id(const char *value);
int app_launch_utc_offset(const char *site, int64_t now);
bool app_launch_parse(const cJSON *json, unsigned agency, app_remote_launch_t *out, int *status_id);
bool app_launch_accept(app_launch_entry_t *entry, const cJSON *json, unsigned agency, int64_t now,
                       bool exact_id);
bool app_launch_terminal(int status_id);
bool app_launch_expired(const app_launch_entry_t *entry, int64_t now);
bool app_launch_stale(const app_launch_entry_t *entry, int64_t now);
bool app_launch_stale_for_mask(const app_launch_entry_t *entry, int64_t now, uint8_t enabled);
int64_t app_launch_next_check(const app_launch_entry_t *entry, int64_t now);
int64_t app_launch_interval(const app_launch_entry_t *entry, int64_t now);
int64_t app_launch_failure_interval(unsigned failures);
int64_t app_launch_budget_due(const int64_t attempts[APP_LAUNCH_REQUESTS_PER_HOUR], int64_t now);
bool app_launch_agencies(const cJSON *array, uint8_t *mask);

void app_launches_start(void);
void app_launches_load(void);
void app_launches_tick(bool online);
void app_launches_request_refresh(void);
cJSON *app_launches_json(void);
