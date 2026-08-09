// AirESP32ace
// Created by VM, August 2026

#include "desktop_platform.h"
#include "desktop_renderer.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "app_config.h"
#include "data.h"
#include "locations.h"
#include "network.h"
#include "remote.h"

enum {
    DESKTOP_MAP_RADAR = 0,
    DESKTOP_MAP_FLIGHT_GLOBE = 1,
    DESKTOP_MAP_FLIGHT_FRAME = 2,
    DESKTOP_MAP_COUNT = 3,
};

typedef struct {
    const char *name;
    uint16_t width;
    uint16_t height;
    bool alpha;
    uint8_t *bytes;
    size_t length;
} desktop_space_asset_t;

static app_config_t desktop_config;
static app_data_status_t desktop_data;
static app_remote_status_t desktop_remote;
static uint8_t *desktop_maps[DESKTOP_MAP_COUNT];
static size_t desktop_map_lengths[DESKTOP_MAP_COUNT];
static uint32_t desktop_map_revisions[DESKTOP_MAP_COUNT];
static uint8_t *desktop_route_flags[2];
static size_t desktop_route_flag_lengths[2];
static uint32_t desktop_route_flag_revisions[2];
static uint8_t *desktop_airline_logo;
static size_t desktop_airline_logo_length;
static uint16_t desktop_airline_logo_width;
static uint16_t desktop_airline_logo_height;
static uint32_t desktop_airline_logo_revision;
static char desktop_airline_logo_iata[4];
static uint8_t *desktop_weather_city_icon;
static char desktop_weather_city_icon_key[16];
static uint32_t desktop_weather_city_icon_revision;
static uint8_t desktop_weather_radar_overlay[APP_WEATHER_RADAR_OVERLAY_BYTES];
static char desktop_asset_root[1024];
static bool desktop_initialized;

static desktop_space_asset_t desktop_space_assets[] = {
    {"sun", 128, 128, true, NULL, 0},
    {"earth", 80, 80, true, NULL, 0},
    {"moon", 64, 64, true, NULL, 0},
    {"iss", 64, 64, true, NULL, 0},
    {"world", 466, 466, false, NULL, 0},
    {"nasa", 72, 60, true, NULL, 0},
    {"roscosm", 60, 60, true, NULL, 0},
    {"launch_sls", 140, 300, true, NULL, 0},
    {"launch_falcon9", 140, 300, true, NULL, 0},
    {"launch_soyuz", 140, 300, true, NULL, 0},
    {"launch_starship", 140, 300, true, NULL, 0},
    {"launch_spacex", 144, 32, true, NULL, 0},
    {"launch_florida", 466, 466, false, NULL, 0},
    {"launch_starbase", 466, 466, false, NULL, 0},
    {"launch_baikonur", 466, 466, false, NULL, 0},
    {"iss_flags", 402, 131, true, NULL, 0},
};

static void desktop_copy(
    char *destination,
    size_t destination_size,
    const char *source)
{
    if (destination == NULL || destination_size == 0) {
        return;
    }
    snprintf(
        destination,
        destination_size,
        "%s",
        source != NULL ? source : "");
}

static void desktop_initialize_weather_radar_overlay(void)
{
    uint8_t *alpha_plane =
        desktop_weather_radar_overlay +
        APP_WEATHER_RADAR_OVERLAY_PIXELS * 2U;
    for (int y = 0; y < APP_WEATHER_RADAR_OVERLAY_SIZE; y++) {
        for (int x = 0; x < APP_WEATHER_RADAR_OVERLAY_SIZE; x++) {
            const float dx1 = ((float)x - 76.0f) / 34.0f;
            const float dy1 = ((float)y - 148.0f) / 24.0f;
            const float dx2 = ((float)x - 151.0f) / 29.0f;
            const float dy2 = ((float)y - 88.0f) / 38.0f;
            const float dx3 = ((float)x - 181.0f) / 19.0f;
            const float dy3 = ((float)y - 174.0f) / 27.0f;
            float intensity =
                0.92f * expf(-(dx1 * dx1 + dy1 * dy1) * 1.25f) +
                0.74f * expf(-(dx2 * dx2 + dy2 * dy2) * 1.45f) +
                0.58f * expf(-(dx3 * dx3 + dy3 * dy3) * 1.65f);
            if (intensity > 1.0f) {
                intensity = 1.0f;
            }

            uint8_t red = 20;
            uint8_t green = 194;
            uint8_t blue = 92;
            if (intensity > 0.72f) {
                red = 242;
                green = 76;
                blue = 53;
            } else if (intensity > 0.43f) {
                red = 250;
                green = 191;
                blue = 40;
            }
            const uint16_t rgb565 =
                (uint16_t)(((uint16_t)(red >> 3) << 11) |
                           ((uint16_t)(green >> 2) << 5) |
                           (uint16_t)(blue >> 3));
            const size_t pixel =
                (size_t)y * APP_WEATHER_RADAR_OVERLAY_SIZE + (size_t)x;
            desktop_weather_radar_overlay[pixel * 2U] =
                (uint8_t)(rgb565 & 0xffU);
            desktop_weather_radar_overlay[pixel * 2U + 1U] =
                (uint8_t)(rgb565 >> 8);
            alpha_plane[pixel] =
                intensity < 0.08f
                    ? 0
                    : (uint8_t)lroundf(205.0f * intensity);
        }
    }
}

