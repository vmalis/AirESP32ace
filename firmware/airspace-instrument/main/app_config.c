// AirESP32ace
// Created by VM, August 2026

#include "app_config.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "locations.h"

static const char *NVS_NAMESPACE = "aircfg";

static app_config_t current_config;
static SemaphoreHandle_t config_mutex;
static bool config_initialized;

static const app_dock_entry_t DEFAULT_DOCK_ENTRIES[] = {
    {APP_DOCK_RADAR, 20},
    {APP_DOCK_TRACKER, 20},
    {APP_DOCK_WEATHER, 10},
    {APP_DOCK_CLOCK, 20},
    {APP_DOCK_ECLIPSE, 10},
    {APP_DOCK_MOON, 5},
    {APP_DOCK_ISS, 20},
    {APP_DOCK_LAUNCH_NASA, 20},
    {APP_DOCK_LAUNCH_ROSCOSMOS, 20},
    {APP_DOCK_LAUNCH_SPACEX, 20},
};

static bool dock_entries_valid(const app_config_t *config)
{
    if (config->dock_entry_count < APP_DOCK_ENTRY_MIN ||
        config->dock_entry_count > APP_DOCK_ENTRY_MAX) {
        return false;
    }
    uint32_t seen = 0;
    for (uint8_t index = 0;
         index < config->dock_entry_count;
         index++) {
        const app_dock_entry_t *entry =
            &config->dock_entries[index];
        if (entry->item >= APP_DOCK_ITEM_COUNT ||
            entry->duration_seconds < 5 ||
            entry->duration_seconds > 300 ||
            (seen & (1u << entry->item)) != 0) {
            return false;
        }
        seen |= 1u << entry->item;
    }
    return true;
}

static void set_default_dock_entries(app_config_t *config)
{
    config->dock_entry_count =
        sizeof(DEFAULT_DOCK_ENTRIES) /
        sizeof(DEFAULT_DOCK_ENTRIES[0]);
    memcpy(
        config->dock_entries,
        DEFAULT_DOCK_ENTRIES,
        sizeof(DEFAULT_DOCK_ENTRIES));
}

static void set_default_main_screens(app_config_t *config)
{
    config->main_screen_count = APP_MAIN_SCREEN_COUNT;
    for (uint8_t index = 0; index < APP_MAIN_SCREEN_COUNT; index++) {
        config->main_screens[index] = index;
    }
}

static bool main_screens_valid(const app_config_t *config)
{
    if (config->main_screen_count < 1 ||
        config->main_screen_count > APP_MAIN_SCREEN_COUNT) {
        return false;
    }
    uint8_t seen = 0;
    for (uint8_t index = 0; index < config->main_screen_count; index++) {
        const uint8_t screen = config->main_screens[index];
        if (screen >= APP_MAIN_SCREEN_COUNT ||
            (seen & (1u << screen)) != 0) {
            return false;
        }
        seen |= 1u << screen;
    }
    return true;
}

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void lock_config(void)
{
    if (config_mutex != NULL) {
        xSemaphoreTake(config_mutex, portMAX_DELAY);
    }
}

static void unlock_config(void)
{
    if (config_mutex != NULL) {
        xSemaphoreGive(config_mutex);
    }
}

