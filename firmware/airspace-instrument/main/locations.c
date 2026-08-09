// AirESP32ace
// Created by VM, August 2026

#include "locations.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static const app_location_preset_t presets[] = {
    {
        .key = "SJC",
        .label = "San Jose",
        .lat = "37.3639",
        .lon = "-121.9289",
        .timezone = "PST8PDT,M3.2.0,M11.1.0",
        .metar = "KSJC",
        .radar_half_degrees = "1.55",
    },
    {
        .key = "SFO",
        .label = "San Francisco",
        .lat = "37.6213",
        .lon = "-122.3790",
        .timezone = "PST8PDT,M3.2.0,M11.1.0",
        .metar = "KSFO",
        .radar_half_degrees = "1.55",
    },
    {
        .key = "SAN",
        .label = "San Diego",
        .lat = "32.7338",
        .lon = "-117.1933",
        .timezone = "PST8PDT,M3.2.0,M11.1.0",
        .metar = "KSAN",
        .radar_half_degrees = "1.55",
    },
    {
        .key = "JFK",
        .label = "New York",
        .lat = "40.6413",
        .lon = "-73.7781",
        .timezone = "EST5EDT,M3.2.0,M11.1.0",
        .metar = "KJFK",
        .radar_half_degrees = "1.55",
    },
    {
        .key = "MOW",
        .label = "Moscow",
        .lat = "55.7558",
        .lon = "37.6173",
        .timezone = "MSK-3",
        .metar = "UUEE",
        .radar_half_degrees = "1.55",
    },
    {
        .key = "LED",
        .label = "St. Petersburg",
        .lat = "59.8003",
        .lon = "30.2625",
        .timezone = "MSK-3",
        .metar = "ULLI",
        .radar_half_degrees = "1.55",
    },
    {
        .key = "DUS",
        .label = "Dusseldorf",
        .lat = "51.2895",
        .lon = "6.7668",
        .timezone = "CET-1CEST,M3.5.0,M10.5.0/3",
        .metar = "EDDL",
        .radar_half_degrees = "1.55",
    },
    {
        .key = "ICN",
        .label = "Seoul",
        .lat = "37.4602",
        .lon = "126.4407",
        .timezone = "KST-9",
        .metar = "RKSI",
        .radar_half_degrees = "1.55",
    },
};

#define CUSTOM_LOCATION_MAX 8

typedef struct {
    char key[16];
    char label[48];
    char lat[20];
    char lon[20];
    char timezone[64];
    char metar[8];
    char radar_half_degrees[20];
    app_location_preset_t preset;
} custom_location_storage_t;

static custom_location_storage_t custom_locations[CUSTOM_LOCATION_MAX];
static size_t custom_location_count;

static size_t built_in_location_count(void)
{
    return sizeof(presets) / sizeof(presets[0]);
}

static const app_location_preset_t *find_built_in(const char *key)
{
    if (key == NULL || key[0] == '\0') {
        return NULL;
    }
    for (size_t index = 0; index < built_in_location_count(); index++) {
        if (strcasecmp(presets[index].key, key) == 0) {
            return &presets[index];
        }
    }
    return NULL;
}

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

size_t app_location_preset_count(void)
{
    return built_in_location_count() + custom_location_count;
}

const app_location_preset_t *app_location_preset_at(size_t index)
{
    if (index < built_in_location_count()) {
        return &presets[index];
    }
    index -= built_in_location_count();
    return index < custom_location_count
        ? &custom_locations[index].preset
        : NULL;
}

const app_location_preset_t *app_location_preset_find(const char *key)
{
    if (key == NULL || key[0] == '\0') {
        return NULL;
    }

    const app_location_preset_t *built_in = find_built_in(key);
    if (built_in != NULL) {
        return built_in;
    }
    for (size_t index = 0; index < custom_location_count; index++) {
        if (strcasecmp(custom_locations[index].key, key) == 0) {
            return &custom_locations[index].preset;
        }
    }
    return NULL;
}

bool app_location_preset_is_builtin(const char *key)
{
    return find_built_in(key) != NULL;
}

void app_location_set_custom_data(const char *serialized)
{
    custom_location_count = 0;
    memset(custom_locations, 0, sizeof(custom_locations));
    if (serialized == NULL || serialized[0] == '\0') {
        return;
    }

    char source[1024];
    copy_string(source, sizeof(source), serialized);
    char *line_save = NULL;
    for (char *line = strtok_r(source, "\n", &line_save);
         line != NULL && custom_location_count < CUSTOM_LOCATION_MAX;
         line = strtok_r(NULL, "\n", &line_save)) {
        char *fields[7] = {0};
        char *field_save = NULL;
        size_t field_count = 0;
        for (char *field = strtok_r(line, "|", &field_save);
             field != NULL && field_count < 7;
             field = strtok_r(NULL, "|", &field_save)) {
            fields[field_count++] = field;
        }
        if (field_count != 7 ||
            fields[0][0] == '\0' ||
            find_built_in(fields[0]) != NULL) {
            continue;
        }

        custom_location_storage_t *storage =
            &custom_locations[custom_location_count];
        copy_string(storage->key, sizeof(storage->key), fields[0]);
        copy_string(storage->label, sizeof(storage->label), fields[1]);
        copy_string(storage->lat, sizeof(storage->lat), fields[2]);
        copy_string(storage->lon, sizeof(storage->lon), fields[3]);
        copy_string(storage->timezone, sizeof(storage->timezone), fields[4]);
        copy_string(
            storage->metar,
            sizeof(storage->metar),
            strcmp(fields[5], "-") == 0 ? "" : fields[5]);
        copy_string(
            storage->radar_half_degrees,
            sizeof(storage->radar_half_degrees),
            fields[6]);
        storage->preset = (app_location_preset_t) {
            .key = storage->key,
            .label = storage->label,
            .lat = storage->lat,
            .lon = storage->lon,
            .timezone = storage->timezone,
            .metar = storage->metar,
            .radar_half_degrees = storage->radar_half_degrees,
        };
        custom_location_count++;
    }
}

const app_location_preset_t *app_location_preset_for_config(const app_config_t *config)
{
    if (config == NULL) {
        return &presets[0];
    }

    const app_location_preset_t *preset = app_location_preset_find(config->location_key);
    return preset != NULL ? preset : &presets[0];
}

bool app_location_apply_preset(app_config_t *config, const char *key)
{
    if (config == NULL) {
        return false;
    }

    const app_location_preset_t *preset = app_location_preset_find(key);
    if (preset == NULL) {
        return false;
    }

    copy_string(config->location_key, sizeof(config->location_key), preset->key);
    copy_string(config->location_label, sizeof(config->location_label), preset->label);
    copy_string(config->location_lat, sizeof(config->location_lat), preset->lat);
    copy_string(config->location_lon, sizeof(config->location_lon), preset->lon);
    copy_string(config->timezone, sizeof(config->timezone), preset->timezone);
    copy_string(config->metar_station, sizeof(config->metar_station), preset->metar);
    copy_string(config->radar_half_degrees, sizeof(config->radar_half_degrees), preset->radar_half_degrees);
    return true;
}