static void desktop_initialize_fixture(void)
{
    if (desktop_initialized) {
        return;
    }
    desktop_initialized = true;
    memset(&desktop_config, 0, sizeof(desktop_config));
    desktop_config.active_wifi_profile = APP_WIFI_PROFILE_HOME;
    desktop_copy(
        desktop_config.wifi_profiles[APP_WIFI_PROFILE_HOME].ssid,
        sizeof(desktop_config.wifi_profiles[APP_WIFI_PROFILE_HOME].ssid),
        "Studio Wi-Fi");
    desktop_copy(
        desktop_config.wifi_profiles[APP_WIFI_PROFILE_HOTSPOT].ssid,
        sizeof(desktop_config.wifi_profiles[APP_WIFI_PROFILE_HOTSPOT].ssid),
        "iPhone Hotspot");
    desktop_config.aircraft_labels_enabled = true;
    desktop_config.radar_wedge_enabled = true;
    desktop_config.iss_flags_enabled = false;
    desktop_config.weather_units = APP_WEATHER_UNITS_CELSIUS;
    desktop_config.accent_color = APP_ACCENT_GREEN;
    desktop_config.accent_cyan_rgb = 0x00A3BF;
    desktop_config.accent_orange_rgb = 0xD47A17;
    desktop_config.accent_red_rgb = 0xBC252F;
    desktop_config.opensky_enabled = true;
    desktop_config.airlabs_api_key[0] = 'x';
    desktop_config.flightaware_available = true;
    desktop_copy(
        desktop_config.clock_city_keys,
        sizeof(desktop_config.clock_city_keys),
        "SJC,MOW,DUS,JFK,ICN");
    desktop_copy(
        desktop_config.radar_city_keys,
        sizeof(desktop_config.radar_city_keys),
        "SJC,SFO,SAN,JFK,MOW,LED,DUS,ICN");
    app_location_apply_preset(&desktop_config, "SJC");

    memset(&desktop_data, 0, sizeof(desktop_data));
    desktop_data.state = APP_DATA_READY;
    desktop_data.weather_valid = true;
    desktop_data.weather_air_quality_valid = true;
    desktop_data.metar_valid = true;
    desktop_data.aircraft_valid = true;
    desktop_data.iss_valid = true;
    desktop_data.aircraft_auth_used = true;
    desktop_data.aircraft_count = 7;
    desktop_data.aircraft_raw_count = 19;
    desktop_data.aircraft_usable_count = 7;
    desktop_data.aircraft_http_status = 200;
    desktop_data.aircraft_radius_miles = 20.0f;
    desktop_data.fetched_unix = (int)time(NULL);
    desktop_copy(
        desktop_data.weather_location_key,
        sizeof(desktop_data.weather_location_key),
        desktop_config.location_key);
    desktop_data.weather_temperature = 21;
    desktop_data.weather_apparent_temperature = 21;
    desktop_data.weather_code = 1;
    desktop_data.weather_humidity_percent = 54;
    desktop_data.weather_pressure_hpa = 1016;
    desktop_data.weather_precipitation_tenths_mm = 0;
    desktop_data.weather_uv_tenths = 58;
    desktop_data.weather_aqi = 34;
    desktop_data.weather_wind_kt = 11;
    desktop_data.weather_wind_deg = 315;
    desktop_data.weather_radar_overlay_valid = true;
    desktop_data.weather_radar_frame_count =
        APP_WEATHER_RADAR_FRAME_COUNT;
    desktop_data.weather_radar_overlay_revision = 1;
    desktop_data.weather_radar_overlay_radius_miles = 20.0f;
    desktop_data.weather_radar_motion_x = 0.75f;
    desktop_data.weather_radar_motion_y = -0.35f;
    desktop_copy(
        desktop_data.weather_radar_overlay_location_key,
        sizeof(desktop_data.weather_radar_overlay_location_key),
        desktop_config.location_key);
    desktop_initialize_weather_radar_overlay();
    for (int index = 0; index < APP_WEATHER_RADAR_FRAME_COUNT; index++) {
        desktop_data.weather_radar_frame_unix[index] =
            (int)time(NULL) +
            index * APP_WEATHER_RADAR_FORECAST_STEP_MINUTES * 60;
    }
    desktop_copy(
        desktop_data.weather_unit,
        sizeof(desktop_data.weather_unit),
        "C");
    desktop_copy(
        desktop_data.weather_summary,
        sizeof(desktop_data.weather_summary),
        "Mostly clear");
    desktop_copy(
        desktop_data.weather_wind,
        sizeof(desktop_data.weather_wind),
        "NW 11 kt");
    desktop_copy(
        desktop_data.weather_sunrise,
        sizeof(desktop_data.weather_sunrise),
        "06:08");
    desktop_copy(
        desktop_data.weather_sunset,
        sizeof(desktop_data.weather_sunset),
        "20:15");

    static const char *days[APP_WEATHER_FORECAST_DAYS] = {
        "THU", "FRI", "SAT", "SUN", "MON",
    };
    static const int codes[APP_WEATHER_FORECAST_DAYS] = {
        1, 0, 2, 3, 61,
    };
    static const int highs[APP_WEATHER_FORECAST_DAYS] = {
        27, 29, 28, 25, 23,
    };
    static const int lows[APP_WEATHER_FORECAST_DAYS] = {
        14, 15, 16, 15, 14,
    };
    static const int rain[APP_WEATHER_FORECAST_DAYS] = {
        0, 0, 8, 18, 42,
    };
    for (int index = 0; index < APP_WEATHER_FORECAST_DAYS; index++) {
        desktop_copy(
            desktop_data.forecast_day[index],
            sizeof(desktop_data.forecast_day[index]),
            days[index]);
        desktop_data.forecast_code[index] = codes[index];
        desktop_data.forecast_high[index] = highs[index];
        desktop_data.forecast_low[index] = lows[index];
        desktop_data.forecast_precip_percent[index] = rain[index];
    }

    desktop_copy(
        desktop_data.metar_station,
        sizeof(desktop_data.metar_station),
        "KSJC");
    desktop_copy(
        desktop_data.metar_category,
        sizeof(desktop_data.metar_category),
        "VFR");
    desktop_data.metar_temp_c = 21;
    desktop_data.metar_wind_kt = 11;
    desktop_data.metar_wind_deg = 315;

    desktop_data.iss_latitude = 31.4f;
    desktop_data.iss_longitude = -145.2f;
    desktop_data.iss_altitude_km = 419.4f;
    desktop_data.iss_velocity_kph = 27584.0f;
    desktop_data.iss_timestamp = (int)time(NULL);
    desktop_copy(
        desktop_data.iss_visibility,
        sizeof(desktop_data.iss_visibility),
        "daylight");
    desktop_data.iss_track_count = APP_ISS_TRACK_POINTS;
    for (int index = 0; index < APP_ISS_TRACK_POINTS; index++) {
        const float fraction =
            (float)index / (APP_ISS_TRACK_POINTS - 1);
        desktop_data.iss_track_longitude[index] =
            -175.0f + fraction * 124.0f;
        desktop_data.iss_track_latitude[index] =
            47.0f * sinf(fraction * 2.6f - 1.2f);
        desktop_data.iss_track_timestamp[index] =
            desktop_data.iss_timestamp -
            (APP_ISS_TRACK_POINTS - 1 - index) * 180;
    }

    static const float distances[] = {
        4.8f, 7.6f, 10.2f, 13.5f, 16.1f, 18.3f, 19.4f,
    };
    static const int bearings[] = {
        325, 42, 110, 198, 273, 73, 236,
    };
    static const int headings[] = {
        145, 302, 76, 12, 250, 212, 38,
    };
    static const char *callsigns[] = {
        "SWA4772", "SKW4756", "UAL1842", "N521SP",
        "FDX1054", "ASA338", "N724MD",
    };
    static const char *types[] = {
        "B738", "E75L", "A320", "C172",
        "B763", "B739", "R44",
    };
    for (int index = 0; index < desktop_data.aircraft_count; index++) {
        app_aircraft_t *aircraft = &desktop_data.aircraft[index];
        aircraft->valid = true;
        aircraft->metadata_valid = true;
        aircraft->route_valid = index < 3;
        snprintf(
            aircraft->icao24,
            sizeof(aircraft->icao24),
            "a%05x",
            0x1420 + index * 73);
        desktop_copy(
            aircraft->callsign,
            sizeof(aircraft->callsign),
            callsigns[index]);
        desktop_copy(
            aircraft->typecode,
            sizeof(aircraft->typecode),
            types[index]);
        desktop_copy(
            aircraft->model,
            sizeof(aircraft->model),
            index == 0
                ? "Boeing 737-800"
                : (index == 1
                    ? "Embraer 175"
                    : types[index]));
        desktop_copy(
            aircraft->manufacturer,
            sizeof(aircraft->manufacturer),
            index == 1 ? "Embraer" : "Boeing");
        if (index == 0) {
            desktop_copy(
                aircraft->airline_name,
                sizeof(aircraft->airline_name),
                "Southwest Airlines");
            desktop_copy(
                aircraft->airline_iata,
                sizeof(aircraft->airline_iata),
                "WN");
            desktop_copy(
                aircraft->airline_icao,
                sizeof(aircraft->airline_icao),
                "SWA");
            desktop_copy(
                aircraft->origin_iata,
                sizeof(aircraft->origin_iata),
                "SAN");
            desktop_copy(
                aircraft->destination_iata,
                sizeof(aircraft->destination_iata),
                "SJC");
            aircraft->route_progress_valid = true;
            aircraft->route_progress = 0.72f;
            aircraft->route_distance_nm = 361.0f;
            aircraft->route_distance_covered_nm = 260.0f;
            aircraft->route_distance_remaining_nm = 101.0f;
        }
        aircraft->altitude_ft = 7200 + index * 1750;
        aircraft->speed_kt = 168 + index * 25;
        aircraft->track_deg = headings[index];
        aircraft->track_valid = true;
        aircraft->vertical_rate_mps =
            index % 3 == 0 ? 5.2f : (index % 3 == 1 ? 0.0f : -3.4f);
        aircraft->vertical_rate_valid = true;
        aircraft->distance_nm = distances[index];
        aircraft->bearing_deg = bearings[index];
        aircraft->category = index == 3 ? 1 : (index == 6 ? 7 : 4);
        const float bearing_radians =
            (float)bearings[index] *
            (float)M_PI / 180.0f;
        const float north_nm =
            cosf(bearing_radians) * distances[index];
        const float east_nm =
            sinf(bearing_radians) * distances[index];
        aircraft->latitude =
            37.3639f + north_nm / 60.0f;
        aircraft->longitude =
            -121.9289f +
            east_nm /
                (60.0f *
                 cosf(37.3639f * (float)M_PI / 180.0f));
        aircraft->position_unix = (int)time(NULL);
        aircraft->contact_unix = aircraft->position_unix;
    }

    memset(&desktop_remote, 0, sizeof(desktop_remote));
    desktop_remote.style = APP_REMOTE_STYLE_XDR_RADAR;
    desktop_remote.route_progress = 0.56f;
    desktop_remote.tracked_flight_valid = true;
    desktop_copy(
        desktop_remote.tracked_flight_number,
        sizeof(desktop_remote.tracked_flight_number),
        "YP111");
    desktop_copy(
        desktop_remote.tracked_flight_callsign,
        sizeof(desktop_remote.tracked_flight_callsign),
        "APZ111");
    desktop_copy(
        desktop_remote.tracked_flight_airline_iata,
        sizeof(desktop_remote.tracked_flight_airline_iata),
        "YP");
    desktop_copy(
        desktop_remote.tracked_flight_airline_name,
        sizeof(desktop_remote.tracked_flight_airline_name),
        "Air Premia");
    desktop_copy(
        desktop_remote.tracked_flight_aircraft_type,
        sizeof(desktop_remote.tracked_flight_aircraft_type),
        "B789");
    desktop_copy(
        desktop_remote.tracked_flight_origin,
        sizeof(desktop_remote.tracked_flight_origin),
        "ICN");
    desktop_copy(
        desktop_remote.tracked_flight_origin_city,
        sizeof(desktop_remote.tracked_flight_origin_city),
        "Seoul");
    desktop_copy(
        desktop_remote.tracked_flight_origin_country,
        sizeof(desktop_remote.tracked_flight_origin_country),
        "KR");
    desktop_copy(
        desktop_remote.tracked_flight_destination,
        sizeof(desktop_remote.tracked_flight_destination),
        "SFO");
    desktop_copy(
        desktop_remote.tracked_flight_destination_city,
        sizeof(desktop_remote.tracked_flight_destination_city),
        "San Francisco");
    desktop_copy(
        desktop_remote.tracked_flight_destination_country,
        sizeof(desktop_remote.tracked_flight_destination_country),
        "US");
    desktop_copy(
        desktop_remote.tracked_flight_status,
        sizeof(desktop_remote.tracked_flight_status),
        "En route");
    desktop_copy(
        desktop_remote.tracked_flight_duration,
        sizeof(desktop_remote.tracked_flight_duration),
        "10h 42m");
    desktop_copy(
        desktop_remote.tracked_flight_distance,
        sizeof(desktop_remote.tracked_flight_distance),
        "9,085 km");
    desktop_copy(
        desktop_remote.tracked_flight_eta,
        sizeof(desktop_remote.tracked_flight_eta),
        "16:48");
    desktop_copy(
        desktop_remote.tracked_flight_elapsed,
        sizeof(desktop_remote.tracked_flight_elapsed),
        "6h 02m");
    desktop_copy(
        desktop_remote.tracked_flight_remaining,
        sizeof(desktop_remote.tracked_flight_remaining),
        "4h 40m");
    desktop_copy(
        desktop_remote.tracked_flight_phase,
        sizeof(desktop_remote.tracked_flight_phase),
        "CRUISE");
    desktop_copy(
        desktop_remote.tracked_flight_journey_phase,
        sizeof(desktop_remote.tracked_flight_journey_phase),
        "enroute");
    const int64_t fixture_now =
        (int64_t)time(NULL);
    desktop_remote.tracked_flight_route_distance_km =
        9085;
    desktop_remote
        .tracked_flight_origin_current_utc_offset_s =
        9 * 3600;
    desktop_remote
        .tracked_flight_destination_current_utc_offset_s =
        -7 * 3600;
    desktop_remote.tracked_flight_departure_utc_offset_s =
        9 * 3600;
    desktop_remote.tracked_flight_arrival_utc_offset_s =
        -7 * 3600;
    desktop_remote.tracked_flight_actual_departure_unix =
        fixture_now - (6 * 3600 + 2 * 60);
    desktop_remote.tracked_flight_estimated_departure_unix =
        desktop_remote.tracked_flight_actual_departure_unix;
    desktop_remote.tracked_flight_estimated_arrival_unix =
        fixture_now + (4 * 3600 + 40 * 60);
    desktop_remote.tracked_flight_altitude_m = 11278;
    desktop_remote.tracked_flight_speed_kph = 903;
    desktop_remote.tracked_flight_heading_deg = 78;
    desktop_remote.tracked_flight_heading_valid = true;
    desktop_remote.launch_valid = true;
    desktop_copy(
        desktop_remote.launch_id,
        sizeof(desktop_remote.launch_id),
        "preview-artemis");
    desktop_copy(
        desktop_remote.launch_mission,
        sizeof(desktop_remote.launch_mission),
        "ARTEMIS III");
    desktop_copy(
        desktop_remote.launch_full_name,
        sizeof(desktop_remote.launch_full_name),
        "SLS Block 1 | Artemis III");
    desktop_copy(
        desktop_remote.launch_agency,
        sizeof(desktop_remote.launch_agency),
        "nasa");
    desktop_copy(
        desktop_remote.launch_vehicle,
        sizeof(desktop_remote.launch_vehicle),
        "sls");
    desktop_copy(
        desktop_remote.launch_site,
        sizeof(desktop_remote.launch_site),
        "florida");
    desktop_copy(
        desktop_remote.launch_pad,
        sizeof(desktop_remote.launch_pad),
        "LC-39B");
    desktop_copy(
        desktop_remote.launch_location,
        sizeof(desktop_remote.launch_location),
        "Kennedy Space Center");
    desktop_copy(
        desktop_remote.launch_precision,
        sizeof(desktop_remote.launch_precision),
        "Hour");
    desktop_copy(
        desktop_remote.launch_precision_abbrev,
        sizeof(desktop_remote.launch_precision_abbrev),
        "H");
    desktop_copy(
        desktop_remote.launch_status,
        sizeof(desktop_remote.launch_status),
        "Go for Launch");
    desktop_copy(
        desktop_remote.launch_status_abbrev,
        sizeof(desktop_remote.launch_status_abbrev),
        "GO");
    desktop_copy(
        desktop_remote.launch_watch_url,
        sizeof(desktop_remote.launch_watch_url),
        "https://www.nasa.gov/nasatv/");
    desktop_remote.launch_net_unix =
        fixture_now + 64 * 86400 + 8437;
    desktop_remote.launch_updated_unix =
        fixture_now;
    desktop_remote.launch_current_utc_offset_s =
        -4 * 3600;
    desktop_remote
        .launch_weather_probability_percent =
        82;
    desktop_remote.revision = 1;
    desktop_remote.map_revision = 1;
    desktop_remote.radar_map_revision = 1;
}

