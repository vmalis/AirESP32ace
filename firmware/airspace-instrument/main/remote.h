// AirESP32ace
// Created by VM, August 2026

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct cJSON;

#define APP_REMOTE_USB_TX_BUFFER_BYTES 512U
/* Native USB queues an entire write in its bounded ring buffer. */
static inline size_t app_remote_usb_tx_chunk(size_t remaining)
{
    const size_t limit = APP_REMOTE_USB_TX_BUFFER_BYTES / 2;
    return remaining < limit ? remaining : limit;
}

#define APP_REMOTE_MAP_WIDTH 466
#define APP_REMOTE_MAP_HEIGHT 466
#define APP_REMOTE_MAP_BYTES (APP_REMOTE_MAP_WIDTH * APP_REMOTE_MAP_HEIGHT * 2)
#define APP_REMOTE_RADAR_MAP_RANGE_COUNT 3
#define APP_REMOTE_ROUTE_MAP_VIEW_COUNT 2
#define APP_REMOTE_ROUTE_FLAG_WIDTH 40
#define APP_REMOTE_ROUTE_FLAG_HEIGHT 40
#define APP_REMOTE_ROUTE_FLAG_BYTES \
    (APP_REMOTE_ROUTE_FLAG_WIDTH * APP_REMOTE_ROUTE_FLAG_HEIGHT * 2)
#define APP_REMOTE_TRACKED_ROUTE_MAX_POINTS 24
#define APP_REMOTE_WEATHER_CITY_ICON_SIZE 128
#define APP_REMOTE_WEATHER_CITY_ICON_PIXELS \
    (APP_REMOTE_WEATHER_CITY_ICON_SIZE * APP_REMOTE_WEATHER_CITY_ICON_SIZE)
#define APP_REMOTE_WEATHER_CITY_ICON_BYTES \
    (APP_REMOTE_WEATHER_CITY_ICON_PIXELS * 3)
#define APP_REMOTE_SPACE_WORLD_WIDTH 466
#define APP_REMOTE_SPACE_WORLD_HEIGHT 466
#define APP_REMOTE_SPACE_WORLD_BYTES \
    (APP_REMOTE_SPACE_WORLD_WIDTH * APP_REMOTE_SPACE_WORLD_HEIGHT * 2)

typedef enum {
    APP_REMOTE_STYLE_XDR_RADAR = 0,
    APP_REMOTE_STYLE_CARTOGRAPHIC_GLASS,
    APP_REMOTE_STYLE_GMT_CHRONOGRAPH,
    APP_REMOTE_STYLE_WEATHER,
    APP_REMOTE_STYLE_WEATHER_RADAR,
    APP_REMOTE_STYLE_SPACE,
    APP_REMOTE_STYLE_SPACE_ISS,
    APP_REMOTE_STYLE_SPACE_MOON,
    APP_REMOTE_STYLE_LAUNCH,
    APP_REMOTE_STYLE_LAUNCH_MAP,
    APP_REMOTE_STYLE_LAUNCH_QR,
} app_remote_style_t;

typedef enum {
    APP_REMOTE_LAUNCH_NASA = 0,
    APP_REMOTE_LAUNCH_ROSCOSMOS,
    APP_REMOTE_LAUNCH_SPACEX,
    APP_REMOTE_LAUNCH_AGENCY_COUNT,
} app_remote_launch_agency_t;

typedef struct {
    bool valid;
    char id[40];
    char mission[48];
    char full_name[64];
    char agency[16];
    char vehicle[16];
    char site[16];
    char pad[32];
    char location[48];
    char precision[24];
    char precision_abbrev[8];
    char status[32];
    char status_abbrev[8];
    char watch_url[192];
    int64_t net_unix;
    int64_t updated_unix;
    int current_utc_offset_s;
    int weather_probability_percent;
} app_remote_launch_t;