void app_config_init_defaults(void)
{
    if (config_mutex == NULL) {
        config_mutex = xSemaphoreCreateMutex();
    }
    if (config_initialized) {
        return;
    }

    app_config_t defaults = {0};
    defaults.revision = 0;
    defaults.active_wifi_profile = APP_WIFI_PROFILE_HOME;
    copy_string(defaults.wifi_profiles[APP_WIFI_PROFILE_HOME].ssid,
                sizeof(defaults.wifi_profiles[APP_WIFI_PROFILE_HOME].ssid),
                CONFIG_AIRSPACE_WIFI_SSID);
    copy_string(defaults.wifi_profiles[APP_WIFI_PROFILE_HOME].password,
                sizeof(defaults.wifi_profiles[APP_WIFI_PROFILE_HOME].password),
                CONFIG_AIRSPACE_WIFI_PASSWORD);
    copy_string(defaults.location_key, sizeof(defaults.location_key), "SJC");
    copy_string(defaults.location_label, sizeof(defaults.location_label), CONFIG_AIRSPACE_LOCATION_LABEL);
    copy_string(defaults.location_lat, sizeof(defaults.location_lat), CONFIG_AIRSPACE_LOCATION_LAT);
    copy_string(defaults.location_lon, sizeof(defaults.location_lon), CONFIG_AIRSPACE_LOCATION_LON);
    copy_string(defaults.timezone, sizeof(defaults.timezone), CONFIG_AIRSPACE_TZ_POSIX);
    copy_string(defaults.metar_station, sizeof(defaults.metar_station), CONFIG_AIRSPACE_METAR_STATION);
    copy_string(defaults.radar_half_degrees, sizeof(defaults.radar_half_degrees), CONFIG_AIRSPACE_RADAR_HALF_DEGREES);
    defaults.aircraft_labels_enabled = false;
    defaults.radar_wedge_enabled = true;
    defaults.iss_flags_enabled = false;
    defaults.weather_units = APP_WEATHER_UNITS_CELSIUS;
    defaults.accent_color = APP_ACCENT_GREEN;
    defaults.accent_cyan_rgb = 0x00A3BF;
    defaults.accent_orange_rgb = 0xD47A17;
    defaults.accent_red_rgb = 0xBC252F;
    defaults.display_brightness_percent = 100;
    defaults.radar_range_miles = 20;
    copy_string(
        defaults.radar_city_keys,
        sizeof(defaults.radar_city_keys),
        "SJC,SFO,SAN,JFK,MOW,LED,DUS,ICN");
    copy_string(
        defaults.clock_city_keys,
        sizeof(defaults.clock_city_keys),
        "ICN,MOW,DUS,JFK,SJC");
    set_default_dock_entries(&defaults);
    set_default_main_screens(&defaults);
    defaults.opensky_enabled = CONFIG_AIRSPACE_OPENSKY_ENABLE;

    lock_config();
    current_config = defaults;
    config_initialized = true;
    unlock_config();
    app_location_set_custom_data(defaults.custom_location_data);
}

static void load_string(nvs_handle_t nvs, const char *key, char *dst, size_t dst_size)
{
    size_t len = dst_size;
    esp_err_t err = nvs_get_str(nvs, key, dst, &len);
    if (err != ESP_OK && dst_size > 0) {
        dst[dst_size - 1] = '\0';
    }
}