void airspace_desktop_set_asset_root(const char *asset_root)
{
    desktop_initialize_fixture();
    desktop_copy(
        desktop_asset_root,
        sizeof(desktop_asset_root),
        asset_root);
}

void app_config_init_defaults(void)
{
    desktop_initialize_fixture();
}

esp_err_t app_config_load(void)
{
    desktop_initialize_fixture();
    return ESP_OK;
}

esp_err_t app_config_save(const app_config_t *config)
{
    desktop_initialize_fixture();
    if (config == NULL) {
        return ESP_FAIL;
    }
    desktop_config = *config;
    return ESP_OK;
}

void app_config_get(app_config_t *out)
{
    desktop_initialize_fixture();
    if (out != NULL) {
        *out = desktop_config;
    }
}

const app_wifi_profile_t *app_config_active_wifi_profile(
    const app_config_t *config)
{
    if (config == NULL ||
        config->active_wifi_profile < 0 ||
        config->active_wifi_profile >= APP_WIFI_PROFILE_COUNT) {
        return NULL;
    }
    return &config->wifi_profiles[config->active_wifi_profile];
}

const char *app_config_wifi_profile_name(app_wifi_profile_id_t profile)
{
    return profile == APP_WIFI_PROFILE_HOTSPOT ? "HOTSPOT" : "HOME";
}

