// AirESP32ace
// Created by VM, August 2026

#pragma once
struct cJSON;
#define APP_OPENSKY_DEFERRED (-2) /* no request to provider; do not use paid fallback */

/* Called only from the data worker; shares Radar's token and rate limiter. */
struct cJSON *app_data_tracker_opensky(const char *query, int *code);

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Shared, bounded transport. The caller owns the parsed tree. retry_after is
 * only overwritten when a valid numeric Retry-After <= 1 day is received. */
struct cJSON *app_data_request_provider_json(const char *url, const char *form,
    const char *flightaware_key, size_t max_len, int *code, int *retry_after);

#define APP_MAX_AIRCRAFT 16
#define APP_WEATHER_FORECAST_DAYS 5
#define APP_ISS_TRACK_POINTS 10
#define APP_WEATHER_RADAR_FRAME_COUNT 13
#define APP_WEATHER_RADAR_FORECAST_STEP_MINUTES 10
#define APP_WEATHER_RADAR_VIEW_SIZE 233
#define APP_WEATHER_RADAR_OVERLAY_SIZE 465
#define APP_WEATHER_RADAR_OVERLAY_PIXELS \
    (APP_WEATHER_RADAR_OVERLAY_SIZE * APP_WEATHER_RADAR_OVERLAY_SIZE)
#define APP_WEATHER_RADAR_OVERLAY_BYTES \
    (APP_WEATHER_RADAR_OVERLAY_PIXELS * 3)

typedef enum {
    APP_DATA_IDLE = 0,
    APP_DATA_WAITING_FOR_WIFI,
    APP_DATA_FETCHING,
    APP_DATA_READY,
    APP_DATA_ERROR,
} app_data_state_t;

typedef enum {
    APP_API_HEALTH_PENDING = 0,
    APP_API_HEALTH_VALID,
    APP_API_HEALTH_INVALID,
    APP_API_HEALTH_OFFLINE,
    APP_API_HEALTH_LIMITED,
    APP_API_HEALTH_UNAVAILABLE,
} app_api_health_state_t;

typedef enum {
    APP_AIRCRAFT_TYPE_SOURCE_NONE = 0,
    APP_AIRCRAFT_TYPE_SOURCE_ADSBDB,
    APP_AIRCRAFT_TYPE_SOURCE_AIRLABS,
    APP_AIRCRAFT_TYPE_SOURCE_FLIGHTAWARE,
} app_aircraft_type_source_t;

typedef struct {
    bool valid;
    bool metadata_valid;
    bool route_valid;
    char icao24[8];
    char callsign[12];
    char typecode[8];
    char model[32];
    char registration[12];
    char manufacturer[20];
    char engine_type[16];
    int engine_count;
    app_aircraft_type_source_t type_source;
    char airline_name[32];
    char airline_iata[4];
    char airline_icao[5];
    char origin_iata[5];
    char destination_iata[5];
    bool route_progress_valid;
    float route_progress;
    float route_distance_nm;
    float route_distance_covered_nm;
    float route_distance_remaining_nm;
    float latitude;
    float longitude;
    int altitude_ft;
    int speed_kt;
    int track_deg;
    bool track_valid;
    float vertical_rate_mps;
    bool vertical_rate_valid;
    int position_unix;
    int contact_unix;
    int category;
    float distance_nm;
    int bearing_deg;
} app_aircraft_t;

typedef struct {
    app_data_state_t state;
    app_api_health_state_t flightaware_api_health;
    app_api_health_state_t opensky_api_health;
    app_api_health_state_t airlabs_api_health;
    uint32_t api_health_revision;
    bool weather_valid;
    bool weather_air_quality_valid;
    bool metar_valid;
    bool radar_valid;
    bool weather_radar_overlay_valid;
    bool aircraft_valid;
    bool iss_valid;
    bool aircraft_auth_used;
    int aircraft_count;
    int aircraft_raw_count;
    int aircraft_usable_count;
    int aircraft_http_status;
    float aircraft_radius_miles;
    int fetched_unix;
    uint32_t revision; /* UI publication generation, independent of observation time. */
    int aircraft_fetched_unix;
    int weather_fetched_unix;
    int metar_observed_unix;
    int last_attempt_unix;
    char weather_location_key[8];
    int weather_temperature;
    int weather_apparent_temperature;
    int weather_code;
    int weather_humidity_percent;
    int weather_pressure_hpa;
    int weather_precipitation_tenths_mm;
    int weather_uv_tenths;
    int weather_aqi;
    int weather_wind_kt;
    int weather_wind_deg;
    char weather_unit[4];
    char weather_summary[64];
    char weather_wind[32];
    char weather_sunrise[6];
    char weather_sunset[6];
    char forecast_day[APP_WEATHER_FORECAST_DAYS][4];
    int forecast_code[APP_WEATHER_FORECAST_DAYS];
    int forecast_high[APP_WEATHER_FORECAST_DAYS];
    int forecast_low[APP_WEATHER_FORECAST_DAYS];
    int forecast_precip_percent[APP_WEATHER_FORECAST_DAYS];
    char metar_station[8];
    char metar_category[8];
    int metar_temp_c;
    int metar_wind_kt;
    int metar_wind_deg;
    char metar_raw[96];
    int radar_generated_unix;
    int radar_frame_unix;
    char radar_path[96];
    int weather_radar_frame_count;
    int weather_radar_frame_unix[APP_WEATHER_RADAR_FRAME_COUNT];
    uint32_t weather_radar_overlay_revision;
    float weather_radar_overlay_radius_miles;
    float weather_radar_motion_x;
    float weather_radar_motion_y;
    char weather_radar_overlay_location_key[8];
    float iss_latitude;
    float iss_longitude;
    float iss_altitude_km;
    float iss_velocity_kph;
    int iss_timestamp;
    char iss_visibility[16];
    int iss_track_count;
    float iss_track_latitude[APP_ISS_TRACK_POINTS];
    float iss_track_longitude[APP_ISS_TRACK_POINTS];
    int iss_track_timestamp[APP_ISS_TRACK_POINTS];
    app_aircraft_t aircraft[APP_MAX_AIRCRAFT];
} app_data_status_t;

void app_data_start(void);
void app_data_set_paused(bool paused);
void app_data_get_status(app_data_status_t *status);
void app_data_request_refresh(void);
void app_data_request_api_validation(void);
void app_data_request_aircraft_enrichment(const char *icao24, const char *callsign);
bool app_data_aircraft_enrichment_pending(const char *icao24);
bool app_data_get_airport_city(const char *code, char *city, size_t city_size);
bool app_data_airline_logo_code(const app_aircraft_t *aircraft, char code[4]);
void app_data_request_airline_logo(const char *iata);
const uint8_t *app_data_get_airline_logo(const char *iata,
                                         size_t *len,
                                         uint16_t *width,
                                         uint16_t *height,
                                         uint32_t *revision);
void app_data_request_weather_radar(float radius_miles);
void app_data_set_weather_location_key(const char *location_key);
const uint8_t *app_data_get_weather_radar_overlay(size_t *len, uint32_t *revision);
const uint8_t *app_data_get_weather_radar_overlay_frame(int frame_index,
                                                        size_t *len,
                                                        uint32_t *revision,
                                                        int *frame_unix);
void app_data_set_aircraft_radius_miles(float miles);
const char *app_data_state_text(app_data_state_t state);