esp_err_t app_config_load(void)
{
    app_config_t loaded;
    app_config_get(&loaded);

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    char active[16] = {0};
    load_string(nvs, "active", active, sizeof(active));
    app_wifi_profile_id_t active_profile;
    if (app_config_parse_wifi_profile_name(active, &active_profile)) {
        loaded.active_wifi_profile = active_profile;
    }

    load_string(nvs, "home_ssid", loaded.wifi_profiles[APP_WIFI_PROFILE_HOME].ssid,
                sizeof(loaded.wifi_profiles[APP_WIFI_PROFILE_HOME].ssid));
    load_string(nvs, "home_pass", loaded.wifi_profiles[APP_WIFI_PROFILE_HOME].password,
                sizeof(loaded.wifi_profiles[APP_WIFI_PROFILE_HOME].password));
    load_string(nvs, "hot_ssid", loaded.wifi_profiles[APP_WIFI_PROFILE_HOTSPOT].ssid,
                sizeof(loaded.wifi_profiles[APP_WIFI_PROFILE_HOTSPOT].ssid));
    load_string(nvs, "hot_pass", loaded.wifi_profiles[APP_WIFI_PROFILE_HOTSPOT].password,
                sizeof(loaded.wifi_profiles[APP_WIFI_PROFILE_HOTSPOT].password));
    load_string(nvs, "loc_key", loaded.location_key, sizeof(loaded.location_key));
    load_string(nvs, "loc_label", loaded.location_label, sizeof(loaded.location_label));
    load_string(nvs, "loc_lat", loaded.location_lat, sizeof(loaded.location_lat));
    load_string(nvs, "loc_lon", loaded.location_lon, sizeof(loaded.location_lon));
    load_string(nvs, "tz", loaded.timezone, sizeof(loaded.timezone));
    load_string(nvs, "metar", loaded.metar_station, sizeof(loaded.metar_station));
    load_string(nvs, "rad_half", loaded.radar_half_degrees, sizeof(loaded.radar_half_degrees));
    load_string(nvs, "radar_city", loaded.radar_city_keys, sizeof(loaded.radar_city_keys));
    load_string(nvs, "clock_city", loaded.clock_city_keys, sizeof(loaded.clock_city_keys));
    load_string(
        nvs,
        "custom_locs",
        loaded.custom_location_data,
        sizeof(loaded.custom_location_data));
    load_string(nvs, "osky_id", loaded.opensky_client_id, sizeof(loaded.opensky_client_id));
    load_string(nvs, "osky_sec", loaded.opensky_client_secret, sizeof(loaded.opensky_client_secret));
    load_string(nvs, "airlabs", loaded.airlabs_api_key, sizeof(loaded.airlabs_api_key));

    size_t dock_bytes = sizeof(loaded.dock_entries);
    uint8_t dock_count = loaded.dock_entry_count;
    if (nvs_get_u8(nvs, "dock_cnt", &dock_count) == ESP_OK &&
        nvs_get_blob(
            nvs,
            "dock_ent",
            loaded.dock_entries,
            &dock_bytes) == ESP_OK &&
        dock_bytes == sizeof(loaded.dock_entries)) {
        loaded.dock_entry_count = dock_count;
        if (!dock_entries_valid(&loaded)) {
            set_default_dock_entries(&loaded);
        }
    }

    size_t main_screen_bytes = sizeof(loaded.main_screens);
    uint8_t main_screen_count = loaded.main_screen_count;
    if (nvs_get_u8(nvs, "main_cnt", &main_screen_count) == ESP_OK &&
        nvs_get_blob(
            nvs,
            "main_ord",
            loaded.main_screens,
            &main_screen_bytes) == ESP_OK &&
        main_screen_bytes == sizeof(loaded.main_screens)) {
        loaded.main_screen_count = main_screen_count;
        if (!main_screens_valid(&loaded)) {
            set_default_main_screens(&loaded);
        }
    }

    uint32_t revision = loaded.revision;
    if (nvs_get_u32(nvs, "revision", &revision) == ESP_OK) {
        loaded.revision = revision;
    }

    uint16_t radar_range_miles = loaded.radar_range_miles;
    if (nvs_get_u16(nvs, "rad_range", &radar_range_miles) == ESP_OK &&
        (radar_range_miles == 10 || radar_range_miles == 20 || radar_range_miles == 50)) {
        loaded.radar_range_miles = radar_range_miles;
    }

    uint8_t flightaware_available = loaded.flightaware_available ? 1 : 0;
    if (nvs_get_u8(nvs, "fa_avail", &flightaware_available) == ESP_OK) {
        loaded.flightaware_available = flightaware_available != 0;
    }

    uint8_t aircraft_labels_enabled = loaded.aircraft_labels_enabled ? 1 : 0;
    if (nvs_get_u8(nvs, "ac_labels", &aircraft_labels_enabled) == ESP_OK) {
        loaded.aircraft_labels_enabled = aircraft_labels_enabled != 0;
    }

    uint8_t radar_wedge_enabled = loaded.radar_wedge_enabled ? 1 : 0;
    if (nvs_get_u8(nvs, "wedge", &radar_wedge_enabled) == ESP_OK) {
        loaded.radar_wedge_enabled = radar_wedge_enabled != 0;
    }

    uint8_t iss_flags_enabled = loaded.iss_flags_enabled ? 1 : 0;
    if (nvs_get_u8(nvs, "iss_flags", &iss_flags_enabled) == ESP_OK) {
        loaded.iss_flags_enabled = iss_flags_enabled != 0;
    }

    uint8_t weather_units = (uint8_t)loaded.weather_units;
    if (nvs_get_u8(nvs, "wx_units", &weather_units) == ESP_OK &&
        weather_units < APP_WEATHER_UNITS_COUNT) {
        loaded.weather_units = (app_weather_units_t)weather_units;
    }

    uint8_t accent_color = (uint8_t)loaded.accent_color;
    if (nvs_get_u8(nvs, "accent", &accent_color) == ESP_OK &&
        accent_color < APP_ACCENT_COUNT) {
        loaded.accent_color = (app_accent_color_t)accent_color;
    }
    uint32_t accent_rgb = loaded.accent_cyan_rgb;
    if (nvs_get_u32(nvs, "acc_cyan", &accent_rgb) == ESP_OK) {
        loaded.accent_cyan_rgb = accent_rgb & 0xFFFFFF;
    }
    accent_rgb = loaded.accent_orange_rgb;
    if (nvs_get_u32(nvs, "acc_orange", &accent_rgb) == ESP_OK) {
        loaded.accent_orange_rgb = accent_rgb & 0xFFFFFF;
    }
    accent_rgb = loaded.accent_red_rgb;
    if (nvs_get_u32(nvs, "acc_red", &accent_rgb) == ESP_OK) {
        loaded.accent_red_rgb = accent_rgb & 0xFFFFFF;
    }

    uint8_t display_brightness = loaded.display_brightness_percent;
    if (nvs_get_u8(nvs, "bright", &display_brightness) == ESP_OK &&
        display_brightness >= 10 && display_brightness <= 100) {
        loaded.display_brightness_percent = display_brightness;
    }

    uint8_t opensky_enabled = loaded.opensky_enabled ? 1 : 0;
    if (nvs_get_u8(nvs, "osky_en", &opensky_enabled) == ESP_OK) {
        loaded.opensky_enabled = opensky_enabled != 0;
    }
    if (loaded.location_key[0] == '\0') {
        copy_string(loaded.location_key, sizeof(loaded.location_key), "SJC");
    }
    if (loaded.radar_city_keys[0] == '\0') {
        copy_string(
            loaded.radar_city_keys,
            sizeof(loaded.radar_city_keys),
            "SJC,SFO,SAN,JFK,MOW,LED,DUS,ICN");
    }
    if (loaded.clock_city_keys[0] == '\0') {
        copy_string(
            loaded.clock_city_keys,
            sizeof(loaded.clock_city_keys),
            "ICN,MOW,DUS,JFK,SJC");
    }
    if (!dock_entries_valid(&loaded)) {
        set_default_dock_entries(&loaded);
    }
    if (!main_screens_valid(&loaded)) {
        set_default_main_screens(&loaded);
    }

    nvs_close(nvs);

    lock_config();
    current_config = loaded;
    unlock_config();
    app_location_set_custom_data(loaded.custom_location_data);
    return ESP_OK;
}