bool app_config_parse_wifi_profile_name(
    const char *name,
    app_wifi_profile_id_t *profile)
{
    if (name == NULL || profile == NULL) {
        return false;
    }
    *profile =
        strcasecmp(name, "HOTSPOT") == 0
            ? APP_WIFI_PROFILE_HOTSPOT
            : APP_WIFI_PROFILE_HOME;
    return true;
}

const char *app_config_accent_color_name(app_accent_color_t color)
{
    static const char *names[] = {
        "green", "cyan", "orange", "red",
    };
    return color >= 0 && color < APP_ACCENT_COUNT
        ? names[color]
        : names[0];
}

bool app_config_parse_accent_color_name(
    const char *name,
    app_accent_color_t *color)
{
    if (name == NULL || color == NULL) {
        return false;
    }
    for (int index = 0; index < APP_ACCENT_COUNT; index++) {
        if (strcasecmp(
                name,
                app_config_accent_color_name(
                    (app_accent_color_t)index)) == 0) {
            *color = (app_accent_color_t)index;
            return true;
        }
    }
    return false;
}

void app_network_start(void)
{
}

void app_network_apply_config(bool reconnect_wifi)
{
    (void)reconnect_wifi;
}

void app_network_get_status(app_network_status_t *status)
{
    desktop_initialize_fixture();
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->wifi_state = APP_WIFI_CONNECTED;
    status->wifi_configured = true;
    status->wifi_connected = true;
    status->time_synced = true;
    status->rssi = -43;
    desktop_copy(
        status->configured_ssid,
        sizeof(status->configured_ssid),
        desktop_config.wifi_profiles[
            desktop_config.active_wifi_profile].ssid);
    desktop_copy(status->ip, sizeof(status->ip), "192.168.1.84");
    desktop_copy(
        status->location_key,
        sizeof(status->location_key),
        desktop_config.location_key);
    desktop_copy(
        status->location_label,
        sizeof(status->location_label),
        desktop_config.location_label);
    desktop_copy(
        status->latitude,
        sizeof(status->latitude),
        desktop_config.location_lat);
    desktop_copy(
        status->longitude,
        sizeof(status->longitude),
        desktop_config.location_lon);
    desktop_copy(
        status->timezone,
        sizeof(status->timezone),
        desktop_config.timezone);
    status->aircraft_labels_enabled =
        desktop_config.aircraft_labels_enabled;
    status->radar_wedge_enabled =
        desktop_config.radar_wedge_enabled;
    desktop_copy(
        status->accent_color,
        sizeof(status->accent_color),
        app_config_accent_color_name(
            desktop_config.accent_color));
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(
        status->local_time,
        sizeof(status->local_time),
        "%H:%M:%S",
        &local);
}

const char *app_wifi_state_text(app_wifi_state_t state)
{
    switch (state) {
    case APP_WIFI_CONNECTED:
        return "CONNECTED";
    case APP_WIFI_CONNECTING:
        return "CONNECTING";
    case APP_WIFI_FAILED:
        return "FAILED";
    case APP_WIFI_NO_CREDENTIALS:
        return "NOT CONFIGURED";
    default:
        return "IDLE";
    }
}

void app_data_start(void)
{
}

void app_data_get_status(app_data_status_t *status)
{
    desktop_initialize_fixture();
    if (status != NULL) {
        desktop_data.fetched_unix = (int)time(NULL);
        *status = desktop_data;
    }
}

void app_data_request_refresh(void)
{
}

void app_data_request_aircraft_enrichment(
    const char *icao24,
    const char *callsign)
{
    (void)icao24;
    (void)callsign;
}

bool app_data_aircraft_enrichment_pending(const char *icao24)
{
    (void)icao24;
    return false;
}

bool app_data_get_airport_city(
    const char *code,
    char *city,
    size_t city_size)
{
    if (code == NULL || city == NULL || city_size == 0) {
        return false;
    }
    const app_location_preset_t *location =
        app_location_preset_find(code);
    if (location == NULL) {
        return false;
    }
    desktop_copy(city, city_size, location->label);
    return true;
}

bool app_data_airline_logo_code(
    const app_aircraft_t *aircraft,
    char code[4])
{
    if (aircraft == NULL || code == NULL ||
        aircraft->airline_iata[0] == '\0') {
        return false;
    }
    desktop_copy(code, 4, aircraft->airline_iata);
    return true;
}

void app_data_request_airline_logo(const char *iata)
{
    (void)iata;
}

const uint8_t *app_data_get_airline_logo(
    const char *iata,
    size_t *len,
    uint16_t *width,
    uint16_t *height,
    uint32_t *revision)
{
    const bool matches =
        iata != NULL &&
        desktop_airline_logo != NULL &&
        strcasecmp(iata, desktop_airline_logo_iata) == 0;
    if (len != NULL) {
        *len = matches ? desktop_airline_logo_length : 0;
    }
    if (width != NULL) {
        *width = matches ? desktop_airline_logo_width : 0;
    }
    if (height != NULL) {
        *height = matches ? desktop_airline_logo_height : 0;
    }
    if (revision != NULL) {
        *revision = desktop_airline_logo_revision;
    }
    return matches ? desktop_airline_logo : NULL;
}

void app_data_request_weather_radar(float radius_miles)
{
    desktop_initialize_fixture();
    desktop_data.weather_radar_overlay_radius_miles = radius_miles;
}