typedef struct {
    app_remote_style_t style;
    float route_progress;
    bool tracked_flight_valid;
    bool tracked_flight_delayed;
    char tracked_flight_number[16];
    char tracked_flight_callsign[16];
    char tracked_flight_airline_iata[4];
    char tracked_flight_airline_name[40];
    char tracked_flight_aircraft_type[16];
    char tracked_flight_origin[8];
    char tracked_flight_origin_city[28];
    char tracked_flight_origin_country[3];
    char tracked_flight_destination[8];
    char tracked_flight_destination_city[28];
    char tracked_flight_destination_country[3];
    char tracked_flight_status[56];
    char tracked_flight_duration[24];
    char tracked_flight_distance[24];
    char tracked_flight_eta[16];
    char tracked_flight_elapsed[16];
    char tracked_flight_remaining[16];
    char tracked_flight_phase[16];
    int tracked_flight_altitude_m;
    int tracked_flight_speed_kph;
    int tracked_flight_heading_deg;
    bool tracked_flight_heading_valid;
    bool map_valid;
    size_t map_bytes;
    uint32_t route_map_valid_mask;
    uint32_t route_flag_valid_mask;
    uint32_t route_flag_revision;
    bool radar_map_valid;
    size_t radar_map_bytes;
    uint32_t radar_map_valid_mask;
    uint32_t revision;
    uint32_t map_revision;
    uint32_t radar_map_revision;
    uint32_t state_pushes;
    uint32_t map_pushes;
    uint32_t radar_map_pushes;
    int last_push_unix;
    char last_error[80];
    uint32_t tracked_route_geometry_version;
    uint8_t tracked_route_point_count;
    int16_t tracked_route_points[
        APP_REMOTE_TRACKED_ROUTE_MAX_POINTS
    ][2];
    char tracked_flight_journey_phase[12];
    int tracked_flight_route_distance_km;
    int tracked_flight_origin_current_utc_offset_s;
    int tracked_flight_destination_current_utc_offset_s;
    int tracked_flight_departure_utc_offset_s;
    int tracked_flight_arrival_utc_offset_s;
    int64_t tracked_flight_estimated_departure_unix;
    int64_t tracked_flight_actual_departure_unix;
    int64_t tracked_flight_estimated_arrival_unix;
    int64_t tracked_flight_actual_arrival_unix;
    bool launch_valid;
    char launch_id[40];
    char launch_mission[48];
    char launch_full_name[64];
    char launch_agency[16];
    char launch_vehicle[16];
    char launch_site[16];
    char launch_pad[32];
    char launch_location[48];
    char launch_precision[24];
    char launch_precision_abbrev[8];
    char launch_status[32];
    char launch_status_abbrev[8];
    char launch_watch_url[192];
    int64_t launch_net_unix;
    int64_t launch_updated_unix;
    int launch_current_utc_offset_s;
    int launch_weather_probability_percent;
    app_remote_launch_t launch_options[
        APP_REMOTE_LAUNCH_AGENCY_COUNT
    ];
    uint8_t launch_option_mask;
    char tracked_flight_origin_terminal[8];
    char tracked_flight_origin_gate[12];
    char tracked_flight_destination_terminal[8];
    char tracked_flight_destination_gate[12];
    uint8_t tracked_flight_slot;
} app_remote_status_t;

void app_remote_start(void);
/* Device-owned launch publication; does not change the selected screen. */
void app_remote_publish_launches(const app_remote_launch_t launches[3], uint8_t mask);
void app_remote_get_status(app_remote_status_t *out);
/* Pure in-place merge. The caller must hold its status lock. No allocation
 * or full app_remote_status_t automatic copy is permitted in this path. */
void app_remote_merge_status(app_remote_status_t *current, const app_remote_status_t *incoming,
                             bool tracker_owns_updates, bool launches_own_updates);
bool app_remote_decode_state(const struct cJSON *root, app_remote_status_t *out);
void app_remote_publish_tracker(const app_remote_status_t *flight, bool owns_updates);
bool app_remote_get_launch_status(
    app_remote_launch_agency_t agency,
    app_remote_status_t *out);
void app_remote_refresh_map_storage_index(void);
void app_remote_refresh_config_file(void);
const uint8_t *app_remote_get_map(size_t *len, uint32_t *revision);
const uint8_t *app_remote_get_map_for_view(int view_index, size_t *len, uint32_t *revision);
const uint8_t *app_remote_get_route_flag(int endpoint_index,
                                         size_t *len,
                                         uint32_t *revision);
const uint8_t *app_remote_get_weather_city_icon(const char *location_key,
                                                 size_t *len,
                                                 uint32_t *revision);
const uint8_t *app_remote_get_space_asset(const char *name,
                                           size_t *len,
                                           uint16_t *width,
                                           uint16_t *height,
                                           bool *has_alpha,
                                           uint32_t *revision);
void app_remote_release_space_assets_except(
    const char *const *keep_names,
    size_t keep_count);
const uint8_t *app_remote_get_radar_map(size_t *len, uint32_t *revision);
const uint8_t *app_remote_get_radar_map_for_range(float miles, size_t *len, uint32_t *revision, int *range_index);
const uint8_t *app_remote_get_radar_map_for_location(const char *location_key,
                                                     float miles,
                                                     size_t *len,
                                                     uint32_t *revision,
                                                     int *range_index);
int app_remote_radar_range_index_for_miles(float miles);
float app_remote_radar_range_miles_at(int index);
const char *app_remote_style_text(app_remote_style_t style);
