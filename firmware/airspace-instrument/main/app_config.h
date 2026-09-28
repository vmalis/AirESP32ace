// AirESP32ace
// Created by VM, August 2026

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_CLOCK_CITY_COUNT 5
#define APP_RADAR_CITY_COUNT 8
#define APP_DOCK_ENTRY_MIN 3
#define APP_DOCK_ENTRY_MAX 10
#define APP_MAIN_SCREEN_COUNT 6

typedef enum {
    APP_WIFI_PROFILE_HOME = 0,
    APP_WIFI_PROFILE_HOTSPOT = 1,
    APP_WIFI_PROFILE_COUNT,
} app_wifi_profile_id_t;

typedef enum {
    APP_ACCENT_GREEN = 0,
    APP_ACCENT_CYAN,
    APP_ACCENT_ORANGE,
    APP_ACCENT_RED,
    APP_ACCENT_COUNT,
} app_accent_color_t;

typedef enum {
    APP_WEATHER_UNITS_CELSIUS = 0,
    APP_WEATHER_UNITS_FAHRENHEIT,
    APP_WEATHER_UNITS_COUNT,
} app_weather_units_t;

typedef enum {
    APP_DOCK_RADAR = 0,
    APP_DOCK_TRACKER,
    APP_DOCK_WEATHER,
    APP_DOCK_CLOCK,
    APP_DOCK_SOLAR,
    APP_DOCK_ECLIPSE,
    APP_DOCK_MOON,
    APP_DOCK_ISS,
    APP_DOCK_LAUNCH_NASA,
    APP_DOCK_LAUNCH_ROSCOSMOS,
    APP_DOCK_LAUNCH_SPACEX,
    APP_DOCK_TRACKER_FRAME,
    APP_DOCK_TRACKER_DETAIL,
    APP_DOCK_WEATHER_FORECAST,
    APP_DOCK_WEATHER_RADAR,
    APP_DOCK_CLOCK_WORLD,
    APP_DOCK_LAUNCH_NASA_MAP,
    APP_DOCK_LAUNCH_ROSCOSMOS_MAP,
    APP_DOCK_LAUNCH_SPACEX_MAP,
    APP_DOCK_ITEM_COUNT,
} app_dock_item_t;

typedef enum {
    APP_MAIN_RADAR = 0,
    APP_MAIN_TRACKER,
    APP_MAIN_WEATHER,
    APP_MAIN_CLOCK,
    APP_MAIN_SPACE,
    APP_MAIN_LAUNCHES,
} app_main_screen_t;

typedef struct {
    uint8_t item;
    uint16_t duration_seconds;
} app_dock_entry_t;

typedef struct {
    char ssid[33];
    char password[65];
} app_wifi_profile_t;

typedef struct {
    uint32_t revision;
    app_wifi_profile_id_t active_wifi_profile;
    app_wifi_profile_t wifi_profiles[APP_WIFI_PROFILE_COUNT];
    char location_key[16];
    char location_label[48];
    char location_lat[20];
    char location_lon[20];
    char timezone[64];
    char metar_station[8];
    char radar_half_degrees[20];
    bool aircraft_labels_enabled;
    bool radar_wedge_enabled;
    bool iss_flags_enabled;
    app_weather_units_t weather_units;
    app_accent_color_t accent_color;
    uint32_t accent_cyan_rgb;
    uint32_t accent_orange_rgb;
    uint32_t accent_red_rgb;
    uint8_t display_brightness_percent;
    uint16_t radar_range_miles;
    char radar_city_keys[72];
    char clock_city_keys[48];
    /* Companion-defined locations, one pipe-delimited record per line. */
    char custom_location_data[1024];
    uint8_t dock_entry_count;
    app_dock_entry_t dock_entries[APP_DOCK_ENTRY_MAX];
    uint8_t main_screen_count;
    uint8_t main_screens[APP_MAIN_SCREEN_COUNT];
    bool opensky_enabled;
    char opensky_client_id[96];
    char opensky_client_secret[96];
    char airlabs_api_key[96];
    bool flightaware_available;
    char flightaware_api_key[128];
    uint8_t launch_agency_mask; /* NASA=1, Roscosmos=2, SpaceX=4; zero migrates legacy preferences. */
} app_config_t;

void app_config_init_defaults(void);
esp_err_t app_config_load(void);
esp_err_t app_config_save(const app_config_t *config);
void app_config_get(app_config_t *out);
const app_wifi_profile_t *app_config_active_wifi_profile(const app_config_t *config);
const char *app_config_wifi_profile_name(app_wifi_profile_id_t profile);
bool app_config_parse_wifi_profile_name(const char *name, app_wifi_profile_id_t *profile);
const char *app_config_accent_color_name(app_accent_color_t color);
bool app_config_parse_accent_color_name(const char *name, app_accent_color_t *color);