void app_data_set_weather_location_key(const char *location_key)
{
    desktop_initialize_fixture();
    const app_location_preset_t *location =
        app_location_preset_find(location_key);
    desktop_copy(
        desktop_data.weather_location_key,
        sizeof(desktop_data.weather_location_key),
        location != NULL
            ? location->key
            : desktop_config.location_key);
    desktop_copy(
        desktop_data.weather_radar_overlay_location_key,
        sizeof(desktop_data.weather_radar_overlay_location_key),
        desktop_data.weather_location_key);
}

const uint8_t *app_data_get_weather_radar_overlay(
    size_t *len,
    uint32_t *revision)
{
    if (len != NULL) {
        *len = APP_WEATHER_RADAR_OVERLAY_BYTES;
    }
    if (revision != NULL) {
        *revision = 1;
    }
    return desktop_weather_radar_overlay;
}

const uint8_t *app_data_get_weather_radar_overlay_frame(
    int frame_index,
    size_t *len,
    uint32_t *revision,
    int *frame_unix)
{
    desktop_initialize_fixture();
    if (frame_index < 0) {
        frame_index = 0;
    } else if (frame_index >= APP_WEATHER_RADAR_FRAME_COUNT) {
        frame_index = APP_WEATHER_RADAR_FRAME_COUNT - 1;
    }
    if (frame_unix != NULL) {
        *frame_unix = desktop_data.weather_radar_frame_unix[frame_index];
    }
    if (len != NULL) {
        *len = APP_WEATHER_RADAR_OVERLAY_BYTES;
    }
    if (revision != NULL) {
        *revision = (uint32_t)(frame_index + 1);
    }
    return desktop_weather_radar_overlay;
}

void app_data_set_aircraft_radius_miles(float miles)
{
    desktop_data.aircraft_radius_miles = miles;
}

const char *app_data_state_text(app_data_state_t state)
{
    switch (state) {
    case APP_DATA_READY:
        return "READY";
    case APP_DATA_FETCHING:
        return "FETCHING";
    case APP_DATA_ERROR:
        return "ERROR";
    case APP_DATA_WAITING_FOR_WIFI:
        return "WAITING";
    default:
        return "IDLE";
    }
}

void app_remote_start(void)
{
}

void app_remote_get_status(app_remote_status_t *out)
{
    desktop_initialize_fixture();
    if (out != NULL) {
        *out = desktop_remote;
    }
}

bool app_remote_get_launch_status(
    app_remote_launch_agency_t agency,
    app_remote_status_t *out)
{
    desktop_initialize_fixture();
    if (out == NULL || !desktop_remote.launch_valid) {
        return false;
    }
    const char *expected =
        agency == APP_REMOTE_LAUNCH_NASA
            ? "nasa"
            : agency == APP_REMOTE_LAUNCH_ROSCOSMOS
                ? "roscosmos"
                : "spacex";
    if (strcasecmp(
            desktop_remote.launch_agency,
            expected) != 0) {
        return false;
    }
    *out = desktop_remote;
    return true;
}

void app_remote_refresh_map_storage_index(void)
{
}

const uint8_t *app_remote_get_map(
    size_t *len,
    uint32_t *revision)
{
    return app_remote_get_map_for_view(0, len, revision);
}

const uint8_t *app_remote_get_map_for_view(
    int view_index,
    size_t *len,
    uint32_t *revision)
{
    const int slot =
        view_index == 1
            ? DESKTOP_MAP_FLIGHT_FRAME
            : DESKTOP_MAP_FLIGHT_GLOBE;
    if (len != NULL) {
        *len = desktop_map_lengths[slot];
    }
    if (revision != NULL) {
        *revision = desktop_map_revisions[slot];
    }
    return desktop_maps[slot];
}

const uint8_t *app_remote_get_route_flag(
    int endpoint_index,
    size_t *len,
    uint32_t *revision)
{
    if (endpoint_index < 0 || endpoint_index > 1) {
        if (len != NULL) {
            *len = 0;
        }
        if (revision != NULL) {
            *revision = 0;
        }
        return NULL;
    }
    if (len != NULL) {
        *len = desktop_route_flag_lengths[endpoint_index];
    }
    if (revision != NULL) {
        *revision = desktop_route_flag_revisions[endpoint_index];
    }
    return desktop_route_flags[endpoint_index];
}

const uint8_t *app_remote_get_weather_city_icon(
    const char *location_key,
    size_t *len,
    uint32_t *revision)
{
    const char *key =
        location_key != NULL && location_key[0] != '\0'
            ? location_key
            : "SJC";
    if (desktop_weather_city_icon == NULL ||
        strcasecmp(key, desktop_weather_city_icon_key) != 0) {
        free(desktop_weather_city_icon);
        desktop_weather_city_icon = NULL;
        desktop_weather_city_icon_key[0] = '\0';
        if (desktop_asset_root[0] != '\0') {
            char path[1280];
            snprintf(
                path,
                sizeof(path),
                "%s/weather_city_icons/%s.rga",
                desktop_asset_root,
                key);
            FILE *file = fopen(path, "rb");
            const size_t expected = 128U * 128U * 3U;
            if (file != NULL) {
                desktop_weather_city_icon = malloc(expected);
                if (desktop_weather_city_icon != NULL &&
                    fread(
                        desktop_weather_city_icon,
                        1,
                        expected,
                        file) == expected) {
                    desktop_copy(
                        desktop_weather_city_icon_key,
                        sizeof(desktop_weather_city_icon_key),
                        key);
                    desktop_weather_city_icon_revision++;
                } else {
                    free(desktop_weather_city_icon);
                    desktop_weather_city_icon = NULL;
                }
                fclose(file);
            }
        }
    }
    if (len != NULL) {
        *len = desktop_weather_city_icon != NULL
            ? 128U * 128U * 3U
            : 0;
    }
    if (revision != NULL) {
        *revision = desktop_weather_city_icon_revision;
    }
    return desktop_weather_city_icon;
}

static desktop_space_asset_t *desktop_space_asset(
    const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (size_t index = 0;
         index < sizeof(desktop_space_assets) /
            sizeof(desktop_space_assets[0]);
         index++) {
        if (strcasecmp(
                name,
                desktop_space_assets[index].name) == 0) {
            return &desktop_space_assets[index];
        }
    }
    return NULL;
}

const uint8_t *app_remote_get_space_asset(
    const char *name,
    size_t *len,
    uint16_t *width,
    uint16_t *height,
    bool *has_alpha,
    uint32_t *revision)
{
    desktop_space_asset_t *asset =
        desktop_space_asset(name);
    if (asset == NULL) {
        return NULL;
    }
    if (asset->bytes == NULL &&
        desktop_asset_root[0] != '\0') {
        char path[1280];
        snprintf(
            path,
            sizeof(path),
            "%s/space/%s.%s",
            desktop_asset_root,
            asset->name,
            asset->alpha ? "rga" : "rgb");
        FILE *file = fopen(path, "rb");
        if (file != NULL) {
            fseek(file, 0, SEEK_END);
            const long file_size = ftell(file);
            rewind(file);
            const size_t expected =
                (size_t)asset->width *
                asset->height *
                (asset->alpha ? 3U : 2U);
            if (file_size == (long)expected) {
                asset->bytes = malloc(expected);
                if (asset->bytes != NULL &&
                    fread(
                        asset->bytes,
                        1,
                        expected,
                        file) == expected) {
                    asset->length = expected;
                } else {
                    free(asset->bytes);
                    asset->bytes = NULL;
                }
            }
            fclose(file);
        }
    }
    if (len != NULL) {
        *len = asset->length;
    }
    if (width != NULL) {
        *width = asset->width;
    }
    if (height != NULL) {
        *height = asset->height;
    }
    if (has_alpha != NULL) {
        *has_alpha = asset->alpha;
    }
    if (revision != NULL) {
        *revision = 1;
    }
    return asset->bytes;
}

