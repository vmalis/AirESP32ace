// AirESP32ace
// Created by VM, August 2026

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    AIRSPACE_PREVIEW_RADAR_MAIN = 0,
    AIRSPACE_PREVIEW_RADAR_LOCATIONS,
    AIRSPACE_PREVIEW_RADAR_CONNECTIVITY,
    AIRSPACE_PREVIEW_RADAR_BRIGHTNESS,
    AIRSPACE_PREVIEW_RADAR_APIS,
    AIRSPACE_PREVIEW_AIRCRAFT_DETAIL,
    AIRSPACE_PREVIEW_FLIGHT_GLOBE,
    AIRSPACE_PREVIEW_FLIGHT_FRAME,
    AIRSPACE_PREVIEW_FLIGHT_DETAIL,
    AIRSPACE_PREVIEW_WEATHER_CURRENT,
    AIRSPACE_PREVIEW_WEATHER_FORECAST,
    AIRSPACE_PREVIEW_WEATHER_RADAR,
    AIRSPACE_PREVIEW_SPACE_SOLAR,
    AIRSPACE_PREVIEW_SPACE_MOON,
    AIRSPACE_PREVIEW_SPACE_ISS,
    AIRSPACE_PREVIEW_CLOCK_GMT,
    AIRSPACE_PREVIEW_CLOCK_WORLD,
    AIRSPACE_PREVIEW_LAUNCH_MAIN,
    AIRSPACE_PREVIEW_LAUNCH_MAP,
    AIRSPACE_PREVIEW_LAUNCH_QR,
    AIRSPACE_PREVIEW_POWER_MENU,
    AIRSPACE_PREVIEW_DOCK_ON,
    AIRSPACE_PREVIEW_DOCK_OFF,
    AIRSPACE_PREVIEW_SPACE_ECLIPSE,
};

bool airspace_renderer_initialize(const char *asset_root);
void airspace_renderer_set_page(int page);
void airspace_renderer_set_accent(int accent);
void airspace_renderer_set_weather_units(int units);
void airspace_renderer_set_location(const char *key);
void airspace_renderer_set_clock_cities(const char *keys);
void airspace_renderer_set_weather(
    bool valid,
    bool air_quality_valid,
    const char *location_key,
    int fetched_unix,
    int temperature,
    int apparent_temperature,
    int code,
    int humidity_percent,
    int pressure_hpa,
    int precipitation_tenths_mm,
    int uv_tenths,
    int aqi,
    int wind_kt,
    int wind_degrees,
    const char *unit,
    const char *summary,
    const char *wind,
    const char *sunrise,
    const char *sunset);
void airspace_renderer_set_weather_forecast(
    int index,
    const char *day,
    int code,
    int high,
    int low,
    int precipitation_percent);
void airspace_renderer_begin_aircraft(
    bool valid,
    int count,
    int raw_count,
    int usable_count,
    int http_status,
    float radius_miles,
    bool auth_used,
    int fetched_unix);
void airspace_renderer_set_aircraft_identity(
    int index,
    const char *icao24,
    const char *callsign,
    const char *typecode,
    const char *model,
    const char *registration,
    const char *manufacturer,
    const char *engine_type,
    int engine_count,
    const char *type_source,
    const char *airline_name,
    const char *airline_iata,
    const char *airline_icao);
void airspace_renderer_set_aircraft_route(
    int index,
    const char *origin,
    const char *destination,
    bool metadata_valid,
    bool route_valid,
    bool route_progress_valid,
    float route_progress,
    float route_distance_nm,
    float route_distance_covered_nm,
    float route_distance_remaining_nm);
void airspace_renderer_set_aircraft_motion(
    int index,
    float latitude,
    float longitude,
    int altitude_ft,
    int speed_kt,
    int track_deg,
    bool track_valid,
    float vertical_rate_mps,
    bool vertical_rate_valid,
    int position_unix,
    int contact_unix,
    int category,
    float distance_nm,
    int bearing_deg);
void airspace_renderer_set_flight(
    const char *number,
    const char *callsign,
    const char *airline_iata,
    const char *airline_name,
    const char *aircraft_type,
    const char *origin,
    const char *origin_city,
    const char *destination,
    const char *destination_city,
    const char *origin_terminal,
    const char *origin_gate,
    const char *destination_terminal,
    const char *destination_gate,
    float progress,
    int altitude_m,
    int speed_kph,
    int heading_deg);
void airspace_renderer_set_flight_timing(
    const char *status,
    const char *vertical_phase,
    const char *journey_phase,
    const char *duration,
    const char *elapsed,
    const char *remaining,
    const char *origin_country,
    const char *destination_country,
    bool delayed,
    int route_distance_km,
    int origin_current_utc_offset_seconds,
    int destination_current_utc_offset_seconds,
    int departure_utc_offset_seconds,
    int arrival_utc_offset_seconds,
    int64_t estimated_departure_unix,
    int64_t actual_departure_unix,
    int64_t estimated_arrival_unix,
    int64_t actual_arrival_unix);
void airspace_renderer_set_route_points(
    const int16_t *xy,
    int count);
void airspace_renderer_set_map(int slot, const uint8_t *bytes, size_t length);
void airspace_renderer_set_route_flag(
    int endpoint,
    const uint8_t *bytes,
    size_t length);
void airspace_renderer_force_tracked_time_page(int page);
void airspace_renderer_set_airline_logo(
    const char *iata,
    const uint8_t *bytes,
    size_t length,
    int width,
    int height);
void airspace_renderer_set_launch(
    const char *identifier,
    const char *mission,
    const char *full_name,
    const char *agency,
    const char *vehicle,
    const char *site,
    const char *pad,
    const char *location,
    const char *precision,
    const char *precision_abbreviation,
    const char *status,
    const char *status_abbreviation,
    const char *watch_url,
    int64_t net_unix,
    int64_t updated_unix,
    int current_utc_offset_seconds,
    int weather_probability_percent);
void airspace_renderer_render(void);
const uint16_t *airspace_renderer_pixels(void);
int airspace_renderer_width(void);
int airspace_renderer_height(void);
int airspace_renderer_stride_bytes(void);
const char *airspace_renderer_error(void);

#ifdef __cplusplus
}
#endif