esp_err_t app_config_save(const app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    app_config_t stored = *config;
    lock_config();
    stored.revision = current_config.revision + 1;
    unlock_config();

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, "active", app_config_wifi_profile_name(stored.active_wifi_profile));
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "home_ssid", stored.wifi_profiles[APP_WIFI_PROFILE_HOME].ssid);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "home_pass", stored.wifi_profiles[APP_WIFI_PROFILE_HOME].password);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "hot_ssid", stored.wifi_profiles[APP_WIFI_PROFILE_HOTSPOT].ssid);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "hot_pass", stored.wifi_profiles[APP_WIFI_PROFILE_HOTSPOT].password);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "loc_key", stored.location_key);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "loc_label", stored.location_label);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "loc_lat", stored.location_lat);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "loc_lon", stored.location_lon);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "tz", stored.timezone);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "metar", stored.metar_station);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "rad_half", stored.radar_half_degrees);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "radar_city", stored.radar_city_keys);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "clock_city", stored.clock_city_keys);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "custom_locs", stored.custom_location_data);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "ac_labels", stored.aircraft_labels_enabled ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "wedge", stored.radar_wedge_enabled ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "iss_flags", stored.iss_flags_enabled ? 1 : 0);
    }
    if (err == ESP_OK) {
        const uint8_t weather_units =
            stored.weather_units >= 0 &&
                    stored.weather_units < APP_WEATHER_UNITS_COUNT
                ? (uint8_t)stored.weather_units
                : (uint8_t)APP_WEATHER_UNITS_CELSIUS;
        err = nvs_set_u8(nvs, "wx_units", weather_units);
    }
    if (err == ESP_OK) {
        const uint8_t accent_color = (stored.accent_color >= 0 && stored.accent_color < APP_ACCENT_COUNT)
            ? (uint8_t)stored.accent_color
            : (uint8_t)APP_ACCENT_GREEN;
        err = nvs_set_u8(nvs, "accent", accent_color);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "acc_cyan", stored.accent_cyan_rgb & 0xFFFFFF);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "acc_orange", stored.accent_orange_rgb & 0xFFFFFF);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "acc_red", stored.accent_red_rgb & 0xFFFFFF);
    }
    if (err == ESP_OK) {
        const uint8_t brightness =
            stored.display_brightness_percent >= 10 &&
                    stored.display_brightness_percent <= 100
                ? stored.display_brightness_percent
                : 100;
        err = nvs_set_u8(nvs, "bright", brightness);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(nvs, "rad_range", stored.radar_range_miles);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "osky_en", stored.opensky_enabled ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "osky_id", stored.opensky_client_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "osky_sec", stored.opensky_client_secret);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "airlabs", stored.airlabs_api_key);
    }
    if (!dock_entries_valid(&stored)) {
        set_default_dock_entries(&stored);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "dock_cnt", stored.dock_entry_count);
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(
            nvs,
            "dock_ent",
            stored.dock_entries,
            sizeof(stored.dock_entries));
    }
    if (!main_screens_valid(&stored)) {
        set_default_main_screens(&stored);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "main_cnt", stored.main_screen_count);
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(
            nvs,
            "main_ord",
            stored.main_screens,
            sizeof(stored.main_screens));
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "fa_avail", stored.flightaware_available ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "revision", stored.revision);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        lock_config();
        current_config = stored;
        unlock_config();
        app_location_set_custom_data(stored.custom_location_data);
    }
    return err;
}