void app_remote_release_space_assets_except(
    const char *const *keep_names,
    size_t keep_count)
{
    for (size_t index = 0;
         index < sizeof(desktop_space_assets) /
             sizeof(desktop_space_assets[0]);
         index++) {
        desktop_space_asset_t *asset =
            &desktop_space_assets[index];
        bool keep = false;
        for (size_t keep_index = 0;
             keep_index < keep_count;
             keep_index++) {
            if (keep_names != NULL &&
                keep_names[keep_index] != NULL &&
                strcasecmp(
                    asset->name,
                    keep_names[keep_index]) == 0) {
                keep = true;
                break;
            }
        }
        if (!keep) {
            free(asset->bytes);
            asset->bytes = NULL;
            asset->length = 0;
        }
    }
}

const uint8_t *app_remote_get_radar_map(
    size_t *len,
    uint32_t *revision)
{
    return app_remote_get_radar_map_for_range(
        20.0f,
        len,
        revision,
        NULL);
}

const uint8_t *app_remote_get_radar_map_for_range(
    float miles,
    size_t *len,
    uint32_t *revision,
    int *range_index)
{
    if (range_index != NULL) {
        *range_index =
            app_remote_radar_range_index_for_miles(miles);
    }
    if (len != NULL) {
        *len = desktop_map_lengths[DESKTOP_MAP_RADAR];
    }
    if (revision != NULL) {
        *revision =
            desktop_map_revisions[DESKTOP_MAP_RADAR];
    }
    return desktop_maps[DESKTOP_MAP_RADAR];
}

const uint8_t *app_remote_get_radar_map_for_location(
    const char *location_key,
    float miles,
    size_t *len,
    uint32_t *revision,
    int *range_index)
{
    (void)location_key;
    return app_remote_get_radar_map_for_range(
        miles,
        len,
        revision,
        range_index);
}

int app_remote_radar_range_index_for_miles(float miles)
{
    if (miles < 15.0f) {
        return 0;
    }
    if (miles < 35.0f) {
        return 1;
    }
    return 2;
}

float app_remote_radar_range_miles_at(int index)
{
    static const float ranges[] = {10.0f, 20.0f, 50.0f};
    return index >= 0 && index < 3 ? ranges[index] : 20.0f;
}

const char *app_remote_style_text(app_remote_style_t style)
{
    static const char *names[] = {
        "RADAR", "FLIGHT", "GMT", "WEATHER",
        "WEATHER RADAR", "SPACE", "ISS", "MOON",
        "LAUNCH", "LAUNCH MAP", "LAUNCH QR",
    };
    return style >= 0 &&
        style < (int)(sizeof(names) / sizeof(names[0]))
        ? names[style]
        : "RADAR";
}

void airspace_renderer_set_accent(int accent)
{
    desktop_initialize_fixture();
    if (accent >= 0 && accent < APP_ACCENT_COUNT) {
        desktop_config.accent_color =
            (app_accent_color_t)accent;
    }
}

void airspace_renderer_set_weather_units(int units)
{
    desktop_initialize_fixture();
    desktop_config.weather_units =
        units == APP_WEATHER_UNITS_FAHRENHEIT
            ? APP_WEATHER_UNITS_FAHRENHEIT
            : APP_WEATHER_UNITS_CELSIUS;
}

void airspace_renderer_set_location(const char *key)
{
    desktop_initialize_fixture();
    if (key != NULL) {
        app_location_apply_preset(
            &desktop_config,
            key);
    }
}

void airspace_renderer_set_clock_cities(const char *keys)
{
    desktop_initialize_fixture();
    if (keys == NULL || keys[0] == '\0') {
        return;
    }
    desktop_copy(
        desktop_config.clock_city_keys,
        sizeof(desktop_config.clock_city_keys),
        keys);
}

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
    const char *sunset)
{
    desktop_initialize_fixture();
    desktop_data.weather_valid = valid;
    desktop_data.weather_air_quality_valid = air_quality_valid;
    desktop_data.fetched_unix = fetched_unix;
    desktop_data.weather_temperature = temperature;
    desktop_data.weather_apparent_temperature = apparent_temperature;
    desktop_data.weather_code = code;
    desktop_data.weather_humidity_percent = humidity_percent;
    desktop_data.weather_pressure_hpa = pressure_hpa;
    desktop_data.weather_precipitation_tenths_mm =
        precipitation_tenths_mm;
    desktop_data.weather_uv_tenths = uv_tenths;
    desktop_data.weather_aqi = aqi;
    desktop_data.weather_wind_kt = wind_kt;
    desktop_data.weather_wind_deg = wind_degrees;
    desktop_copy(
        desktop_data.weather_location_key,
        sizeof(desktop_data.weather_location_key),
        location_key);
    desktop_copy(
        desktop_data.weather_unit,
        sizeof(desktop_data.weather_unit),
        unit);
    desktop_copy(
        desktop_data.weather_summary,
        sizeof(desktop_data.weather_summary),
        summary);
    desktop_copy(
        desktop_data.weather_wind,
        sizeof(desktop_data.weather_wind),
        wind);
    desktop_copy(
        desktop_data.weather_sunrise,
        sizeof(desktop_data.weather_sunrise),
        sunrise);
    desktop_copy(
        desktop_data.weather_sunset,
        sizeof(desktop_data.weather_sunset),
        sunset);
}

void airspace_renderer_set_weather_forecast(
    int index,
    const char *day,
    int code,
    int high,
    int low,
    int precipitation_percent)
{
    desktop_initialize_fixture();
    if (index < 0 || index >= APP_WEATHER_FORECAST_DAYS) {
        return;
    }
    desktop_copy(
        desktop_data.forecast_day[index],
        sizeof(desktop_data.forecast_day[index]),
        day);
    desktop_data.forecast_code[index] = code;
    desktop_data.forecast_high[index] = high;
    desktop_data.forecast_low[index] = low;
    desktop_data.forecast_precip_percent[index] =
        precipitation_percent;
}

void airspace_renderer_begin_aircraft(
    bool valid,
    int count,
    int raw_count,
    int usable_count,
    int http_status,
    float radius_miles,
    bool auth_used,
    int fetched_unix)
{
    desktop_initialize_fixture();
    memset(desktop_data.aircraft, 0, sizeof(desktop_data.aircraft));
    desktop_data.aircraft_valid = valid;
    desktop_data.aircraft_count =
        count < 0 ? 0 : (count > APP_MAX_AIRCRAFT ? APP_MAX_AIRCRAFT : count);
    desktop_data.aircraft_raw_count = raw_count;
    desktop_data.aircraft_usable_count = usable_count;
    desktop_data.aircraft_http_status = http_status;
    desktop_data.aircraft_radius_miles = radius_miles;
    desktop_data.aircraft_auth_used = auth_used;
    desktop_data.fetched_unix = fetched_unix;
}

