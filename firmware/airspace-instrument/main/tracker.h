// AirESP32ace
// Created by VM, August 2026

#pragma once
#include "cJSON.h"
#include "remote.h"

#define APP_TRACKER_SLOTS 4

typedef struct {
    app_remote_status_t display;
    char plan_id[40];
    char provider_id[128];
    char icao24[8];
    double origin_lat, origin_lon, destination_lat, destination_lon;
    double latitude, longitude;
    int64_t scheduled_departure;
    int64_t position_unix, contact_unix;
    int64_t next_motion, next_schedule, last_fallback, missing_since;
    int64_t checked_unix;
    float position_progress;
    bool estimated;
    char source[32];
    int http_status;
} app_tracker_flight_t;

bool app_tracker_coordinates_valid(double lat, double lon);
void app_tracker_preserve_runtime(app_tracker_flight_t *next, const app_tracker_flight_t *old);
double app_tracker_progress(const app_tracker_flight_t *flight, double lat, double lon);
void app_tracker_route_coordinate(const app_tracker_flight_t *flight, double progress, double *lat,
                                  double *lon);
int64_t app_tracker_timestamp(const cJSON *value);
bool app_tracker_parse_opensky(app_tracker_flight_t *flight, const cJSON *root, int64_t now);
bool app_tracker_parse_schedule(app_tracker_flight_t *flight, const cJSON *root, bool flightaware,
                                int64_t now);
bool app_tracker_parse_position(app_tracker_flight_t *flight, const cJSON *root, int64_t now);
bool app_tracker_airborne(const app_tracker_flight_t *flight);
bool app_tracker_terminal(const app_tracker_flight_t *flight);
void app_tracker_reconcile(app_tracker_flight_t *flight, int64_t now);
int app_tracker_select(const app_tracker_flight_t *flights, unsigned count, int current,
                       int64_t now);
int app_tracker_schedule_interval(const app_tracker_flight_t *flight, int64_t now);
bool app_tracker_schedule_due(const app_tracker_flight_t *flight, int64_t now);

/* Queue replacement and publication are serialized against provider replies.
 * Network I/O always runs in the existing data worker, never in the renderer. */
void app_tracker_start(void);
void app_tracker_load(void);
bool app_tracker_install(const cJSON *root);
bool app_tracker_clear(void);
bool app_tracker_owns_updates(void);
void app_tracker_tick(void);
cJSON *app_tracker_snapshot(void);
bool app_tracker_validate_flightaware(void);