void app_config_get(app_config_t *out)
{
    if (out == NULL) {
        return;
    }
    lock_config();
    *out = current_config;
    unlock_config();
}

const app_wifi_profile_t *app_config_active_wifi_profile(const app_config_t *config)
{
    if (config == NULL) {
        return NULL;
    }
    app_wifi_profile_id_t profile = config->active_wifi_profile;
    if (profile < 0 || profile >= APP_WIFI_PROFILE_COUNT) {
        profile = APP_WIFI_PROFILE_HOME;
    }
    return &config->wifi_profiles[profile];
}

const char *app_config_wifi_profile_name(app_wifi_profile_id_t profile)
{
    switch (profile) {
    case APP_WIFI_PROFILE_HOTSPOT:
        return "hotspot";
    case APP_WIFI_PROFILE_HOME:
    default:
        return "home";
    }
}

bool app_config_parse_wifi_profile_name(const char *name, app_wifi_profile_id_t *profile)
{
    if (name == NULL || profile == NULL) {
        return false;
    }
    if (strcmp(name, "home") == 0) {
        *profile = APP_WIFI_PROFILE_HOME;
        return true;
    }
    if (strcmp(name, "hotspot") == 0) {
        *profile = APP_WIFI_PROFILE_HOTSPOT;
        return true;
    }
    return false;
}

const char *app_config_accent_color_name(app_accent_color_t color)
{
    switch (color) {
    case APP_ACCENT_CYAN:
        return "cyan";
    case APP_ACCENT_ORANGE:
        return "orange";
    case APP_ACCENT_RED:
        return "red";
    case APP_ACCENT_GREEN:
    default:
        return "green";
    }
}

bool app_config_parse_accent_color_name(const char *name, app_accent_color_t *color)
{
    if (name == NULL || color == NULL) {
        return false;
    }
    if (strcmp(name, "green") == 0) {
        *color = APP_ACCENT_GREEN;
        return true;
    }
    if (strcmp(name, "cyan") == 0) {
        *color = APP_ACCENT_CYAN;
        return true;
    }
    if (strcmp(name, "orange") == 0) {
        *color = APP_ACCENT_ORANGE;
        return true;
    }
    if (strcmp(name, "red") == 0 || strcmp(name, "teal") == 0) {
        *color = APP_ACCENT_RED;
        return true;
    }
    return false;
}