static app_aircraft_type_source_t desktop_aircraft_type_source(
    const char *source)
{
    if (source == NULL) {
        return APP_AIRCRAFT_TYPE_SOURCE_NONE;
    }
    if (strcasecmp(source, "adsbdb") == 0) {
        return APP_AIRCRAFT_TYPE_SOURCE_ADSBDB;
    }
    if (strcasecmp(source, "airlabs") == 0) {
        return APP_AIRCRAFT_TYPE_SOURCE_AIRLABS;
    }
    if (strcasecmp(source, "flightaware") == 0) {
        return APP_AIRCRAFT_TYPE_SOURCE_FLIGHTAWARE;
    }
    return APP_AIRCRAFT_TYPE_SOURCE_NONE;
}

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
    const char *airline_icao)
{
    desktop_initialize_fixture();
    if (index < 0 || index >= APP_MAX_AIRCRAFT) {
        return;
    }
    app_aircraft_t *aircraft = &desktop_data.aircraft[index];
    aircraft->valid = true;
    desktop_copy(aircraft->icao24, sizeof(aircraft->icao24), icao24);
    desktop_copy(aircraft->callsign, sizeof(aircraft->callsign), callsign);
    desktop_copy(aircraft->typecode, sizeof(aircraft->typecode), typecode);
    desktop_copy(aircraft->model, sizeof(aircraft->model), model);
    desktop_copy(
        aircraft->registration,
        sizeof(aircraft->registration),
        registration);
    desktop_copy(
        aircraft->manufacturer,
        sizeof(aircraft->manufacturer),
        manufacturer);
    desktop_copy(
        aircraft->engine_type,
        sizeof(aircraft->engine_type),
        engine_type);
    aircraft->engine_count = engine_count;
    aircraft->type_source = desktop_aircraft_type_source(type_source);
    desktop_copy(
        aircraft->airline_name,
        sizeof(aircraft->airline_name),
        airline_name);
    desktop_copy(
        aircraft->airline_iata,
        sizeof(aircraft->airline_iata),
        airline_iata);
    desktop_copy(
        aircraft->airline_icao,
        sizeof(aircraft->airline_icao),
        airline_icao);
}

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
    float route_distance_remaining_nm)
{
    desktop_initialize_fixture();
    if (index < 0 || index >= APP_MAX_AIRCRAFT) {
        return;
    }
    app_aircraft_t *aircraft = &desktop_data.aircraft[index];
    aircraft->metadata_valid = metadata_valid;
    aircraft->route_valid = route_valid;
    aircraft->route_progress_valid = route_progress_valid;
    aircraft->route_progress = route_progress;
    aircraft->route_distance_nm = route_distance_nm;
    aircraft->route_distance_covered_nm = route_distance_covered_nm;
    aircraft->route_distance_remaining_nm = route_distance_remaining_nm;
    desktop_copy(
        aircraft->origin_iata,
        sizeof(aircraft->origin_iata),
        origin);
    desktop_copy(
        aircraft->destination_iata,
        sizeof(aircraft->destination_iata),
        destination);
}

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
    int bearing_deg)
{
    desktop_initialize_fixture();
    if (index < 0 || index >= APP_MAX_AIRCRAFT) {
        return;
    }
    app_aircraft_t *aircraft = &desktop_data.aircraft[index];
    aircraft->latitude = latitude;
    aircraft->longitude = longitude;
    aircraft->altitude_ft = altitude_ft;
    aircraft->speed_kt = speed_kt;
    aircraft->track_deg = track_deg;
    aircraft->track_valid = track_valid;
    aircraft->vertical_rate_mps = vertical_rate_mps;
    aircraft->vertical_rate_valid = vertical_rate_valid;
    aircraft->position_unix = position_unix;
    aircraft->contact_unix = contact_unix;
    aircraft->category = category;
    aircraft->distance_nm = distance_nm;
    aircraft->bearing_deg = bearing_deg;
}

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
    int heading_deg)
{
    desktop_initialize_fixture();
    desktop_remote.tracked_flight_valid = true;
    desktop_copy(
        desktop_remote.tracked_flight_number,
        sizeof(desktop_remote.tracked_flight_number),
        number);
    desktop_copy(
        desktop_remote.tracked_flight_callsign,
        sizeof(desktop_remote.tracked_flight_callsign),
        callsign);
    desktop_copy(
        desktop_remote.tracked_flight_airline_iata,
        sizeof(desktop_remote.tracked_flight_airline_iata),
        airline_iata);
    desktop_copy(
        desktop_remote.tracked_flight_airline_name,
        sizeof(desktop_remote.tracked_flight_airline_name),
        airline_name);
    desktop_copy(
        desktop_remote.tracked_flight_aircraft_type,
        sizeof(desktop_remote.tracked_flight_aircraft_type),
        aircraft_type);
    desktop_copy(
        desktop_remote.tracked_flight_origin,
        sizeof(desktop_remote.tracked_flight_origin),
        origin);
    desktop_copy(
        desktop_remote.tracked_flight_origin_city,
        sizeof(desktop_remote.tracked_flight_origin_city),
        origin_city);
    desktop_copy(
        desktop_remote.tracked_flight_destination,
        sizeof(desktop_remote.tracked_flight_destination),
        destination);
    desktop_copy(
        desktop_remote.tracked_flight_destination_city,
        sizeof(desktop_remote.tracked_flight_destination_city),
        destination_city);
    desktop_copy(
        desktop_remote.tracked_flight_origin_terminal,
        sizeof(desktop_remote.tracked_flight_origin_terminal),
        origin_terminal);
    desktop_copy(
        desktop_remote.tracked_flight_origin_gate,
        sizeof(desktop_remote.tracked_flight_origin_gate),
        origin_gate);
    desktop_copy(
        desktop_remote.tracked_flight_destination_terminal,
        sizeof(desktop_remote.tracked_flight_destination_terminal),
        destination_terminal);
    desktop_copy(
        desktop_remote.tracked_flight_destination_gate,
        sizeof(desktop_remote.tracked_flight_destination_gate),
        destination_gate);
    desktop_remote.route_progress =
        fminf(1.0f, fmaxf(0.0f, progress));
    desktop_remote.tracked_flight_altitude_m = altitude_m;
    desktop_remote.tracked_flight_speed_kph = speed_kph;
    desktop_remote.tracked_flight_heading_deg = heading_deg;
    desktop_remote.tracked_flight_heading_valid = heading_deg >= 0;
    desktop_remote.revision++;
}

void airspace_renderer_set_route_points(
    const int16_t *xy,
    int count)
{
    desktop_initialize_fixture();
    if (xy == NULL || count < 2) {
        desktop_remote.tracked_route_point_count = 0;
        desktop_remote.revision++;
        return;
    }

    if (count > APP_REMOTE_TRACKED_ROUTE_MAX_POINTS) {
        count = APP_REMOTE_TRACKED_ROUTE_MAX_POINTS;
    }
    desktop_remote.tracked_route_point_count = (uint8_t)count;
    for (int index = 0; index < count; index++) {
        desktop_remote.tracked_route_points[index][0] =
            xy[index * 2];
        desktop_remote.tracked_route_points[index][1] =
            xy[index * 2 + 1];
    }
    desktop_remote.revision++;
}

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
    int64_t actual_arrival_unix)
{
    desktop_initialize_fixture();
    desktop_copy(
        desktop_remote.tracked_flight_status,
        sizeof(desktop_remote.tracked_flight_status),
        status);
    desktop_copy(
        desktop_remote.tracked_flight_phase,
        sizeof(desktop_remote.tracked_flight_phase),
        vertical_phase);
    desktop_copy(
        desktop_remote.tracked_flight_journey_phase,
        sizeof(desktop_remote.tracked_flight_journey_phase),
        journey_phase);
    desktop_copy(
        desktop_remote.tracked_flight_duration,
        sizeof(desktop_remote.tracked_flight_duration),
        duration);
    desktop_copy(
        desktop_remote.tracked_flight_elapsed,
        sizeof(desktop_remote.tracked_flight_elapsed),
        elapsed);
    desktop_copy(
        desktop_remote.tracked_flight_remaining,
        sizeof(desktop_remote.tracked_flight_remaining),
        remaining);
    desktop_copy(
        desktop_remote.tracked_flight_origin_country,
        sizeof(desktop_remote.tracked_flight_origin_country),
        origin_country);
    desktop_copy(
        desktop_remote.tracked_flight_destination_country,
        sizeof(desktop_remote.tracked_flight_destination_country),
        destination_country);
    desktop_remote.tracked_flight_delayed = delayed;
    desktop_remote.tracked_flight_route_distance_km =
        route_distance_km;
    desktop_remote
        .tracked_flight_origin_current_utc_offset_s =
        origin_current_utc_offset_seconds;
    desktop_remote
        .tracked_flight_destination_current_utc_offset_s =
        destination_current_utc_offset_seconds;
    desktop_remote.tracked_flight_departure_utc_offset_s =
        departure_utc_offset_seconds;
    desktop_remote.tracked_flight_arrival_utc_offset_s =
        arrival_utc_offset_seconds;
    desktop_remote.tracked_flight_estimated_departure_unix =
        estimated_departure_unix;
    desktop_remote.tracked_flight_actual_departure_unix =
        actual_departure_unix;
    desktop_remote.tracked_flight_estimated_arrival_unix =
        estimated_arrival_unix;
    desktop_remote.tracked_flight_actual_arrival_unix =
        actual_arrival_unix;
    desktop_remote.revision++;
}

void airspace_renderer_set_route_flag(
    int endpoint,
    const uint8_t *bytes,
    size_t length)
{
    desktop_initialize_fixture();
    if (endpoint < 0 || endpoint > 1) {
        return;
    }
    free(desktop_route_flags[endpoint]);
    desktop_route_flags[endpoint] = NULL;
    desktop_route_flag_lengths[endpoint] = 0;
    if (bytes != NULL &&
        length == APP_REMOTE_ROUTE_FLAG_BYTES) {
        desktop_route_flags[endpoint] = malloc(length);
        if (desktop_route_flags[endpoint] != NULL) {
            memcpy(
                desktop_route_flags[endpoint],
                bytes,
                length);
            desktop_route_flag_lengths[endpoint] = length;
        }
    }
    desktop_route_flag_revisions[endpoint]++;
    desktop_remote.route_flag_revision++;
    desktop_remote.route_flag_valid_mask = 0;
    for (int index = 0; index < 2; index++) {
        if (desktop_route_flag_lengths[index] ==
            APP_REMOTE_ROUTE_FLAG_BYTES) {
            desktop_remote.route_flag_valid_mask |=
                (1UL << index);
        }
    }
}

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
    int weather_probability_percent)
{
    desktop_initialize_fixture();
    desktop_remote.launch_valid =
        identifier != NULL &&
        identifier[0] != '\0';
    desktop_copy(
        desktop_remote.launch_id,
        sizeof(desktop_remote.launch_id),
        identifier);
    desktop_copy(
        desktop_remote.launch_mission,
        sizeof(desktop_remote.launch_mission),
        mission);
    desktop_copy(
        desktop_remote.launch_full_name,
        sizeof(desktop_remote.launch_full_name),
        full_name);
    desktop_copy(
        desktop_remote.launch_agency,
        sizeof(desktop_remote.launch_agency),
        agency);
    desktop_copy(
        desktop_remote.launch_vehicle,
        sizeof(desktop_remote.launch_vehicle),
        vehicle);
    desktop_copy(
        desktop_remote.launch_site,
        sizeof(desktop_remote.launch_site),
        site);
    desktop_copy(
        desktop_remote.launch_pad,
        sizeof(desktop_remote.launch_pad),
        pad);
    desktop_copy(
        desktop_remote.launch_location,
        sizeof(desktop_remote.launch_location),
        location);
    desktop_copy(
        desktop_remote.launch_precision,
        sizeof(desktop_remote.launch_precision),
        precision);
    desktop_copy(
        desktop_remote.launch_precision_abbrev,
        sizeof(desktop_remote.launch_precision_abbrev),
        precision_abbreviation);
    desktop_copy(
        desktop_remote.launch_status,
        sizeof(desktop_remote.launch_status),
        status);
    desktop_copy(
        desktop_remote.launch_status_abbrev,
        sizeof(desktop_remote.launch_status_abbrev),
        status_abbreviation);
    desktop_copy(
        desktop_remote.launch_watch_url,
        sizeof(desktop_remote.launch_watch_url),
        watch_url);
    desktop_remote.launch_net_unix = net_unix;
    desktop_remote.launch_updated_unix =
        updated_unix;
    desktop_remote.launch_current_utc_offset_s =
        current_utc_offset_seconds;
    desktop_remote
        .launch_weather_probability_percent =
        weather_probability_percent;
    desktop_remote.revision++;
}

void airspace_renderer_set_map(
    int slot,
    const uint8_t *bytes,
    size_t length)
{
    if (slot < 0 || slot >= DESKTOP_MAP_COUNT) {
        return;
    }
    free(desktop_maps[slot]);
    desktop_maps[slot] = NULL;
    desktop_map_lengths[slot] = 0;
    if (bytes != NULL && length > 0) {
        desktop_maps[slot] = malloc(length);
        if (desktop_maps[slot] != NULL) {
            memcpy(desktop_maps[slot], bytes, length);
            desktop_map_lengths[slot] = length;
        }
    }
    desktop_map_revisions[slot]++;
    if (slot == DESKTOP_MAP_RADAR) {
        desktop_remote.radar_map_revision++;
        desktop_remote.radar_map_valid =
            desktop_maps[slot] != NULL;
        desktop_remote.radar_map_bytes =
            desktop_map_lengths[slot];
    } else {
        desktop_remote.map_revision++;
        desktop_remote.map_valid =
            desktop_maps[slot] != NULL;
        desktop_remote.map_bytes =
            desktop_map_lengths[slot];
    }
}

void airspace_renderer_set_airline_logo(
    const char *iata,
    const uint8_t *bytes,
    size_t length,
    int width,
    int height)
{
    free(desktop_airline_logo);
    desktop_airline_logo = NULL;
    desktop_airline_logo_length = 0;
    desktop_airline_logo_width = 0;
    desktop_airline_logo_height = 0;
    desktop_copy(
        desktop_airline_logo_iata,
        sizeof(desktop_airline_logo_iata),
        iata);

    const size_t expected =
        width > 0 && height > 0
            ? (size_t)width * (size_t)height * 3U
            : 0;
    if (bytes != NULL &&
        length == expected &&
        width <= UINT16_MAX &&
        height <= UINT16_MAX) {
        desktop_airline_logo = malloc(length);
        if (desktop_airline_logo != NULL) {
            memcpy(desktop_airline_logo, bytes, length);
            desktop_airline_logo_length = length;
            desktop_airline_logo_width = (uint16_t)width;
            desktop_airline_logo_height = (uint16_t)height;
        }
    }
    desktop_airline_logo_revision++;
}
