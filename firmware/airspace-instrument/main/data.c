// AirESP32ace
// Created by VM, August 2026

#include "data.h"
#include "tracker.h"
#include "launches.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "png.h"
#include "aircraft_type_registry.h"
#include "app_config.h"
#include "cargo_logos.h"
#include "locations.h"
#include "network.h"
#include "route_direction.h"
#include "sdkconfig.h"

#define FETCH_SMALL_MAX 8192
#define FETCH_MEDIUM_MAX 24576
#define FETCH_LARGE_MAX 49152
#define FETCH_AIRCRAFT_MAX (128 * 1024)
#define FETCH_PNG_MAX (384 * 1024)
#define DATA_TASK_STACK_BYTES (32 * 1024)
#define DATA_REFRESH_MS (5 * 60 * 1000)
#define AIRCRAFT_REFRESH_MS (30 * 1000)
#define AIRCRAFT_FALLBACK_REFRESH_MS (3 * 60 * 1000)
#define AIRCRAFT_ERROR_RETRY_MS (60 * 1000)
#define AIRCRAFT_RATE_LIMIT_RETRY_MS (5 * 60 * 1000)
#define OPENSKY_TOKEN_URL "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"
#define OPENSKY_TOKEN_REFRESH_MARGIN_SECONDS 30
#define AIRCRAFT_CANDIDATE_LIMIT 48
#define AIRCRAFT_STATUTE_MI_TO_NM 0.868976f
#define AIRCRAFT_QUERY_PADDING 1.10f
#define AIRCRAFT_RADIUS_DEFAULT_MI 20.0f
#define AIRCRAFT_RADIUS_MIN_MI 10.0f
#define AIRCRAFT_RADIUS_MAX_MI 50.0f
#define AIRCRAFT_MAX_DISPLAY_NM (AIRCRAFT_RADIUS_MAX_MI * AIRCRAFT_STATUTE_MI_TO_NM * AIRCRAFT_QUERY_PADDING)
#define STATUTE_MI_PER_LAT_DEG 69.0
#define AIRCRAFT_STALE_SECONDS 180
#define AIRCRAFT_MIN_SPEED_KT 20
#define AIRCRAFT_REASONABLE_UNIX_TIME 1700000000
#define AIRCRAFT_METADATA_CACHE_COUNT 24
#define AIRPORT_CITY_CACHE_COUNT 32
#define AIRCRAFT_METADATA_REFRESH_SECONDS (6 * 60 * 60)
#define AIRCRAFT_METADATA_BACKGROUND_PER_CYCLE 4
#define AIRCRAFT_ROUTE_MAX_MIN_EXCESS_NM 180.0f
#define AIRCRAFT_ROUTE_MAX_RELATIVE_EXCESS 0.35f
#define AIRCRAFT_ROUTE_ENDPOINT_MARGIN_NM 250.0f
#define ADSBDB_API_BASE "https://api.adsbdb.com/v0"
#define AIRLABS_API_BASE "https://airlabs.co/api/v9"
#define AIRLABS_SUCCESS_CACHE_SECONDS (30 * 60)
#define AIRLABS_FAILURE_RETRY_SECONDS (5 * 60)
#define AIRLABS_MAX_POSITION_AGE_SECONDS (10 * 60)
#define AIRLABS_MAX_POSITION_DIFFERENCE_NM 50.0f
#define AIRLINE_LOGO_API_BASE "https://airlabs.co/img/airline/s"
#define AIRLINE_LOGO_DIRECTORY "/spiffs/logos"
#define AIRLINE_LOGO_CACHE_LIMIT 500
#define AIRLINE_LOGO_PNG_MAX (16 * 1024)
#define AIRLINE_LOGO_MAX_DIM 64
#define AIRLINE_LOGO_MAX_PIXELS (AIRLINE_LOGO_MAX_DIM * AIRLINE_LOGO_MAX_DIM)
#define AIRLINE_LOGO_BUFFER_BYTES (AIRLINE_LOGO_MAX_PIXELS * 3)
#define WEATHER_RADAR_SOURCE_SIZE 512
#define WEATHER_RADAR_ZOOM 7
#define WEATHER_RADAR_METERS_PER_MILE 1609.344
#define WEATHER_RADAR_WEB_MERCATOR_METERS_PER_PIXEL 156543.03392
#define WEATHER_RADAR_OBSERVED_FRAME_COUNT 2

static const char *TAG = "airspace_data";
static app_data_status_t status = {
    .state = APP_DATA_IDLE,
};
static SemaphoreHandle_t status_mutex;
static TaskHandle_t data_task_handle;
static char opensky_access_token[2048];
static int opensky_token_expires_unix;
static int opensky_rate_limited_until_unix;
static bool opensky_rate_limit_was_authenticated;
static bool aircraft_last_fetch_used_fallback;
static volatile float aircraft_radius_miles = AIRCRAFT_RADIUS_DEFAULT_MI;
static volatile bool full_refresh_requested;
static volatile bool aircraft_refresh_requested;
static volatile bool aircraft_enrichment_requested;
static volatile bool aircraft_enrichment_active;
static volatile bool airline_logo_requested;
static volatile bool weather_radar_requested;
static volatile bool api_validation_requested;
static volatile bool data_paused;
static volatile float requested_weather_radar_radius_miles = AIRCRAFT_RADIUS_DEFAULT_MI;
static char weather_location_key[8];
static char requested_weather_radar_location_key[8];
static char requested_aircraft_icao24[8];
static char requested_aircraft_callsign[12];
static char active_aircraft_icao24[8];
static char requested_airline_logo_iata[4];
static char rainviewer_host[128] = "https://tilecache.rainviewer.com";
static char rainviewer_paths[WEATHER_RADAR_OBSERVED_FRAME_COUNT][96];
static int rainviewer_frame_unix[WEATHER_RADAR_OBSERVED_FRAME_COUNT];
static int rainviewer_frame_count;
static uint8_t *weather_radar_overlays;
static int weather_radar_observed_frame_count;
static uint32_t weather_radar_overlay_revision;
static uint8_t *airline_logo_buffers[2];
static int airline_logo_active_buffer = -1;
static uint16_t airline_logo_width;
static uint16_t airline_logo_height;
static size_t airline_logo_bytes;
static char airline_logo_iata[4];
static uint32_t airline_logo_revision;

static void copy_string(char *dst, size_t dst_size, const char *src);

static void weather_config(app_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    app_config_get(cfg);

    char key[sizeof(weather_location_key)] = {0};
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    copy_string(key, sizeof(key), weather_location_key);
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    if (key[0] != '\0') {
        (void)app_location_apply_preset(cfg, key);
    }
}

typedef struct {
    bool occupied;
    bool metadata_valid;
    bool route_valid;
    bool airlabs_valid;
    bool airlabs_query_succeeded;
    int attempted_unix;
    int airlabs_attempted_unix;
    char icao24[8];
    char route_callsign[12];
    char airlabs_callsign[12];
    char airlabs_match_icao24[8];
    char airlabs_match_registration[12];
    int airlabs_updated_unix;
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
    char origin_city[24];
    char destination_city[24];
    bool route_coordinates_valid;
    double origin_latitude;
    double origin_longitude;
    double destination_latitude;
    double destination_longitude;
} aircraft_metadata_cache_t;

typedef enum {
    AIRLABS_ENRICH_UNAVAILABLE = 0,
    AIRLABS_ENRICH_NO_IDENTITY_MATCH,
    AIRLABS_ENRICH_MATCHED,
} airlabs_enrich_result_t;

static aircraft_metadata_cache_t aircraft_metadata_cache[AIRCRAFT_METADATA_CACHE_COUNT];
static int aircraft_metadata_cache_cursor;

typedef struct {
    bool occupied;
    char code[5];
    char city[24];
} airport_city_cache_entry_t;

static airport_city_cache_entry_t airport_city_cache[AIRPORT_CITY_CACHE_COUNT];
static int airport_city_cache_cursor;

typedef struct {
    const char *code;
    const char *city;
} airport_city_fallback_t;

/* Small flash-only fallback for common destinations.  Live provider data
 * still wins when available; this prevents a missing municipality field or
 * a slow enrichment request from leaving a familiar IATA code unlabeled. */
static const airport_city_fallback_t airport_city_fallbacks[] = {
    {"AMS", "Amsterdam"}, {"ANC", "Anchorage"}, {"ATL", "Atlanta"},
    {"BCN", "Barcelona"}, {"BOS", "Boston"}, {"BWI", "Baltimore"},
    {"CDG", "Paris"}, {"CLT", "Charlotte"}, {"CPH", "Copenhagen"},
    {"DCA", "Washington"}, {"DEN", "Denver"}, {"DFW", "Dallas"},
    {"DME", "Moscow"}, {"DUS", "Dusseldorf"}, {"DXB", "Dubai"},
    {"EWR", "Newark"}, {"FRA", "Frankfurt"}, {"HEL", "Helsinki"},
    {"HKG", "Hong Kong"}, {"HND", "Tokyo"}, {"HNL", "Honolulu"},
    {"IAD", "Washington"}, {"IAH", "Houston"}, {"ICN", "Seoul"},
    {"IST", "Istanbul"}, {"JFK", "New York"}, {"KOA", "Kailua-Kona"},
    {"LAS", "Las Vegas"}, {"LAX", "Los Angeles"}, {"LED", "St. Petersburg"},
    {"LGA", "New York"}, {"LHR", "London"}, {"LIH", "Lihue"},
    {"MAD", "Madrid"}, {"MCO", "Orlando"}, {"MEX", "Mexico City"},
    {"MIA", "Miami"}, {"MSP", "Minneapolis"}, {"NRT", "Tokyo"},
    {"OAK", "Oakland"}, {"OGG", "Kahului"}, {"ORD", "Chicago"},
    {"PDX", "Portland"}, {"PHX", "Phoenix"}, {"SAN", "San Diego"},
    {"SEA", "Seattle"}, {"SFO", "San Francisco"}, {"SIN", "Singapore"},
    {"SJC", "San Jose"}, {"SLC", "Salt Lake City"}, {"SVO", "Moscow"},
    {"VIE", "Vienna"}, {"VKO", "Moscow"}, {"YVR", "Vancouver"},
    {"YYZ", "Toronto"},
};

static bool url_encode_component(const char *src, char *dst, size_t dst_size);

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%.*s", (int)dst_size - 1, src ? src : "");
}

static void copy_icao_type_designator(char *dst,
                                      size_t dst_size,
                                      const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    size_t used = 0;
    for (size_t index = 0;
         src != NULL && src[index] != '\0' && used < 4;
         index++) {
        const unsigned char value = (unsigned char)src[index];
        if (!isalnum(value)) {
            continue;
        }
        dst[used++] = (char)toupper(value);
    }
    dst[used] = '\0';
}

static void cache_airport_city(const char *code, const char *city)
{
    if (code == NULL || code[0] == '\0' || city == NULL || city[0] == '\0') {
        return;
    }

    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }

    airport_city_cache_entry_t *entry = NULL;
    for (int i = 0; i < AIRPORT_CITY_CACHE_COUNT; i++) {
        if (airport_city_cache[i].occupied &&
            strcasecmp(airport_city_cache[i].code, code) == 0) {
            entry = &airport_city_cache[i];
            break;
        }
        if (entry == NULL && !airport_city_cache[i].occupied) {
            entry = &airport_city_cache[i];
        }
    }
    if (entry == NULL) {
        entry = &airport_city_cache[airport_city_cache_cursor];
        airport_city_cache_cursor =
            (airport_city_cache_cursor + 1) % AIRPORT_CITY_CACHE_COUNT;
    }
    memset(entry, 0, sizeof(*entry));
    entry->occupied = true;
    copy_string(entry->code, sizeof(entry->code), code);
    copy_string(entry->city, sizeof(entry->city), city);

    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
}

bool app_data_get_airport_city(const char *code, char *city, size_t city_size)
{
    if (city == NULL || city_size == 0) {
        return false;
    }
    city[0] = '\0';
    if (code == NULL || code[0] == '\0') {
        return false;
    }

    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    for (int i = 0; i < AIRPORT_CITY_CACHE_COUNT; i++) {
        if (airport_city_cache[i].occupied &&
            strcasecmp(airport_city_cache[i].code, code) == 0) {
            copy_string(city, city_size, airport_city_cache[i].city);
            break;
        }
    }
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    if (city[0] == '\0') {
        for (size_t i = 0;
             i < sizeof(airport_city_fallbacks) / sizeof(airport_city_fallbacks[0]);
             i++) {
            if (strcasecmp(airport_city_fallbacks[i].code, code) == 0) {
                copy_string(city, city_size, airport_city_fallbacks[i].city);
                break;
            }
        }
    }
    return city[0] != '\0';
}

static void notify_data_task(void)
{
    if (data_task_handle != NULL) {
        xTaskNotifyGive(data_task_handle);
    }
}

static float clamp_aircraft_radius_miles(float miles)
{
    if (!isfinite(miles)) {
        return AIRCRAFT_RADIUS_DEFAULT_MI;
    }
    if (miles < AIRCRAFT_RADIUS_MIN_MI) {
        return AIRCRAFT_RADIUS_MIN_MI;
    }
    if (miles > AIRCRAFT_RADIUS_MAX_MI) {
        return AIRCRAFT_RADIUS_MAX_MI;
    }
    return miles;
}

static float read_aircraft_radius_miles(void)
{
    return clamp_aircraft_radius_miles(aircraft_radius_miles);
}

void app_data_set_aircraft_radius_miles(float miles)
{
    const float clamped = clamp_aircraft_radius_miles(miles);
    if (fabsf(read_aircraft_radius_miles() - clamped) < 0.1f) {
        return;
    }
    aircraft_radius_miles = clamped;
    aircraft_refresh_requested = true;
    notify_data_task();
}

static void set_state(app_data_state_t state)
{
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    status.state = state;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
}

static app_data_status_t read_status(void)
{
    app_data_status_t copy;
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    copy = status;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    return copy;
}

static void write_status(const app_data_status_t *next)
{
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    const app_api_health_state_t flightaware_api_health =
        status.flightaware_api_health;
    const app_api_health_state_t opensky_api_health =
        status.opensky_api_health;
    const app_api_health_state_t airlabs_api_health =
        status.airlabs_api_health;
    const uint32_t api_health_revision = status.api_health_revision;
    status = *next;
    /* API health is published asynchronously while slower weather or
     * aircraft requests may still hold an older status snapshot. */
    status.flightaware_api_health = flightaware_api_health;
    status.opensky_api_health = opensky_api_health;
    status.airlabs_api_health = airlabs_api_health;
    status.api_health_revision = api_health_revision;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
}

static uint32_t aircraft_refresh_delay_ms(void)
{
    const app_data_status_t current = read_status();
    if (aircraft_last_fetch_used_fallback) {
        return AIRCRAFT_FALLBACK_REFRESH_MS;
    }
    if (current.aircraft_http_status == 429) {
        return AIRCRAFT_RATE_LIMIT_RETRY_MS;
    }
    if (current.aircraft_http_status != 0 &&
        (current.aircraft_http_status < 200 ||
         current.aircraft_http_status >= 300)) {
        return AIRCRAFT_ERROR_RETRY_MS;
    }
    return AIRCRAFT_REFRESH_MS;
}

static const cJSON *object_item(const cJSON *object, const char *name)
{
    return cJSON_IsObject(object) ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
}

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *item = object_item(object, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static int json_int(const cJSON *object, const char *name, int fallback)
{
    const cJSON *item = object_item(object, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static double json_double(const cJSON *object, const char *name, double fallback)
{
    const cJSON *item = object_item(object, name);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static double json_array_double(const cJSON *object,
                                const char *name,
                                int index,
                                double fallback)
{
    const cJSON *array = object_item(object, name);
    const cJSON *item = cJSON_IsArray(array)
        ? cJSON_GetArrayItem(array, index)
        : NULL;
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static const char *json_array_string(const cJSON *object,
                                     const char *name,
                                     int index)
{
    const cJSON *array = object_item(object, name);
    const cJSON *item = cJSON_IsArray(array)
        ? cJSON_GetArrayItem(array, index)
        : NULL;
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static void trim_callsign(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    while (*s != '\0' && isspace((unsigned char)*s)) {
        memmove(s, s + 1, strlen(s));
    }
}

static double deg_to_rad(double deg)
{
    return deg * 3.14159265358979323846 / 180.0;
}

static double rad_to_deg(double rad)
{
    return rad * 180.0 / 3.14159265358979323846;
}

static float distance_nm(double lat1, double lon1, double lat2, double lon2)
{
    const double r_nm = 3440.065;
    const double p1 = deg_to_rad(lat1);
    const double p2 = deg_to_rad(lat2);
    const double dp = deg_to_rad(lat2 - lat1);
    const double dl = deg_to_rad(lon2 - lon1);
    const double a = sin(dp / 2.0) * sin(dp / 2.0) +
                     cos(p1) * cos(p2) * sin(dl / 2.0) * sin(dl / 2.0);
    const double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return (float)(r_nm * c);
}

static double initial_bearing_radians(double lat1,
                                      double lon1,
                                      double lat2,
                                      double lon2)
{
    const double p1 = deg_to_rad(lat1);
    const double p2 = deg_to_rad(lat2);
    const double dl = deg_to_rad(lon2 - lon1);
    return atan2(sin(dl) * cos(p2),
                 cos(p1) * sin(p2) -
                     sin(p1) * cos(p2) * cos(dl));
}

static bool aircraft_route_progress(const app_aircraft_t *aircraft,
                                    const aircraft_metadata_cache_t *entry,
                                    float *progress,
                                    float *route_nm,
                                    float *covered_nm,
                                    float *remaining_nm)
{
    if (aircraft == NULL ||
        entry == NULL ||
        !entry->route_coordinates_valid ||
        !isfinite(aircraft->latitude) ||
        !isfinite(aircraft->longitude)) {
        return false;
    }

    const double earth_radius_nm = 3440.065;
    const float total_nm = distance_nm(entry->origin_latitude,
                                       entry->origin_longitude,
                                       entry->destination_latitude,
                                       entry->destination_longitude);
    if (!isfinite(total_nm) || total_nm < 10.0f) {
        return false;
    }

    const double origin_to_aircraft_nm =
        distance_nm(entry->origin_latitude,
                    entry->origin_longitude,
                    aircraft->latitude,
                    aircraft->longitude);
    const double angular_distance = origin_to_aircraft_nm / earth_radius_nm;
    const double route_bearing =
        initial_bearing_radians(entry->origin_latitude,
                                entry->origin_longitude,
                                entry->destination_latitude,
                                entry->destination_longitude);
    const double aircraft_bearing =
        initial_bearing_radians(entry->origin_latitude,
                                entry->origin_longitude,
                                aircraft->latitude,
                                aircraft->longitude);
    const double along_track_angle =
        atan2(sin(angular_distance) *
                  cos(aircraft_bearing - route_bearing),
              cos(angular_distance));
    const double total_angle = (double)total_nm / earth_radius_nm;
    if (!isfinite(along_track_angle) || total_angle <= 0.0) {
        return false;
    }

    const float fraction =
        fmaxf(0.0f,
              fminf(1.0f,
                    (float)(along_track_angle / total_angle)));
    if (progress != NULL) {
        *progress = fraction;
    }
    if (route_nm != NULL) {
        *route_nm = total_nm;
    }
    if (covered_nm != NULL) {
        *covered_nm = total_nm * fraction;
    }
    if (remaining_nm != NULL) {
        *remaining_nm = total_nm * (1.0f - fraction);
    }
    return true;
}

static int bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    const double p1 = deg_to_rad(lat1);
    const double p2 = deg_to_rad(lat2);
    const double dl = deg_to_rad(lon2 - lon1);
    const double y = sin(dl) * cos(p2);
    const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    int b = (int)lround(rad_to_deg(atan2(y, x)));
    b = (b + 360) % 360;
    return b;
}

static aircraft_metadata_cache_t *metadata_cache_find(const char *icao24)
{
    if (icao24 == NULL || icao24[0] == '\0') {
        return NULL;
    }
    for (int i = 0; i < AIRCRAFT_METADATA_CACHE_COUNT; i++) {
        if (aircraft_metadata_cache[i].occupied &&
            strcasecmp(aircraft_metadata_cache[i].icao24, icao24) == 0) {
            return &aircraft_metadata_cache[i];
        }
    }
    return NULL;
}

static aircraft_metadata_cache_t *metadata_cache_slot(const char *icao24)
{
    aircraft_metadata_cache_t *entry = metadata_cache_find(icao24);
    if (entry != NULL) {
        return entry;
    }

    for (int i = 0; i < AIRCRAFT_METADATA_CACHE_COUNT; i++) {
        if (!aircraft_metadata_cache[i].occupied) {
            entry = &aircraft_metadata_cache[i];
            memset(entry, 0, sizeof(*entry));
            entry->occupied = true;
            copy_string(entry->icao24, sizeof(entry->icao24), icao24);
            return entry;
        }
    }

    entry = &aircraft_metadata_cache[aircraft_metadata_cache_cursor];
    aircraft_metadata_cache_cursor =
        (aircraft_metadata_cache_cursor + 1) % AIRCRAFT_METADATA_CACHE_COUNT;
    memset(entry, 0, sizeof(*entry));
    entry->occupied = true;
    copy_string(entry->icao24, sizeof(entry->icao24), icao24);
    return entry;
}

static bool metadata_cache_is_fresh(const app_aircraft_t *aircraft)
{
    const aircraft_metadata_cache_t *entry =
        aircraft != NULL ? metadata_cache_find(aircraft->icao24) : NULL;
    if (entry == NULL || entry->attempted_unix <= 0) {
        return false;
    }
    if (strcmp(entry->route_callsign, aircraft->callsign) != 0) {
        return false;
    }
    return (int)time(NULL) - entry->attempted_unix < AIRCRAFT_METADATA_REFRESH_SECONDS;
}

static bool apply_metadata_cache(app_aircraft_t *aircraft)
{
    aircraft_metadata_cache_t *entry =
        aircraft != NULL ? metadata_cache_find(aircraft->icao24) : NULL;
    if (entry == NULL) {
        return false;
    }

    bool changed = false;
    if (entry->metadata_valid) {
        aircraft->metadata_valid = true;
        copy_string(aircraft->typecode, sizeof(aircraft->typecode), entry->typecode);
        copy_string(aircraft->model, sizeof(aircraft->model), entry->model);
        copy_string(aircraft->registration, sizeof(aircraft->registration), entry->registration);
        copy_string(aircraft->manufacturer, sizeof(aircraft->manufacturer), entry->manufacturer);
        copy_string(aircraft->engine_type, sizeof(aircraft->engine_type), entry->engine_type);
        aircraft->engine_count = entry->engine_count;
        aircraft->type_source = entry->type_source;
        changed = true;
    }
    if (strcmp(entry->route_callsign, aircraft->callsign) == 0) {
        const bool had_route = aircraft->route_valid;
        copy_string(aircraft->airline_name, sizeof(aircraft->airline_name), entry->airline_name);
        copy_string(aircraft->airline_iata, sizeof(aircraft->airline_iata), entry->airline_iata);
        copy_string(aircraft->airline_icao, sizeof(aircraft->airline_icao), entry->airline_icao);
        /* The status snapshot is reused across progressive metadata updates.
         * Explicitly erase a previously published callsign route when a
         * stronger identity lookup rejects it; otherwise stale endpoints can
         * survive even though the cache has already been cleared. */
        aircraft->route_valid = false;
        aircraft->origin_iata[0] = '\0';
        aircraft->destination_iata[0] = '\0';
        aircraft->route_progress_valid = false;
        aircraft->route_progress = 0.0f;
        aircraft->route_distance_nm = 0.0f;
        aircraft->route_distance_covered_nm = 0.0f;
        aircraft->route_distance_remaining_nm = 0.0f;
        if (entry->route_valid) {
            aircraft->route_valid = true;
            copy_string(aircraft->origin_iata, sizeof(aircraft->origin_iata), entry->origin_iata);
            copy_string(aircraft->destination_iata,
                        sizeof(aircraft->destination_iata),
                        entry->destination_iata);
            cache_airport_city(entry->origin_iata, entry->origin_city);
            cache_airport_city(entry->destination_iata, entry->destination_city);
            aircraft->route_progress_valid = aircraft_route_progress(
                aircraft,
                entry,
                &aircraft->route_progress,
                &aircraft->route_distance_nm,
                &aircraft->route_distance_covered_nm,
                &aircraft->route_distance_remaining_nm);
        }
        changed = changed || had_route || entry->airline_name[0] != '\0' || entry->route_valid;
    }
    return changed;
}

static bool aircraft_route_is_plausible(const app_aircraft_t *aircraft,
                                        double origin_lat,
                                        double origin_lon,
                                        double destination_lat,
                                        double destination_lon)
{
    if (aircraft == NULL ||
        !isfinite(origin_lat) ||
        !isfinite(origin_lon) ||
        !isfinite(destination_lat) ||
        !isfinite(destination_lon)) {
        return false;
    }

    const float route_nm = distance_nm(origin_lat, origin_lon, destination_lat, destination_lon);
    const float from_origin_nm =
        distance_nm(origin_lat, origin_lon, aircraft->latitude, aircraft->longitude);
    const float to_destination_nm =
        distance_nm(aircraft->latitude, aircraft->longitude, destination_lat, destination_lon);
    const float excess_nm = from_origin_nm + to_destination_nm - route_nm;
    const float allowed_excess =
        fmaxf(AIRCRAFT_ROUTE_MAX_MIN_EXCESS_NM, route_nm * AIRCRAFT_ROUTE_MAX_RELATIVE_EXCESS);

    return route_nm >= 10.0f &&
           excess_nm <= allowed_excess &&
           from_origin_nm <= route_nm + AIRCRAFT_ROUTE_ENDPOINT_MARGIN_NM &&
           to_destination_nm <= route_nm + AIRCRAFT_ROUTE_ENDPOINT_MARGIN_NM;
}

static int compare_aircraft_by_distance(const void *a, const void *b)
{
    const app_aircraft_t *left = (const app_aircraft_t *)a;
    const app_aircraft_t *right = (const app_aircraft_t *)b;
    if (left->distance_nm < right->distance_nm) {
        return -1;
    }
    if (left->distance_nm > right->distance_nm) {
        return 1;
    }
    return 0;
}

static bool callsign_has_commercial_prefix(const char *callsign)
{
    if (callsign == NULL || callsign[0] == '\0') {
        return false;
    }
    static const char *prefixes[] = {
        "AAL","ACA","AFR","AIC","ANA","AAR","ASA","BAW","CAL","CPA",
        "DAL","DLH","EDV","EIN","ENY","EVA","FDX","FFT","FIN","HAL",
        "IBE","JAL","JBU","JIA","KAL","KLM","NKS","PAL","PDT","QFA",
        "QTR","QXE","RPA","RYR","SAS","SCX","SIA","SKW","SWA","THY",
        "UAE","UAL","UPS","VIR","VOI","VRD","WJA"
    };
    for (size_t index = 0;
         index < sizeof(prefixes) / sizeof(prefixes[0]);
         index++) {
        const size_t prefix_length = strlen(prefixes[index]);
        if (strncasecmp(callsign, prefixes[index], prefix_length) == 0 &&
            callsign[prefix_length] >= '0' &&
            callsign[prefix_length] <= '9') {
            return true;
        }
    }
    return false;
}

static bool aircraft_is_identified_commercial(
    const app_aircraft_t *aircraft)
{
    if (aircraft == NULL || !aircraft->valid ||
        airspace_aircraft_is_rotorcraft(
            aircraft->typecode,
            aircraft->model,
            aircraft->manufacturer,
            aircraft->engine_type,
            aircraft->category)) {
        return false;
    }
    return aircraft->airline_icao[0] != '\0' ||
        aircraft->airline_iata[0] != '\0' ||
        aircraft->airline_name[0] != '\0' ||
        callsign_has_commercial_prefix(aircraft->callsign);
}

static float aircraft_passenger_priority_bonus_nm(
    const app_aircraft_t *aircraft,
    float radius_miles)
{
    if (aircraft == NULL || radius_miles <= 20.0f ||
        airspace_aircraft_is_rotorcraft(
            aircraft->typecode,
            aircraft->model,
            aircraft->manufacturer,
            aircraft->engine_type,
            aircraft->category)) {
        return 0.0f;
    }

    if (!aircraft_is_identified_commercial(aircraft)) {
        return 0.0f;
    }

    float full_bonus_nm = 8.0f;
    airspace_aircraft_type_match_t match;
    if (airspace_aircraft_type_lookup(
            aircraft->typecode,
            aircraft->model,
            aircraft->manufacturer,
            &match)) {
        switch (match.shape) {
        case AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY:
            full_bonus_nm = 14.0f;
            break;
        case AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET:
            full_bonus_nm = 12.0f;
            break;
        case AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET:
            full_bonus_nm = 10.0f;
            break;
        case AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET:
            full_bonus_nm = 8.0f;
            break;
        case AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP:
            full_bonus_nm = 5.0f;
            break;
        default:
            break;
        }
    } else if (aircraft->category == 6) {
        full_bonus_nm = 12.0f;
    } else if (aircraft->category == 4 || aircraft->category == 5) {
        full_bonus_nm = 10.0f;
    }

    const float strength = fminf(1.0f, (radius_miles - 20.0f) / 30.0f);
    return full_bonus_nm * strength;
}

static float aircraft_selection_score(
    const app_aircraft_t *aircraft,
    float radius_miles)
{
    return aircraft->distance_nm -
        aircraft_passenger_priority_bonus_nm(aircraft, radius_miles);
}

static bool aircraft_state_is_stale(const cJSON *time_position, const cJSON *last_contact)
{
    const time_t now = time(NULL);
    if (now < AIRCRAFT_REASONABLE_UNIX_TIME) {
        return false;
    }

    int freshest_unix = 0;
    if (cJSON_IsNumber(time_position)) {
        freshest_unix = time_position->valueint;
    }
    if (cJSON_IsNumber(last_contact) && last_contact->valueint > freshest_unix) {
        freshest_unix = last_contact->valueint;
    }
    if (freshest_unix <= 0) {
        return true;
    }
    return (int)(now - freshest_unix) > AIRCRAFT_STALE_SECONDS;
}

static void insert_aircraft_candidate(app_aircraft_t *candidates,
                                      int *candidate_count,
                                      const app_aircraft_t *aircraft,
                                      float radius_miles)
{
    if (*candidate_count < AIRCRAFT_CANDIDATE_LIMIT) {
        candidates[(*candidate_count)++] = *aircraft;
        return;
    }

    // Protect the globally nearest half-screen before considering any airline
    // preference. The remaining candidate capacity carries the best balanced
    // targets into the final 16-aircraft selection.
    bool protected_nearest[AIRCRAFT_CANDIDATE_LIMIT] = {false};
    const int protected_count = APP_MAX_AIRCRAFT / 2;
    float farthest_protected_nm = 0.0f;
    for (int slot = 0; slot < protected_count; slot++) {
        int nearest_index = -1;
        for (int index = 0; index < AIRCRAFT_CANDIDATE_LIMIT; index++) {
            if (protected_nearest[index]) {
                continue;
            }
            if (nearest_index < 0 ||
                candidates[index].distance_nm < candidates[nearest_index].distance_nm) {
                nearest_index = index;
            }
        }
        if (nearest_index >= 0) {
            protected_nearest[nearest_index] = true;
            farthest_protected_nm = fmaxf(
                farthest_protected_nm,
                candidates[nearest_index].distance_nm);
        }
    }

    int lowest_priority_index = -1;
    float lowest_priority_score = -FLT_MAX;
    for (int index = 0; index < AIRCRAFT_CANDIDATE_LIMIT; index++) {
        if (protected_nearest[index]) {
            continue;
        }
        const float score = aircraft_selection_score(&candidates[index], radius_miles);
        if (lowest_priority_index < 0 || score > lowest_priority_score) {
            lowest_priority_score = score;
            lowest_priority_index = index;
        }
    }

    if (lowest_priority_index >= 0 &&
        (aircraft->distance_nm < farthest_protected_nm ||
         aircraft_selection_score(aircraft, radius_miles) < lowest_priority_score)) {
        candidates[lowest_priority_index] = *aircraft;
    }
}

static char *http_request_with_timeout(const char *url,
                                       const char *method,
                                       const char *request_body,
                                       const char *content_type,
                                       const char *bearer_token,
                                       size_t max_len,
                                       int *status_code,
                                       size_t *response_len,
                                       int timeout_ms)
{
    char *body = (char *)heap_caps_calloc(1, max_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        ESP_LOGW(TAG, "No PSRAM for HTTP response");
        return NULL;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
        .user_agent = CONFIG_AIRSPACE_HTTP_USER_AGENT,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        heap_caps_free(body);
        return NULL;
    }

    esp_http_client_set_header(client,
                               "Accept",
                               response_len != NULL
                                   ? "image/png"
                                   : "application/geo+json, application/json");
    if (content_type != NULL) {
        esp_http_client_set_header(client, "Content-Type", content_type);
    }
    char authorization[4096];
    if (bearer_token != NULL && bearer_token[0] != '\0') {
        snprintf(authorization, sizeof(authorization), "Bearer %s", bearer_token);
        esp_http_client_set_header(client, "Authorization", authorization);
    }

    if (method != NULL && strcmp(method, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
    }

    const int request_body_len = request_body != NULL ? strlen(request_body) : 0;
    esp_err_t err = esp_http_client_open(client, request_body_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP open failed for %s: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        heap_caps_free(body);
        return NULL;
    }

    if (request_body_len > 0) {
        const int written = esp_http_client_write(client, request_body, request_body_len);
        if (written < 0) {
            ESP_LOGW(TAG, "HTTP write failed for %s", url);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            heap_caps_free(body);
            return NULL;
        }
    }

    (void)esp_http_client_fetch_headers(client);

    int total = 0;
    while (total < (int)max_len) {
        int read = esp_http_client_read(client, body + total, (int)max_len - total);
        if (read <= 0) {
            break;
        }
        total += read;
    }
    body[total] = '\0';

    if (status_code != NULL) {
        *status_code = esp_http_client_get_status_code(client);
    }
    if (response_len != NULL) {
        *response_len = (size_t)total;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total == 0) {
        heap_caps_free(body);
        return NULL;
    }

    return body;
}

static char *http_request(const char *url,
                          const char *method,
                          const char *request_body,
                          const char *content_type,
                          const char *bearer_token,
                          size_t max_len,
                          int *status_code,
                          size_t *response_len)
{
    return http_request_with_timeout(url,
                                     method,
                                     request_body,
                                     content_type,
                                     bearer_token,
                                     max_len,
                                     status_code,
                                     response_len,
                                     12000);
}

static char *http_get(const char *url, size_t max_len, int *status_code)
{
    return http_request(url, "GET", NULL, NULL, NULL, max_len, status_code, NULL);
}

static char *http_get_fast(const char *url, size_t max_len, int *status_code)
{
    return http_request_with_timeout(url,
                                     "GET",
                                     NULL,
                                     NULL,
                                     NULL,
                                     max_len,
                                     status_code,
                                     NULL,
                                     3500);
}

static uint8_t *http_get_binary(const char *url, size_t max_len, int *status_code, size_t *response_len)
{
    return (uint8_t *)http_request(url,
                                  "GET",
                                  NULL,
                                  NULL,
                                  NULL,
                                  max_len,
                                  status_code,
                                  response_len);
}

static char *http_get_bearer(const char *url, const char *bearer_token, size_t max_len, int *status_code)
{
    return http_request(url, "GET", NULL, NULL, bearer_token, max_len, status_code, NULL);
}

static char *http_post_form(const char *url, const char *form_body, size_t max_len, int *status_code)
{
    return http_request(url,
                        "POST",
                        form_body,
                        "application/x-www-form-urlencoded",
                        NULL,
                        max_len,
                        status_code,
                        NULL);
}

static bool normalize_airline_iata(const char *iata, char normalized[4])
{
    size_t length = iata != NULL ? strlen(iata) : 0;
    if (length < 2 || length > 3) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        const unsigned char c = (unsigned char)iata[i];
        if (!isalnum(c)) {
            return false;
        }
        normalized[i] = (char)toupper(c);
    }
    normalized[length] = '\0';
    return true;
}

static bool text_contains_case_insensitive(const char *text, const char *needle)
{
    if (text == NULL || needle == NULL || needle[0] == '\0') {
        return false;
    }
    const size_t needle_length = strlen(needle);
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (strncasecmp(cursor, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

static void set_airline_logo_code(char code[4], const char *value)
{
    code[0] = (char)toupper((unsigned char)value[0]);
    code[1] = (char)toupper((unsigned char)value[1]);
    code[2] = value[2] != '\0' ? (char)toupper((unsigned char)value[2]) : '\0';
    code[3] = '\0';
}

bool app_data_airline_logo_code(const app_aircraft_t *aircraft, char code[4])
{
    if (code == NULL) {
        return false;
    }
    code[0] = '\0';
    if (aircraft == NULL) {
        return false;
    }

    /*
     * Prefer an explicit cargo brand when metadata supplies it. Amazon's
     * contracted flights retain the operating carrier unless the metadata
     * actually identifies Amazon/Prime Air.
     */
    static const struct {
        const char *name_fragment;
        const char *logo_code;
    } name_mappings[] = {
        {"AIR PREMIA", "YP"},
        {"AMAZON", "AMZ"},
        {"PRIME AIR", "AMZ"},
        {"FEDEX", "FDX"},
        {"UNITED PARCEL", "UPS"},
        {"UPS AIRLINES", "UPS"},
        {"DHL", "DHL"},
        {"CARGOLUX", "CLX"},
        {"ATLAS AIR", "GTI"},
        {"KOREAN AIR", "KE"},
        {"SKYWEST", "OO"},
    };
    for (size_t i = 0; i < sizeof(name_mappings) / sizeof(name_mappings[0]); i++) {
        if (text_contains_case_insensitive(aircraft->airline_name,
                                           name_mappings[i].name_fragment)) {
            set_airline_logo_code(code, name_mappings[i].logo_code);
            return true;
        }
    }

    static const struct {
        const char *icao;
        const char *logo_code;
    } operator_mappings[] = {
        {"APZ", "YP"},
        {"FDX", "FDX"},
        {"UPS", "UPS"},
        {"KAL", "KE"},
        {"CLX", "CLX"},
        {"GTI", "GTI"},
        {"BCS", "DHL"},
        {"DHK", "DHL"},
        {"DHL", "DHL"},
        {"SKW", "OO"},
    };
    for (size_t i = 0; i < sizeof(operator_mappings) / sizeof(operator_mappings[0]); i++) {
        if ((aircraft->airline_icao[0] != '\0' &&
             strcasecmp(aircraft->airline_icao, operator_mappings[i].icao) == 0) ||
            (aircraft->callsign[0] != '\0' &&
             strncasecmp(aircraft->callsign, operator_mappings[i].icao, 3) == 0)) {
            set_airline_logo_code(code, operator_mappings[i].logo_code);
            return true;
        }
    }

    static const struct {
        const char *iata;
        const char *logo_code;
    } iata_mappings[] = {
        {"FX", "FDX"},
        {"5X", "UPS"},
        {"CV", "CLX"},
        {"5Y", "GTI"},
        {"QY", "DHL"},
        {"D0", "DHL"},
        {"KE", "KE"},
    };
    for (size_t i = 0; i < sizeof(iata_mappings) / sizeof(iata_mappings[0]); i++) {
        if (aircraft->airline_iata[0] != '\0' &&
            strcasecmp(aircraft->airline_iata, iata_mappings[i].iata) == 0) {
            set_airline_logo_code(code, iata_mappings[i].logo_code);
            return true;
        }
    }

    char normalized[4] = {0};
    if (!normalize_airline_iata(aircraft->airline_iata, normalized)) {
        return false;
    }
    set_airline_logo_code(code, normalized);
    return true;
}

static bool airline_logo_png_path(const char *iata, char *path, size_t path_size)
{
    char normalized[4] = {0};
    if (!normalize_airline_iata(iata, normalized)) {
        return false;
    }
    snprintf(path, path_size, AIRLINE_LOGO_DIRECTORY "/%s.png", normalized);
    return true;
}

static bool valid_png_signature(const uint8_t *bytes, size_t length)
{
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    return bytes != NULL && length >= sizeof(signature) &&
           memcmp(bytes, signature, sizeof(signature)) == 0;
}

static bool airline_logo_cache_file_is_valid(const char *path)
{
    struct stat file_info;
    if (stat(path, &file_info) != 0 ||
        !S_ISREG(file_info.st_mode) ||
        file_info.st_size < 8 ||
        file_info.st_size > AIRLINE_LOGO_PNG_MAX) {
        return false;
    }
    uint8_t signature[8];
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    const size_t read = fread(signature, 1, sizeof(signature), file);
    fclose(file);
    return valid_png_signature(signature, read);
}

static bool airline_logo_cache_file_matches_bytes(const char *path,
                                                  const uint8_t *png_data,
                                                  size_t png_len)
{
    if (!valid_png_signature(png_data, png_len) || png_len > AIRLINE_LOGO_PNG_MAX) {
        return false;
    }

    struct stat file_info;
    if (stat(path, &file_info) != 0 ||
        !S_ISREG(file_info.st_mode) ||
        file_info.st_size != (off_t)png_len) {
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    uint8_t buffer[256];
    size_t offset = 0;
    while (offset < png_len) {
        size_t chunk = png_len - offset;
        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }
        const size_t read = fread(buffer, 1, chunk, file);
        if (read != chunk || memcmp(buffer, png_data + offset, chunk) != 0) {
            fclose(file);
            return false;
        }
        offset += chunk;
    }
    fclose(file);
    return true;
}

static void touch_airline_logo_file(const char *path)
{
    struct utimbuf timestamp = {
        .actime = time(NULL),
        .modtime = time(NULL),
    };
    (void)utime(path, &timestamp);
}

static void prune_airline_logo_cache(void)
{
    DIR *directory = opendir(AIRLINE_LOGO_DIRECTORY);
    if (directory == NULL) {
        return;
    }

    int count = 0;
    time_t oldest_time = 0;
    char oldest_path[96] = {0};
    struct dirent *entry = NULL;
    while ((entry = readdir(directory)) != NULL) {
        const size_t name_len = strlen(entry->d_name);
        if (name_len < 5 || strcasecmp(entry->d_name + name_len - 4, ".png") != 0) {
            continue;
        }
        char path[96];
        snprintf(path, sizeof(path), AIRLINE_LOGO_DIRECTORY "/%s", entry->d_name);
        struct stat file_info;
        if (stat(path, &file_info) != 0 || !S_ISREG(file_info.st_mode)) {
            continue;
        }
        count++;
        if (oldest_path[0] == '\0' || file_info.st_mtime < oldest_time) {
            oldest_time = file_info.st_mtime;
            copy_string(oldest_path, sizeof(oldest_path), path);
        }
    }
    closedir(directory);

    if (count >= AIRLINE_LOGO_CACHE_LIMIT && oldest_path[0] != '\0') {
        if (unlink(oldest_path) == 0) {
            ESP_LOGI(TAG, "Airline logo cache evicted %s", strrchr(oldest_path, '/') + 1);
        }
    }
}

static bool write_airline_logo_cache_file(const char *normalized,
                                          const char *path,
                                          const uint8_t *png_data,
                                          size_t png_len)
{
    if (!valid_png_signature(png_data, png_len) || png_len > AIRLINE_LOGO_PNG_MAX) {
        return false;
    }

    prune_airline_logo_cache();
    char temporary_path[96];
    snprintf(temporary_path, sizeof(temporary_path), AIRLINE_LOGO_DIRECTORY "/%s.tmp", normalized);
    (void)unlink(temporary_path);
    FILE *file = fopen(temporary_path, "wb");
    if (file == NULL) {
        ESP_LOGW(TAG,
                 "Could not open airline logo cache file for %s: errno=%d",
                 normalized,
                 errno);
        return false;
    }
    const size_t written = fwrite(png_data, 1, png_len, file);
    const int close_result = fclose(file);
    if (written != png_len || close_result != 0) {
        (void)unlink(temporary_path);
        ESP_LOGW(TAG, "Could not save airline logo for %s", normalized);
        return false;
    }
    (void)unlink(path);
    if (rename(temporary_path, path) != 0) {
        (void)unlink(temporary_path);
        ESP_LOGW(TAG, "Could not finalize airline logo for %s", normalized);
        return false;
    }
    touch_airline_logo_file(path);
    ESP_LOGI(TAG, "Cached airline logo %s (%u bytes)", normalized, (unsigned)png_len);
    return true;
}

static bool ensure_airline_logo_cached(const char *iata, char *path, size_t path_size)
{
    char normalized[4] = {0};
    if (!normalize_airline_iata(iata, normalized) ||
        !airline_logo_png_path(normalized, path, path_size)) {
        return false;
    }

    const app_embedded_airline_logo_t *embedded =
        app_embedded_airline_logo_for_code(normalized);

    if (airline_logo_cache_file_is_valid(path)) {
        if (embedded == NULL ||
            airline_logo_cache_file_matches_bytes(path,
                                                  embedded->png,
                                                  embedded->png_length)) {
            touch_airline_logo_file(path);
            return true;
        }
        ESP_LOGI(TAG, "Refreshing embedded airline logo cache for %s", normalized);
    }
    (void)unlink(path);

    if (mkdir(AIRLINE_LOGO_DIRECTORY, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "Airline logo storage is not ready");
        return false;
    }

    if (embedded != NULL) {
        const bool cached = write_airline_logo_cache_file(normalized,
                                                          path,
                                                          embedded->png,
                                                          embedded->png_length);
        if (cached) {
            ESP_LOGI(TAG, "Using embedded cargo logo for %s", normalized);
        }
        return cached;
    }

    char url[128];
    snprintf(url, sizeof(url), AIRLINE_LOGO_API_BASE "/%s.png", normalized);
    int status_code = 0;
    size_t png_len = 0;
    uint8_t *png_data =
        http_get_binary(url, AIRLINE_LOGO_PNG_MAX, &status_code, &png_len);
    if (png_data == NULL ||
        status_code < 200 ||
        status_code >= 300 ||
        !valid_png_signature(png_data, png_len)) {
        ESP_LOGW(TAG, "Airline logo unavailable for %s, code=%d", normalized, status_code);
        heap_caps_free(png_data);
        return false;
    }

    const bool cached = write_airline_logo_cache_file(normalized, path, png_data, png_len);
    heap_caps_free(png_data);
    return cached;
}

static bool decode_airline_logo_file(const char *iata, const char *path)
{
    struct stat file_info;
    if (stat(path, &file_info) != 0 ||
        file_info.st_size < 8 ||
        file_info.st_size > AIRLINE_LOGO_PNG_MAX) {
        return false;
    }

    const size_t png_len = (size_t)file_info.st_size;
    uint8_t *png_data =
        (uint8_t *)heap_caps_malloc(png_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (png_data == NULL) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL || fread(png_data, 1, png_len, file) != png_len) {
        if (file != NULL) {
            fclose(file);
        }
        heap_caps_free(png_data);
        return false;
    }
    fclose(file);

    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, png_data, png_len)) {
        heap_caps_free(png_data);
        return false;
    }
    image.format = PNG_FORMAT_RGBA;
    const size_t rgba_bytes = PNG_IMAGE_SIZE(image);
    if (image.width == 0 ||
        image.height == 0 ||
        image.width > AIRLINE_LOGO_MAX_DIM ||
        image.height > AIRLINE_LOGO_MAX_DIM ||
        rgba_bytes == 0 ||
        rgba_bytes > AIRLINE_LOGO_MAX_PIXELS * 4) {
        png_image_free(&image);
        heap_caps_free(png_data);
        return false;
    }

    uint8_t *rgba =
        (uint8_t *)heap_caps_malloc(rgba_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rgba == NULL || !png_image_finish_read(&image, NULL, rgba, 0, NULL)) {
        heap_caps_free(rgba);
        png_image_free(&image);
        heap_caps_free(png_data);
        return false;
    }
    heap_caps_free(png_data);

    const int target_buffer = airline_logo_active_buffer == 0 ? 1 : 0;
    if (airline_logo_buffers[target_buffer] == NULL) {
        airline_logo_buffers[target_buffer] = (uint8_t *)heap_caps_malloc(
            AIRLINE_LOGO_BUFFER_BYTES,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
    }
    uint8_t *output = airline_logo_buffers[target_buffer];
    if (output == NULL) {
        heap_caps_free(rgba);
        png_image_free(&image);
        return false;
    }

    const uint16_t decoded_width = (uint16_t)image.width;
    const uint16_t decoded_height = (uint16_t)image.height;
    const size_t pixels = (size_t)decoded_width * decoded_height;
    uint8_t *alpha_plane = output + pixels * 2;
    for (size_t i = 0; i < pixels; i++) {
        const uint8_t red = rgba[i * 4];
        const uint8_t green = rgba[i * 4 + 1];
        const uint8_t blue = rgba[i * 4 + 2];
        const uint16_t rgb565 =
            (uint16_t)(((uint16_t)(red >> 3) << 11) |
                       ((uint16_t)(green >> 2) << 5) |
                       (uint16_t)(blue >> 3));
        output[i * 2] = (uint8_t)(rgb565 & 0xff);
        output[i * 2 + 1] = (uint8_t)(rgb565 >> 8);
        alpha_plane[i] = rgba[i * 4 + 3];
    }
    heap_caps_free(rgba);
    png_image_free(&image);

    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    airline_logo_active_buffer = target_buffer;
    airline_logo_width = decoded_width;
    airline_logo_height = decoded_height;
    airline_logo_bytes = pixels * 3;
    copy_string(airline_logo_iata, sizeof(airline_logo_iata), iata);
    airline_logo_revision++;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }

    app_data_status_t next = read_status();
    const int now = (int)time(NULL);
    next.fetched_unix = now > next.fetched_unix ? now : next.fetched_unix + 1;
    write_status(&next);
    return true;
}

static void refresh_requested_airline_logo(void)
{
    char requested_iata[4] = {0};
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    const bool requested = airline_logo_requested;
    if (requested) {
        copy_string(requested_iata, sizeof(requested_iata), requested_airline_logo_iata);
        airline_logo_requested = false;
    }
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    if (!requested || requested_iata[0] == '\0') {
        return;
    }

    char path[96];
    if (ensure_airline_logo_cached(requested_iata, path, sizeof(path))) {
        if (!decode_airline_logo_file(requested_iata, path)) {
            ESP_LOGW(TAG, "Could not decode airline logo %s", requested_iata);
        }
    }
}

static const char *airport_display_code(const cJSON *airport)
{
    const char *iata = json_string(airport, "iata_code");
    if (iata != NULL && iata[0] != '\0') {
        return iata;
    }
    return json_string(airport, "icao_code");
}

static void clear_cached_route(aircraft_metadata_cache_t *entry)
{
    if (entry == NULL) {
        return;
    }
    entry->route_valid = false;
    entry->origin_iata[0] = '\0';
    entry->destination_iata[0] = '\0';
    entry->origin_city[0] = '\0';
    entry->destination_city[0] = '\0';
    entry->route_coordinates_valid = false;
    entry->origin_latitude = NAN;
    entry->origin_longitude = NAN;
    entry->destination_latitude = NAN;
    entry->destination_longitude = NAN;
}

static bool apply_adsbdb_route(const app_aircraft_t *aircraft,
                               aircraft_metadata_cache_t *entry,
                               const cJSON *route_json)
{
    if (aircraft == NULL || entry == NULL || !cJSON_IsObject(route_json)) {
        return false;
    }

    const cJSON *airline = object_item(route_json, "airline");
    const cJSON *origin = object_item(route_json, "origin");
    const cJSON *destination = object_item(route_json, "destination");
    copy_string(entry->airline_name, sizeof(entry->airline_name), json_string(airline, "name"));
    copy_string(entry->airline_iata, sizeof(entry->airline_iata), json_string(airline, "iata"));
    copy_string(entry->airline_icao, sizeof(entry->airline_icao), json_string(airline, "icao"));

    const char *origin_code = airport_display_code(origin);
    const char *destination_code = airport_display_code(destination);
    const char *origin_city = json_string(origin, "municipality");
    const char *destination_city = json_string(destination, "municipality");
    const double origin_lat = json_double(origin, "latitude", NAN);
    const double origin_lon = json_double(origin, "longitude", NAN);
    const double destination_lat = json_double(destination, "latitude", NAN);
    const double destination_lon = json_double(destination, "longitude", NAN);
    if (origin_code != NULL &&
        origin_code[0] != '\0' &&
        destination_code != NULL &&
        destination_code[0] != '\0' &&
        aircraft_route_is_plausible(aircraft,
                                    origin_lat,
                                    origin_lon,
                                    destination_lat,
                                    destination_lon)) {
        const airspace_route_direction_t direction =
            airspace_route_direction_resolve(
                aircraft->latitude,
                aircraft->longitude,
                aircraft->track_deg,
                aircraft->speed_kt,
                aircraft->track_valid,
                origin_lat,
                origin_lon,
                destination_lat,
                destination_lon
            );
        const bool reverse =
            direction == AIRSPACE_ROUTE_DIRECTION_REVERSED;
        copy_string(entry->origin_iata,
                    sizeof(entry->origin_iata),
                    reverse ? destination_code : origin_code);
        copy_string(entry->destination_iata,
                    sizeof(entry->destination_iata),
                    reverse ? origin_code : destination_code);
        copy_string(entry->origin_city,
                    sizeof(entry->origin_city),
                    reverse ? destination_city : origin_city);
        copy_string(entry->destination_city,
                    sizeof(entry->destination_city),
                    reverse ? origin_city : destination_city);
        entry->origin_latitude = reverse ? destination_lat : origin_lat;
        entry->origin_longitude = reverse ? destination_lon : origin_lon;
        entry->destination_latitude = reverse ? origin_lat : destination_lat;
        entry->destination_longitude = reverse ? origin_lon : destination_lon;
        entry->route_coordinates_valid = true;
        cache_airport_city(entry->origin_iata, entry->origin_city);
        cache_airport_city(entry->destination_iata, entry->destination_city);
        entry->route_valid = true;
        if (reverse) {
            ESP_LOGW(TAG,
                     "Corrected reversed route %s>%s to %s>%s for %s "
                     "using live track %d",
                     origin_code,
                     destination_code,
                     entry->origin_iata,
                     entry->destination_iata,
                     aircraft->callsign,
                     aircraft->track_deg);
        }
    } else if (origin_code != NULL && destination_code != NULL) {
        ESP_LOGW(TAG,
                 "Rejected implausible route %s-%s for %s at %.2f,%.2f",
                 origin_code,
                 destination_code,
                 aircraft->callsign,
                 (double)aircraft->latitude,
                 (double)aircraft->longitude);
    }
    return entry->airline_name[0] != '\0' ||
           entry->airline_iata[0] != '\0' ||
           entry->route_valid;
}

static bool fetch_adsbdb_callsign_fallback(const app_aircraft_t *aircraft,
                                           aircraft_metadata_cache_t *entry)
{
    if (aircraft == NULL || entry == NULL || aircraft->callsign[0] == '\0') {
        return false;
    }

    char encoded_callsign[40];
    if (!url_encode_component(aircraft->callsign, encoded_callsign, sizeof(encoded_callsign))) {
        return false;
    }
    char url[160];
    snprintf(url, sizeof(url), ADSBDB_API_BASE "/callsign/%s", encoded_callsign);

    int code = 0;
    char *body = http_get_fast(url, FETCH_MEDIUM_MAX, &code);
    if (body == NULL || code < 200 || code >= 300) {
        heap_caps_free(body);
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    const cJSON *response = object_item(root, "response");
    const bool any =
        apply_adsbdb_route(aircraft, entry, object_item(response, "flightroute"));
    cJSON_Delete(root);
    if (any) {
        ESP_LOGI(TAG,
                 "ADSBDB callsign fallback %s: airline=%s route=%s%s%s",
                 aircraft->callsign,
                 entry->airline_iata[0] ? entry->airline_iata : "--",
                 entry->route_valid ? entry->origin_iata : "--",
                 entry->route_valid ? ">" : "",
                 entry->route_valid ? entry->destination_iata : "");
    }
    return any;
}

static bool airlabs_fallback_is_fresh(const app_aircraft_t *aircraft,
                                      const aircraft_metadata_cache_t *entry)
{
    if (aircraft == NULL ||
        entry == NULL ||
        entry->airlabs_attempted_unix <= 0 ||
        strcmp(entry->airlabs_callsign, aircraft->callsign) != 0) {
        return false;
    }
    const int cache_seconds =
        entry->airlabs_valid ? AIRLABS_SUCCESS_CACHE_SECONDS : AIRLABS_FAILURE_RETRY_SECONDS;
    return (int)time(NULL) - entry->airlabs_attempted_unix < cache_seconds;
}

static int airlabs_result_match_score(const cJSON *flight,
                                      const app_aircraft_t *aircraft,
                                      const aircraft_metadata_cache_t *entry,
                                      float *position_difference_nm)
{
    if (!cJSON_IsObject(flight) || aircraft == NULL) {
        return 0;
    }

    const int updated = json_int(flight, "updated", 0);
    const int now = (int)time(NULL);
    if (updated > 0 &&
        now >= AIRCRAFT_REASONABLE_UNIX_TIME &&
        (updated > now + 300 || now - updated > AIRLABS_MAX_POSITION_AGE_SECONDS)) {
        return 0;
    }

    const char *hex = json_string(flight, "hex");
    const bool exact_hex =
        hex != NULL && hex[0] != '\0' && strcasecmp(hex, aircraft->icao24) == 0;
    /* A Radar detail request identifies a physical transponder, not merely a
     * flight number.  Callsigns are reused and can be marketing/codeshare
     * identifiers, so they may increase confidence but can never establish
     * identity on their own. */
    if (!exact_hex) {
        return 0;
    }

    const char *registration = json_string(flight, "reg_number");
    if (entry != NULL &&
        entry->registration[0] != '\0' &&
        registration != NULL &&
        registration[0] != '\0' &&
        strcasecmp(entry->registration, registration) != 0) {
        return 0;
    }

    const double lat = json_double(flight, "lat", NAN);
    const double lon = json_double(flight, "lng", NAN);
    if (isfinite(lat) && isfinite(lon)) {
        const float difference =
            distance_nm(aircraft->latitude, aircraft->longitude, lat, lon);
        if (position_difference_nm != NULL) {
            *position_difference_nm = difference;
        }
        if (difference > AIRLABS_MAX_POSITION_DIFFERENCE_NM) {
            return 0;
        }
        const char *flight_icao = json_string(flight, "flight_icao");
        const bool callsign_matches =
            flight_icao != NULL &&
            flight_icao[0] != '\0' &&
            strcasecmp(flight_icao, aircraft->callsign) == 0;
        return 4 + (callsign_matches ? 1 : 0);
    }

    return 4;
}

static airlabs_enrich_result_t fetch_airlabs_selected_aircraft(
    const app_aircraft_t *aircraft,
    aircraft_metadata_cache_t *entry,
    const app_config_t *cfg)
{
    if (aircraft == NULL ||
        entry == NULL ||
        cfg == NULL ||
        cfg->airlabs_api_key[0] == '\0' ||
        aircraft->icao24[0] == '\0') {
        return AIRLABS_ENRICH_UNAVAILABLE;
    }
    if (airlabs_fallback_is_fresh(aircraft, entry)) {
        if (!entry->airlabs_query_succeeded) {
            return AIRLABS_ENRICH_UNAVAILABLE;
        }
        return entry->airlabs_valid
            ? AIRLABS_ENRICH_MATCHED
            : AIRLABS_ENRICH_NO_IDENTITY_MATCH;
    }

    entry->airlabs_attempted_unix = (int)time(NULL);
    entry->airlabs_valid = false;
    entry->airlabs_query_succeeded = false;
    entry->airlabs_match_icao24[0] = '\0';
    entry->airlabs_match_registration[0] = '\0';
    entry->airlabs_updated_unix = 0;
    copy_string(entry->airlabs_callsign, sizeof(entry->airlabs_callsign), aircraft->callsign);

    char encoded_icao24[24];
    char encoded_key[256];
    if (!url_encode_component(aircraft->icao24, encoded_icao24, sizeof(encoded_icao24)) ||
        !url_encode_component(cfg->airlabs_api_key, encoded_key, sizeof(encoded_key))) {
        return AIRLABS_ENRICH_UNAVAILABLE;
    }

    char url[768];
    snprintf(
        url,
        sizeof(url),
        AIRLABS_API_BASE "/flights?hex=%s"
        "&_fields=hex,reg_number,lat,lng,airline_icao,airline_iata,aircraft_icao,"
        "flight_icao,flight_iata,dep_iata,arr_iata,updated,status,"
        "model,manufacturer,engine,engine_count,type&api_key=%s",
        encoded_icao24,
        encoded_key
    );

    int code = 0;
    char *body = http_get_fast(url, FETCH_MEDIUM_MAX, &code);
    if (body == NULL || code < 200 || code >= 300) {
        ESP_LOGW(TAG,
                 "AirLabs selected-aircraft lookup failed for %s/%s, code=%d",
                 aircraft->icao24,
                 aircraft->callsign,
                 code);
        heap_caps_free(body);
        return AIRLABS_ENRICH_UNAVAILABLE;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    if (root == NULL) {
        return AIRLABS_ENRICH_UNAVAILABLE;
    }
    entry->airlabs_query_succeeded = true;
    const cJSON *flights = cJSON_IsArray(root) ? root : object_item(root, "response");
    if (!cJSON_IsArray(flights) || cJSON_GetArraySize(flights) == 0) {
        /* An empty AirLabs response is absence of enrichment, not evidence that
         * the ICAO24-validated ADSBDB route belongs to another aircraft.  Keep
         * that route and retry AirLabs on the normal failure interval. */
        entry->airlabs_query_succeeded = false;
        cJSON_Delete(root);
        ESP_LOGI(TAG,
                 "AirLabs returned no rows for selected aircraft %s/%s; "
                 "keeping existing route",
                 aircraft->icao24,
                 aircraft->callsign);
        return AIRLABS_ENRICH_UNAVAILABLE;
    }
    const cJSON *best = NULL;
    int best_score = 0;
    float best_difference = INFINITY;
    const cJSON *flight = NULL;
    cJSON_ArrayForEach(flight, flights) {
        float difference = INFINITY;
        const int score = airlabs_result_match_score(flight, aircraft, entry, &difference);
        if (score > best_score || (score == best_score && difference < best_difference)) {
            best = flight;
            best_score = score;
            best_difference = difference;
        }
    }

    if (best != NULL) {
        /* From this point the physical aircraft identity is confirmed by
         * ICAO24.  Do not retain an older callsign-only route if the current
         * AirLabs row omits route fields. */
        clear_cached_route(entry);
        const char *matched_hex = json_string(best, "hex");
        const char *matched_registration = json_string(best, "reg_number");
        copy_string(entry->airlabs_match_icao24,
                    sizeof(entry->airlabs_match_icao24),
                    matched_hex);
        copy_string(entry->airlabs_match_registration,
                    sizeof(entry->airlabs_match_registration),
                    matched_registration);
        entry->airlabs_updated_unix = json_int(best, "updated", 0);
        const char *airline_iata = json_string(best, "airline_iata");
        const char *airline_icao = json_string(best, "airline_icao");
        const char *origin = json_string(best, "dep_iata");
        const char *destination = json_string(best, "arr_iata");
        const char *typecode = json_string(best, "aircraft_icao");
        const char *model = json_string(best, "model");
        const char *manufacturer = json_string(best, "manufacturer");
        const char *engine = json_string(best, "engine");
        const char *registration = json_string(best, "reg_number");
        if (airline_iata != NULL && airline_iata[0] != '\0') {
            copy_string(entry->airline_iata, sizeof(entry->airline_iata), airline_iata);
        }
        if (airline_icao != NULL && airline_icao[0] != '\0') {
            copy_string(entry->airline_icao, sizeof(entry->airline_icao), airline_icao);
        }
        if (origin != NULL &&
            origin[0] != '\0' &&
            destination != NULL &&
            destination[0] != '\0') {
            copy_string(entry->origin_iata, sizeof(entry->origin_iata), origin);
            copy_string(entry->destination_iata, sizeof(entry->destination_iata), destination);
            entry->route_valid = true;
        }
        if (entry->typecode[0] == '\0' && typecode != NULL && typecode[0] != '\0') {
            copy_icao_type_designator(entry->typecode, sizeof(entry->typecode), typecode);
            entry->type_source = APP_AIRCRAFT_TYPE_SOURCE_AIRLABS;
        }
        if (entry->model[0] == '\0' && model != NULL && model[0] != '\0') {
            copy_string(entry->model, sizeof(entry->model), model);
        }
        if (entry->manufacturer[0] == '\0' &&
            manufacturer != NULL &&
            manufacturer[0] != '\0') {
            copy_string(entry->manufacturer, sizeof(entry->manufacturer), manufacturer);
        }
        if (engine != NULL && engine[0] != '\0') {
            copy_string(entry->engine_type, sizeof(entry->engine_type), engine);
            entry->engine_count = json_int(best, "engine_count", entry->engine_count);
        }
        if (registration != NULL && registration[0] != '\0') {
            copy_string(entry->registration, sizeof(entry->registration), registration);
        }
        entry->metadata_valid =
            entry->metadata_valid ||
            entry->typecode[0] != '\0' ||
            entry->model[0] != '\0' ||
            entry->registration[0] != '\0';
    }
    entry->airlabs_valid = best != NULL;
    cJSON_Delete(root);

    if (best != NULL) {
        ESP_LOGI(TAG,
                 "AirLabs selected aircraft %s/%s: type=%s model=%s engine=%s/%d "
                 "route=%s%s%s match=%d",
                 aircraft->icao24,
                 aircraft->callsign,
                 entry->typecode[0] ? entry->typecode : "--",
                 entry->model[0] ? entry->model : "--",
                 entry->engine_type[0] ? entry->engine_type : "--",
                 entry->engine_count,
                 entry->route_valid ? entry->origin_iata : "--",
                 entry->route_valid ? ">" : "",
                 entry->route_valid ? entry->destination_iata : "",
                 best_score);
    } else {
        ESP_LOGW(TAG,
                 "AirLabs returned no ICAO24 match for selected aircraft %s/%s",
                 aircraft->icao24,
                 aircraft->callsign);
    }
    return best != NULL
        ? AIRLABS_ENRICH_MATCHED
        : AIRLABS_ENRICH_NO_IDENTITY_MATCH;
}

static bool fetch_airlabs_airport_city(const char *airport_code,
                                       const app_config_t *cfg,
                                       char *city,
                                       size_t city_size,
                                       double *latitude,
                                       double *longitude)
{
    if (city == NULL || city_size == 0) {
        return false;
    }
    city[0] = '\0';
    if (latitude != NULL) {
        *latitude = NAN;
    }
    if (longitude != NULL) {
        *longitude = NAN;
    }
    if (airport_code == NULL ||
        airport_code[0] == '\0' ||
        cfg == NULL ||
        cfg->airlabs_api_key[0] == '\0') {
        return false;
    }

    char encoded_code[24];
    char encoded_key[256];
    if (!url_encode_component(airport_code,
                              encoded_code,
                              sizeof(encoded_code)) ||
        !url_encode_component(cfg->airlabs_api_key,
                              encoded_key,
                              sizeof(encoded_key))) {
        return false;
    }

    const char *filter = strlen(airport_code) == 3
        ? "iata_code"
        : "icao_code";
    char url[512];
    snprintf(url,
             sizeof(url),
             AIRLABS_API_BASE "/airports?%s=%s&_fields=city,lat,lng&api_key=%s",
             filter,
             encoded_code,
             encoded_key);

    int code = 0;
    char *body = http_get_fast(url, FETCH_SMALL_MAX, &code);
    if (body == NULL || code < 200 || code >= 300) {
        heap_caps_free(body);
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    const cJSON *airports = cJSON_IsArray(root)
        ? root
        : object_item(root, "response");
    const cJSON *airport = cJSON_IsArray(airports)
        ? cJSON_GetArrayItem(airports, 0)
        : NULL;
    copy_string(city, city_size, json_string(airport, "city"));
    const double resolved_latitude = json_double(airport, "lat", NAN);
    const double resolved_longitude = json_double(airport, "lng", NAN);
    if (latitude != NULL) {
        *latitude = resolved_latitude;
    }
    if (longitude != NULL) {
        *longitude = resolved_longitude;
    }
    cJSON_Delete(root);
    if (city[0] != '\0') {
        cache_airport_city(airport_code, city);
        return true;
    }
    return isfinite(resolved_latitude) && isfinite(resolved_longitude);
}

static void fill_missing_route_cities(const app_aircraft_t *aircraft,
                                      aircraft_metadata_cache_t *entry,
                                      const app_config_t *cfg)
{
    if (entry == NULL || !entry->route_valid) {
        return;
    }

    if (entry->origin_city[0] == '\0') {
        if (!app_data_get_airport_city(entry->origin_iata,
                                       entry->origin_city,
                                       sizeof(entry->origin_city))) {
            fetch_airlabs_airport_city(entry->origin_iata,
                                       cfg,
                                       entry->origin_city,
                                       sizeof(entry->origin_city),
                                       &entry->origin_latitude,
                                       &entry->origin_longitude);
        }
    }
    if (entry->destination_city[0] == '\0') {
        if (!app_data_get_airport_city(entry->destination_iata,
                                       entry->destination_city,
                                       sizeof(entry->destination_city))) {
            fetch_airlabs_airport_city(entry->destination_iata,
                                       cfg,
                                       entry->destination_city,
                                       sizeof(entry->destination_city),
                                       &entry->destination_latitude,
                                       &entry->destination_longitude);
        }
    }

    if (!entry->route_coordinates_valid) {
        if (!isfinite(entry->origin_latitude) ||
            !isfinite(entry->origin_longitude)) {
            char resolved_city[sizeof(entry->origin_city)] = {0};
            fetch_airlabs_airport_city(entry->origin_iata,
                                       cfg,
                                       resolved_city,
                                       sizeof(resolved_city),
                                       &entry->origin_latitude,
                                       &entry->origin_longitude);
            if (entry->origin_city[0] == '\0') {
                copy_string(entry->origin_city,
                            sizeof(entry->origin_city),
                            resolved_city);
            }
        }
        if (!isfinite(entry->destination_latitude) ||
            !isfinite(entry->destination_longitude)) {
            char resolved_city[sizeof(entry->destination_city)] = {0};
            fetch_airlabs_airport_city(entry->destination_iata,
                                       cfg,
                                       resolved_city,
                                       sizeof(resolved_city),
                                       &entry->destination_latitude,
                                       &entry->destination_longitude);
            if (entry->destination_city[0] == '\0') {
                copy_string(entry->destination_city,
                            sizeof(entry->destination_city),
                            resolved_city);
            }
        }
        entry->route_coordinates_valid =
            aircraft_route_is_plausible(aircraft,
                                        entry->origin_latitude,
                                        entry->origin_longitude,
                                        entry->destination_latitude,
                                        entry->destination_longitude);
    }
}

static bool fetch_aircraft_metadata(const app_aircraft_t *aircraft)
{
    if (aircraft == NULL || aircraft->icao24[0] == '\0') {
        return false;
    }

    aircraft_metadata_cache_t *entry = metadata_cache_slot(aircraft->icao24);
    if (entry == NULL) {
        return false;
    }

    if (strcmp(entry->airlabs_callsign, aircraft->callsign) != 0) {
        entry->airlabs_attempted_unix = 0;
        entry->airlabs_valid = false;
        entry->airlabs_query_succeeded = false;
        entry->airlabs_callsign[0] = '\0';
        entry->airlabs_match_icao24[0] = '\0';
        entry->airlabs_match_registration[0] = '\0';
        entry->airlabs_updated_unix = 0;
    }
    entry->attempted_unix = (int)time(NULL);
    copy_string(entry->route_callsign, sizeof(entry->route_callsign), aircraft->callsign);
    clear_cached_route(entry);
    entry->airline_name[0] = '\0';
    entry->airline_iata[0] = '\0';
    entry->airline_icao[0] = '\0';
    char url[192];
    if (aircraft->callsign[0] != '\0') {
        snprintf(url,
                 sizeof(url),
                 ADSBDB_API_BASE "/aircraft/%s?callsign=%s",
                 aircraft->icao24,
                 aircraft->callsign);
    } else {
        snprintf(url, sizeof(url), ADSBDB_API_BASE "/aircraft/%s", aircraft->icao24);
    }

    int code = 0;
    char *body = http_get_fast(url, FETCH_MEDIUM_MAX, &code);
    if (body == NULL || code < 200 || code >= 300) {
        ESP_LOGW(TAG,
                 "ADSBDB metadata failed for %s/%s, code=%d",
                 aircraft->icao24,
                 aircraft->callsign,
                 code);
        heap_caps_free(body);
        return fetch_adsbdb_callsign_fallback(aircraft, entry);
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    const cJSON *response = object_item(root, "response");
    const cJSON *aircraft_json = object_item(response, "aircraft");
    const cJSON *route_json = object_item(response, "flightroute");
    bool any = false;

    if (cJSON_IsObject(aircraft_json)) {
        const char *typecode = json_string(aircraft_json, "icao_type");
        const char *model = json_string(aircraft_json, "type");
        if (typecode != NULL && typecode[0] != '\0') {
            copy_icao_type_designator(entry->typecode, sizeof(entry->typecode), typecode);
            entry->type_source = APP_AIRCRAFT_TYPE_SOURCE_ADSBDB;
        }
        if (model != NULL && model[0] != '\0') {
            copy_string(entry->model, sizeof(entry->model), model);
        }
        copy_string(entry->registration,
                    sizeof(entry->registration),
                    json_string(aircraft_json, "registration"));
        copy_string(entry->manufacturer,
                    sizeof(entry->manufacturer),
                    json_string(aircraft_json, "manufacturer"));
        entry->metadata_valid =
            entry->typecode[0] != '\0' ||
            entry->model[0] != '\0' ||
            entry->registration[0] != '\0';
        any |= entry->metadata_valid;
    }

    any |= apply_adsbdb_route(aircraft, entry, route_json);

    cJSON_Delete(root);
    ESP_LOGI(TAG,
             "ADSBDB %s: type=%s model=%s route=%s%s%s",
             aircraft->callsign,
             entry->typecode[0] ? entry->typecode : "--",
             entry->model[0] ? entry->model : "--",
             entry->route_valid ? entry->origin_iata : "--",
             entry->route_valid ? ">" : "",
             entry->route_valid ? entry->destination_iata : "");
    return any;
}

static bool publish_metadata_cache(void)
{
    app_data_status_t next = read_status();
    bool changed = false;
    for (int i = 0; i < next.aircraft_count && i < APP_MAX_AIRCRAFT; i++) {
        app_aircraft_t enriched = next.aircraft[i];
        apply_metadata_cache(&enriched);
        if (memcmp(&enriched, &next.aircraft[i], sizeof(enriched)) != 0) {
            next.aircraft[i] = enriched;
            changed = true;
        }
    }
    if (!changed) {
        return false;
    }

    const int now = (int)time(NULL);
    next.fetched_unix = now > next.fetched_unix ? now : next.fetched_unix + 1;
    write_status(&next);
    return true;
}

static void refresh_aircraft_metadata_background(void)
{
    app_data_status_t current = read_status();
    int refreshed = 0;
    for (int i = 0;
         i < current.aircraft_count &&
         i < APP_MAX_AIRCRAFT &&
         refreshed < AIRCRAFT_METADATA_BACKGROUND_PER_CYCLE;
         i++) {
        if (!current.aircraft[i].valid || metadata_cache_is_fresh(&current.aircraft[i])) {
            continue;
        }
        fetch_aircraft_metadata(&current.aircraft[i]);
        refreshed++;
    }
    if (refreshed > 0) {
        publish_metadata_cache();
    }
}

static void finish_requested_aircraft_metadata(void)
{
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    aircraft_enrichment_active = false;
    active_aircraft_icao24[0] = '\0';
    const int now = (int)time(NULL);
    status.fetched_unix =
        now > status.fetched_unix ? now : status.fetched_unix + 1;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
}

static void refresh_requested_aircraft_metadata(void)
{
    char requested_icao24[8] = {0};
    char requested_callsign[12] = {0};
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    const bool requested = aircraft_enrichment_requested;
    if (requested) {
        copy_string(requested_icao24, sizeof(requested_icao24), requested_aircraft_icao24);
        copy_string(requested_callsign, sizeof(requested_callsign), requested_aircraft_callsign);
        aircraft_enrichment_requested = false;
        aircraft_enrichment_active = true;
        copy_string(active_aircraft_icao24,
                    sizeof(active_aircraft_icao24),
                    requested_icao24);
    }
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    if (!requested || requested_icao24[0] == '\0') {
        return;
    }

    app_data_status_t current = read_status();
    for (int i = 0; i < current.aircraft_count && i < APP_MAX_AIRCRAFT; i++) {
        if (strcasecmp(current.aircraft[i].icao24, requested_icao24) != 0) {
            continue;
        }
        if (requested_callsign[0] != '\0') {
            copy_string(current.aircraft[i].callsign,
                        sizeof(current.aircraft[i].callsign),
                        requested_callsign);
        }
        if (!metadata_cache_is_fresh(&current.aircraft[i])) {
            fetch_aircraft_metadata(&current.aircraft[i]);
            /* Publish ADSBDB data immediately.  Route/city/type fallbacks can
             * continue without making the detail page wait for every source. */
            publish_metadata_cache();
        }
        aircraft_metadata_cache_t *entry =
            metadata_cache_find(current.aircraft[i].icao24);
        app_config_t cfg;
        app_config_get(&cfg);
        if (entry != NULL) {
            /* Radar details are a physical-aircraft lookup.  Always give
             * AirLabs the opportunity to validate the selected OpenSky
             * ICAO24, even when ADSBDB supplied a callsign route. */
            const airlabs_enrich_result_t result =
                fetch_airlabs_selected_aircraft(&current.aircraft[i], entry, &cfg);
            if (result == AIRLABS_ENRICH_NO_IDENTITY_MATCH) {
                /* A successful live response without this ICAO24 means the
                 * callsign route is ambiguous/stale.  Showing no route is
                 * safer than attaching another UA582 occurrence. */
                clear_cached_route(entry);
            }
            publish_metadata_cache();
        }
        fill_missing_route_cities(&current.aircraft[i], entry, &cfg);
        publish_metadata_cache();
        finish_requested_aircraft_metadata();
        return;
    }
    finish_requested_aircraft_metadata();
}

static bool is_form_safe_char(char c)
{
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '-' ||
           c == '_' ||
           c == '.' ||
           c == '~';
}

static bool url_encode_component(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t out = 0;
    for (size_t i = 0; src != NULL && src[i] != '\0'; i++) {
        const unsigned char c = (unsigned char)src[i];
        if (is_form_safe_char((char)c)) {
            if (out + 1 >= dst_size) {
                return false;
            }
            dst[out++] = (char)c;
        } else {
            if (out + 3 >= dst_size) {
                return false;
            }
            dst[out++] = '%';
            dst[out++] = hex[(c >> 4) & 0x0f];
            dst[out++] = hex[c & 0x0f];
        }
    }
    if (out >= dst_size) {
        return false;
    }
    dst[out] = '\0';
    return true;
}

static bool refresh_opensky_token(const app_config_t *cfg)
{
    char encoded_client_id[300];
    char encoded_client_secret[300];
    if (!url_encode_component(cfg->opensky_client_id, encoded_client_id, sizeof(encoded_client_id)) ||
        !url_encode_component(cfg->opensky_client_secret, encoded_client_secret, sizeof(encoded_client_secret))) {
        ESP_LOGW(TAG, "OpenSky credentials too long to encode");
        return false;
    }

    char form[680];
    snprintf(form,
             sizeof(form),
             "grant_type=client_credentials&client_id=%s&client_secret=%s",
             encoded_client_id,
             encoded_client_secret);

    int code = 0;
    char *body = http_post_form(OPENSKY_TOKEN_URL, form, FETCH_SMALL_MAX, &code);
    if (body == NULL || code < 200 || code >= 300) {
        ESP_LOGW(TAG, "OpenSky token refresh failed, code=%d", code);
        heap_caps_free(body);
        opensky_access_token[0] = '\0';
        opensky_token_expires_unix = 0;
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    const char *access_token = json_string(root, "access_token");
    const int expires_in = json_int(root, "expires_in", 1800);
    if (access_token == NULL || access_token[0] == '\0') {
        cJSON_Delete(root);
        return false;
    }

    copy_string(opensky_access_token, sizeof(opensky_access_token), access_token);
    opensky_token_expires_unix = (int)time(NULL) + expires_in - OPENSKY_TOKEN_REFRESH_MARGIN_SECONDS;
    cJSON_Delete(root);
    ESP_LOGI(TAG, "OpenSky token refreshed");
    return true;
}

static const char *opensky_bearer_token_if_available(const app_config_t *cfg)
{
    if (cfg->opensky_client_id[0] == '\0' || cfg->opensky_client_secret[0] == '\0') {
        return NULL;
    }

    const int now = (int)time(NULL);
    if (opensky_access_token[0] != '\0' && now < opensky_token_expires_unix) {
        return opensky_access_token;
    }

    return refresh_opensky_token(cfg) ? opensky_access_token : NULL;
}

cJSON *app_data_tracker_opensky(const char *query, int *code)
{
    *code = 0;
    if (time(NULL) < opensky_rate_limited_until_unix) {
        *code = 429;
        return NULL;
    }
    app_config_t cfg;
    app_config_get(&cfg);
    char url[320];
    snprintf(url, sizeof(url), "https://opensky-network.org/api/states/all?%s", query);
    const char *token = opensky_bearer_token_if_available(&cfg);
    char *body = http_get_bearer(url, token, FETCH_AIRCRAFT_MAX, code);
    if (*code == 401) {
        heap_caps_free(body);
        opensky_access_token[0] = '\0';
        opensky_token_expires_unix = 0;
        token = opensky_bearer_token_if_available(&cfg);
        body = token ? http_get_bearer(url, token, FETCH_AIRCRAFT_MAX, code) : NULL;
    }
    if (*code == 429) {
        opensky_rate_limited_until_unix = time(NULL) + AIRCRAFT_RATE_LIMIT_RETRY_MS / 1000;
        opensky_rate_limit_was_authenticated = token != NULL;
    }
    cJSON *root = body && *code == 200 ? cJSON_Parse(body) : NULL;
    heap_caps_free(body);
    return root;
}

typedef enum {
    API_HEALTH_PROVIDER_FLIGHTAWARE = 0,
    API_HEALTH_PROVIDER_OPENSKY,
    API_HEALTH_PROVIDER_AIRLABS,
} api_health_provider_t;

static void publish_api_health(api_health_provider_t provider,
                               app_api_health_state_t health)
{
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    switch (provider) {
    case API_HEALTH_PROVIDER_FLIGHTAWARE:
        status.flightaware_api_health = health;
        break;
    case API_HEALTH_PROVIDER_OPENSKY:
        status.opensky_api_health = health;
        break;
    case API_HEALTH_PROVIDER_AIRLABS:
        status.airlabs_api_health = health;
        break;
    }
    status.api_health_revision++;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
}

static bool validate_airlabs_credentials(const app_config_t *cfg)
{
    if (cfg == NULL || cfg->airlabs_api_key[0] == '\0') {
        return false;
    }

    char encoded_key[384];
    if (!url_encode_component(
            cfg->airlabs_api_key,
            encoded_key,
            sizeof(encoded_key))) {
        return false;
    }
    char form[416];
    snprintf(form, sizeof(form), "api_key=%s", encoded_key);

    int code = 0;
    char *body = http_post_form(
        AIRLABS_API_BASE "/ping",
        form,
        FETCH_SMALL_MAX,
        &code);
    if (body == NULL || code < 200 || code >= 300) {
        heap_caps_free(body);
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    const cJSON *error = object_item(root, "error");
    const bool valid =
        cJSON_IsObject(root) &&
        (error == NULL || cJSON_IsNull(error));
    cJSON_Delete(root);
    return valid;
}

static void validate_api_credentials(void)
{
    app_config_t cfg;
    app_config_get(&cfg);

    /* Validate the provisioned key on the device using the same no-cost
     * authorization probe as the companion. */
    publish_api_health(
        API_HEALTH_PROVIDER_FLIGHTAWARE,
        app_tracker_validate_flightaware()
            ? APP_API_HEALTH_VALID
            : APP_API_HEALTH_INVALID);

    const bool opensky_valid =
        cfg.opensky_enabled &&
        cfg.opensky_client_id[0] != '\0' &&
        cfg.opensky_client_secret[0] != '\0' &&
        refresh_opensky_token(&cfg);
    publish_api_health(
        API_HEALTH_PROVIDER_OPENSKY,
        opensky_valid
            ? APP_API_HEALTH_VALID
            : APP_API_HEALTH_INVALID);

    publish_api_health(
        API_HEALTH_PROVIDER_AIRLABS,
        validate_airlabs_credentials(&cfg)
            ? APP_API_HEALTH_VALID
            : APP_API_HEALTH_INVALID);
}

static void resolve_api_credentials_offline(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    publish_api_health(
        API_HEALTH_PROVIDER_FLIGHTAWARE,
        cfg.flightaware_available
            ? APP_API_HEALTH_VALID
            : APP_API_HEALTH_INVALID);
    publish_api_health(
        API_HEALTH_PROVIDER_OPENSKY,
        APP_API_HEALTH_INVALID);
    publish_api_health(
        API_HEALTH_PROVIDER_AIRLABS,
        APP_API_HEALTH_INVALID);
}

static const char *weather_code_summary(int code)
{
    if (code == 0) {
        return "Clear";
    }
    if (code == 1) {
        return "Mostly clear";
    }
    if (code == 2) {
        return "Partly cloudy";
    }
    if (code == 3) {
        return "Overcast";
    }
    if (code == 45 || code == 48) {
        return "Fog";
    }
    if (code >= 51 && code <= 57) {
        return "Drizzle";
    }
    if (code >= 61 && code <= 67) {
        return "Rain";
    }
    if (code >= 71 && code <= 77) {
        return "Snow";
    }
    if (code >= 80 && code <= 82) {
        return "Rain showers";
    }
    if (code >= 85 && code <= 86) {
        return "Snow showers";
    }
    if (code >= 95) {
        return "Thunderstorms";
    }
    return "Current conditions";
}

static const char *wind_cardinal(double degrees)
{
    static const char *directions[] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"
    };
    int index = ((int)lround(degrees / 45.0)) % 8;
    if (index < 0) {
        index += 8;
    }
    return directions[index];
}

static void weather_forecast_day_name(const char *date, char output[4])
{
    static const char *days[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
    };
    int year = 0;
    int month = 0;
    int day = 0;
    if (date == NULL ||
        sscanf(date, "%d-%d-%d", &year, &month, &day) != 3) {
        copy_string(output, 4, "—");
        return;
    }
    struct tm value = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = 12,
        .tm_isdst = -1,
    };
    if (mktime(&value) < 0 || value.tm_wday < 0 || value.tm_wday > 6) {
        copy_string(output, 4, "—");
        return;
    }
    copy_string(output, 4, days[value.tm_wday]);
}

static bool fetch_weather_current(app_data_status_t *next, const app_config_t *cfg)
{
    char url[768];
    snprintf(url,
             sizeof(url),
             "https://api.open-meteo.com/v1/forecast"
             "?latitude=%s&longitude=%s"
             "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
             "precipitation,weather_code,pressure_msl,wind_speed_10m,wind_direction_10m"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
             "precipitation_probability_max,sunrise,sunset"
             "&forecast_days=5&timezone=auto"
             "&temperature_unit=celsius&wind_speed_unit=kn",
             cfg->location_lat,
             cfg->location_lon);

    int code = 0;
    char *body = http_get(url, FETCH_MEDIUM_MAX, &code);
    if (body == NULL || code < 200 || code >= 300) {
        ESP_LOGW(TAG, "Open-Meteo weather failed, code=%d", code);
        heap_caps_free(body);
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    const cJSON *current = object_item(root, "current");
    if (!cJSON_IsObject(current)) {
        cJSON_Delete(root);
        return false;
    }

    const double temperature = json_double(current, "temperature_2m", NAN);
    const double apparent_temperature =
        json_double(current, "apparent_temperature", NAN);
    const double humidity =
        json_double(current, "relative_humidity_2m", NAN);
    const double precipitation =
        json_double(current, "precipitation", NAN);
    const double pressure =
        json_double(current, "pressure_msl", NAN);
    const double wind_speed = json_double(current, "wind_speed_10m", NAN);
    const double wind_direction = json_double(current, "wind_direction_10m", NAN);
    const int weather_code = json_int(current, "weather_code", -1);
    if (!isfinite(temperature)) {
        cJSON_Delete(root);
        return false;
    }

    next->weather_temperature = (int)lround(temperature);
    next->weather_apparent_temperature = isfinite(apparent_temperature)
        ? (int)lround(apparent_temperature)
        : next->weather_temperature;
    next->weather_code = weather_code;
    next->weather_humidity_percent = isfinite(humidity)
        ? (int)lround(humidity)
        : 0;
    next->weather_pressure_hpa = isfinite(pressure)
        ? (int)lround(pressure)
        : 0;
    next->weather_precipitation_tenths_mm = isfinite(precipitation)
        ? (int)lround(precipitation * 10.0)
        : 0;
    next->weather_wind_kt = isfinite(wind_speed)
        ? (int)lround(wind_speed)
        : 0;
    next->weather_wind_deg = isfinite(wind_direction)
        ? (int)lround(wind_direction)
        : 0;
    copy_string(next->weather_unit, sizeof(next->weather_unit), "C");
    copy_string(next->weather_summary,
                sizeof(next->weather_summary),
                weather_code_summary(weather_code));
    if (isfinite(wind_speed) && isfinite(wind_direction)) {
        snprintf(next->weather_wind,
                 sizeof(next->weather_wind),
                 "%s %.0f kt",
                 wind_cardinal(wind_direction),
                 wind_speed);
    } else {
        copy_string(next->weather_wind, sizeof(next->weather_wind), "wind unavailable");
    }

    const cJSON *daily = object_item(root, "daily");
    const char *sunrise = json_array_string(daily, "sunrise", 0);
    const char *sunset = json_array_string(daily, "sunset", 0);
    if (sunrise != NULL && strlen(sunrise) >= 5) {
        copy_string(
            next->weather_sunrise,
            sizeof(next->weather_sunrise),
            sunrise + strlen(sunrise) - 5);
    }
    if (sunset != NULL && strlen(sunset) >= 5) {
        copy_string(
            next->weather_sunset,
            sizeof(next->weather_sunset),
            sunset + strlen(sunset) - 5);
    }
    for (int index = 0; index < APP_WEATHER_FORECAST_DAYS; index++) {
        weather_forecast_day_name(
            json_array_string(daily, "time", index),
            next->forecast_day[index]);
        next->forecast_code[index] = (int)lround(
            json_array_double(daily, "weather_code", index, -1));
        next->forecast_high[index] = (int)lround(
            json_array_double(
                daily,
                "temperature_2m_max",
                index,
                next->weather_temperature));
        next->forecast_low[index] = (int)lround(
            json_array_double(
                daily,
                "temperature_2m_min",
                index,
                next->weather_temperature));
        next->forecast_precip_percent[index] = (int)lround(
            json_array_double(
                daily,
                "precipitation_probability_max",
                index,
                0));
    }
    next->weather_valid = true;
    copy_string(next->weather_location_key,
                sizeof(next->weather_location_key),
                cfg->location_key);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Weather updated: %d%s %s", next->weather_temperature, next->weather_unit, next->weather_summary);
    return true;
}

static bool fetch_weather_air_quality(app_data_status_t *next,
                                      const app_config_t *cfg)
{
    char url[256];
    snprintf(
        url,
        sizeof(url),
        "https://air-quality-api.open-meteo.com/v1/air-quality"
        "?latitude=%s&longitude=%s&current=us_aqi,uv_index",
        cfg->location_lat,
        cfg->location_lon);

    int code = 0;
    char *body = http_get(url, FETCH_SMALL_MAX, &code);
    if (body == NULL || code < 200 || code >= 300) {
        ESP_LOGW(TAG, "Open-Meteo air quality failed, code=%d", code);
        heap_caps_free(body);
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    const cJSON *current = object_item(root, "current");
    const double aqi = json_double(current, "us_aqi", NAN);
    const double uv = json_double(current, "uv_index", NAN);
    if (!isfinite(aqi) && !isfinite(uv)) {
        cJSON_Delete(root);
        return false;
    }

    next->weather_aqi = isfinite(aqi) ? (int)lround(aqi) : 0;
    next->weather_uv_tenths = isfinite(uv) ? (int)lround(uv * 10.0) : 0;
    next->weather_air_quality_valid = true;
    cJSON_Delete(root);
    return true;
}

static bool fetch_iss_track(app_data_status_t *next)
{
    const int now = (int)time(NULL);
    if (now < AIRCRAFT_REASONABLE_UNIX_TIME) {
        return false;
    }

    char timestamps[144] = {0};
    size_t used = 0;
    static const int minute_offsets[
        APP_ISS_TRACK_POINTS] = {
        -93, -81, -69, -57, -45,
        -33, -21, -9, 0, 5,
    };
    for (int index = 0; index < APP_ISS_TRACK_POINTS; index++) {
        const int timestamp =
            now + minute_offsets[index] * 60;
        const int written = snprintf(
            timestamps + used,
            sizeof(timestamps) - used,
            "%s%d",
            index == 0 ? "" : ",",
            timestamp);
        if (written <= 0 || (size_t)written >= sizeof(timestamps) - used) {
            return false;
        }
        used += (size_t)written;
    }

    char url[320];
    snprintf(
        url,
        sizeof(url),
        "https://api.wheretheiss.at/v1/satellites/25544/positions"
        "?timestamps=%s&units=kilometers",
        timestamps);

    int code = 0;
    char *body = http_get(url, FETCH_MEDIUM_MAX, &code);
    if (body == NULL || code < 200 || code >= 300) {
        ESP_LOGW(TAG, "ISS track failed, code=%d", code);
        heap_caps_free(body);
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }

    int stored = 0;
    int closest_index = -1;
    int closest_delta = INT_MAX;
    const cJSON *point = NULL;
    cJSON_ArrayForEach(point, root) {
        if (stored >= APP_ISS_TRACK_POINTS || !cJSON_IsObject(point)) {
            break;
        }
        const double latitude = json_double(point, "latitude", NAN);
        const double longitude = json_double(point, "longitude", NAN);
        const int timestamp = json_int(point, "timestamp", 0);
        if (!isfinite(latitude) || !isfinite(longitude) || timestamp <= 0) {
            continue;
        }
        next->iss_track_latitude[stored] = (float)latitude;
        next->iss_track_longitude[stored] = (float)longitude;
        next->iss_track_timestamp[stored] = timestamp;
        const int delta = abs(timestamp - now);
        if (delta < closest_delta) {
            closest_delta = delta;
            closest_index = stored;
            next->iss_altitude_km =
                (float)json_double(point, "altitude", 0.0);
            next->iss_velocity_kph =
                (float)json_double(point, "velocity", 0.0);
            copy_string(
                next->iss_visibility,
                sizeof(next->iss_visibility),
                json_string(point, "visibility"));
        }
        stored++;
    }
    cJSON_Delete(root);
    if (stored == 0 || closest_index < 0) {
        return false;
    }

    next->iss_track_count = stored;
    next->iss_latitude = next->iss_track_latitude[closest_index];
    next->iss_longitude = next->iss_track_longitude[closest_index];
    next->iss_timestamp = next->iss_track_timestamp[closest_index];
    next->iss_valid = true;
    ESP_LOGI(
        TAG,
        "ISS updated: %.2f %.2f, %.0f km/h",
        (double)next->iss_latitude,
        (double)next->iss_longitude,
        (double)next->iss_velocity_kph);
    return true;
}

static bool fetch_metar(app_data_status_t *next, const app_config_t *cfg)
{
    char url[160];
    snprintf(url, sizeof(url), "https://aviationweather.gov/api/data/metar?ids=%s&format=json",
             cfg->metar_station);

    int code = 0;
    char *body = http_get(url, FETCH_SMALL_MAX, &code);
    if (body == NULL || code < 200 || code >= 300) {
        ESP_LOGW(TAG, "METAR failed, code=%d", code);
        heap_caps_free(body);
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    const cJSON *first = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 0) : NULL;
    if (first == NULL) {
        cJSON_Delete(root);
        return false;
    }

    copy_string(next->metar_station, sizeof(next->metar_station), json_string(first, "icaoId"));
    copy_string(next->metar_category, sizeof(next->metar_category), json_string(first, "fltCat"));
    copy_string(next->metar_raw, sizeof(next->metar_raw), json_string(first, "rawOb"));
    next->metar_temp_c = (int)lround(json_double(first, "temp", 0.0));
    next->metar_wind_kt = json_int(first, "wspd", 0);
    next->metar_wind_deg = json_int(first, "wdir", 0);
    next->metar_valid = true;
    cJSON_Delete(root);
    ESP_LOGI(TAG, "METAR updated: %s %s", next->metar_station, next->metar_category);
    return true;
}

static bool fetch_rainviewer(app_data_status_t *next)
{
    int code = 0;
    char *body = http_get("https://api.rainviewer.com/public/weather-maps.json", FETCH_MEDIUM_MAX, &code);
    if (body == NULL || code < 200 || code >= 300) {
        ESP_LOGW(TAG, "RainViewer failed, code=%d", code);
        heap_caps_free(body);
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    next->radar_generated_unix = json_int(root, "generated", 0);
    const char *host = json_string(root, "host");
    if (host != NULL && host[0] != '\0') {
        copy_string(rainviewer_host, sizeof(rainviewer_host), host);
    }
    const cJSON *radar = object_item(root, "radar");
    const cJSON *past = object_item(radar, "past");
    int count = cJSON_IsArray(past) ? cJSON_GetArraySize(past) : 0;
    const cJSON *last = count > 0 ? cJSON_GetArrayItem(past, count - 1) : NULL;
    if (last == NULL) {
        cJSON_Delete(root);
        return false;
    }

    rainviewer_frame_count =
        count < WEATHER_RADAR_OBSERVED_FRAME_COUNT
            ? count
            : WEATHER_RADAR_OBSERVED_FRAME_COUNT;
    const int first = count - rainviewer_frame_count;
    for (int i = 0; i < rainviewer_frame_count; i++) {
        const cJSON *frame = cJSON_GetArrayItem(past, first + i);
        rainviewer_frame_unix[i] = json_int(frame, "time", 0);
        copy_string(
            rainviewer_paths[i],
            sizeof(rainviewer_paths[i]),
            json_string(frame, "path"));
    }
    next->radar_frame_unix = json_int(last, "time", 0);
    copy_string(next->radar_path, sizeof(next->radar_path), json_string(last, "path"));
    next->radar_valid = next->radar_frame_unix > 0;
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Radar metadata updated: frame=%d", next->radar_frame_unix);
    return next->radar_valid;
}

static void smooth_weather_radar_alpha(uint8_t *alpha_plane)
{
    if (alpha_plane == NULL) {
        return;
    }

    const int size = APP_WEATHER_RADAR_OVERLAY_SIZE;
    for (int y = 0; y < size; y++) {
        uint8_t *row = alpha_plane + (size_t)y * size;
        int left = row[0];
        int center = row[0];
        int right = size > 1 ? row[1] : row[0];
        for (int x = 0; x < size; x++) {
            row[x] = (uint8_t)((left + center * 2 + right + 2) / 4);
            left = center;
            center = right;
            right = x + 2 < size ? row[x + 2] : right;
        }
    }

    uint8_t rows[3][APP_WEATHER_RADAR_OVERLAY_SIZE];
    memcpy(rows[0], alpha_plane, (size_t)size);
    memcpy(rows[1], alpha_plane, (size_t)size);
    memcpy(rows[2], alpha_plane + (size_t)(size > 1 ? 1 : 0) * size,
           (size_t)size);
    int previous = 0;
    int current = 1;
    int next = 2;
    for (int y = 0; y < size; y++) {
        uint8_t *output = alpha_plane + (size_t)y * size;
        for (int x = 0; x < size; x++) {
            output[x] = (uint8_t)(
                (rows[previous][x] + rows[current][x] * 2 +
                 rows[next][x] + 2) /
                4);
        }
        const int reusable = previous;
        previous = current;
        current = next;
        next = reusable;
        const int source_y = y + 2 < size ? y + 2 : size - 1;
        memcpy(rows[next],
               alpha_plane + (size_t)source_y * size,
               (size_t)size);
    }
}

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
} weather_radar_png_reader_t;

static void weather_radar_png_read(png_structp png_ptr,
                                   png_bytep output,
                                   png_size_t length)
{
    weather_radar_png_reader_t *reader =
        (weather_radar_png_reader_t *)png_get_io_ptr(png_ptr);
    if (reader == NULL ||
        length > reader->length ||
        reader->offset > reader->length - length) {
        png_error(png_ptr, "RainViewer PNG ended early");
        return;
    }
    memcpy(output, reader->data + reader->offset, length);
    reader->offset += length;
}

static bool decode_weather_radar_overlay(const uint8_t *png_data,
                                         size_t png_len,
                                         double latitude,
                                         float radius_miles,
                                         uint8_t *radar_overlay)
{
    if (png_data == NULL || png_len == 0 || radar_overlay == NULL) {
        return false;
    }

    png_structp png_ptr =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        ESP_LOGW(TAG, "Could not create RainViewer PNG decoder");
        return false;
    }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        ESP_LOGW(TAG, "Could not create RainViewer PNG info");
        return false;
    }

    uint8_t * volatile row0 = NULL;
    uint8_t * volatile row1 = NULL;
    if (setjmp(png_jmpbuf(png_ptr))) {
        heap_caps_free((void *)row0);
        heap_caps_free((void *)row1);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        ESP_LOGW(TAG, "Could not stream-decode RainViewer PNG");
        return false;
    }

    weather_radar_png_reader_t reader = {
        .data = png_data,
        .length = png_len,
        .offset = 0,
    };
    png_set_read_fn(png_ptr, &reader, weather_radar_png_read);
    png_read_info(png_ptr, info_ptr);

    const png_uint_32 image_width = png_get_image_width(png_ptr, info_ptr);
    const png_uint_32 image_height = png_get_image_height(png_ptr, info_ptr);
    const int bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    const int color_type = png_get_color_type(png_ptr, info_ptr);
    if (image_width == 0 ||
        image_height == 0 ||
        image_width > 512 ||
        image_height > 512) {
        png_error(png_ptr, "Unexpected RainViewer PNG dimensions");
    }

    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }
    const bool transparency = png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS);
    if (transparency) {
        png_set_tRNS_to_alpha(png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }
    if ((color_type & PNG_COLOR_MASK_ALPHA) == 0 && !transparency) {
        png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);
    }
    png_read_update_info(png_ptr, info_ptr);

    const png_size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);
    if (row_bytes != image_width * 4U) {
        png_error(png_ptr, "Unexpected RainViewer PNG pixel format");
    }
    row0 = (uint8_t *)heap_caps_malloc(
        row_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    row1 = (uint8_t *)heap_caps_malloc(
        row_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (row0 == NULL || row1 == NULL) {
        png_error(png_ptr, "No PSRAM for RainViewer scanlines");
    }
    png_read_row(png_ptr, (png_bytep)row0, NULL);
    if (image_height > 1) {
        png_read_row(png_ptr, (png_bytep)row1, NULL);
    } else {
        memcpy((void *)row1, (const void *)row0, row_bytes);
    }

    const double meters_per_pixel =
        WEATHER_RADAR_WEB_MERCATOR_METERS_PER_PIXEL *
        cos(deg_to_rad(latitude)) /
        (double)(1 << WEATHER_RADAR_ZOOM) *
        (256.0 / (double)image_width);
    const double requested_span_m =
        (double)radius_miles * 2.0 * WEATHER_RADAR_METERS_PER_MILE *
        (double)APP_WEATHER_RADAR_OVERLAY_SIZE /
        (double)APP_WEATHER_RADAR_VIEW_SIZE;
    double source_span_px = requested_span_m / fmax(meters_per_pixel, 1.0);
    const double max_span =
        (double)(image_width < image_height ? image_width : image_height);
    if (source_span_px < 8.0) {
        source_span_px = 8.0;
    } else if (source_span_px > max_span) {
        source_span_px = max_span;
    }

    uint8_t *alpha_plane =
        radar_overlay + APP_WEATHER_RADAR_OVERLAY_PIXELS * 2;
    int current_source_y = 0;
    for (int y = 0; y < APP_WEATHER_RADAR_OVERLAY_SIZE; y++) {
        const double normalized_y =
            ((double)y + 0.5) /
                (double)APP_WEATHER_RADAR_OVERLAY_SIZE -
            0.5;
        double source_y =
            (double)image_height * 0.5 - 0.5 +
            normalized_y * source_span_px;
        source_y = fmax(0.0, fmin(source_y, (double)image_height - 1.0));
        const int source_y0 = (int)floor(source_y);
        const uint32_t weight_y =
            (uint32_t)lround((source_y - source_y0) * 256.0);
        while (current_source_y < source_y0) {
            uint8_t *swap = (uint8_t *)row0;
            row0 = row1;
            row1 = swap;
            current_source_y++;
            if (current_source_y + 1 < (int)image_height) {
                png_read_row(png_ptr, (png_bytep)row1, NULL);
            } else {
                memcpy((void *)row1, (const void *)row0, row_bytes);
            }
        }
        for (int x = 0; x < APP_WEATHER_RADAR_OVERLAY_SIZE; x++) {
            const double normalized_x =
                ((double)x + 0.5) /
                    (double)APP_WEATHER_RADAR_OVERLAY_SIZE -
                0.5;
            double source_x =
                (double)image_width * 0.5 - 0.5 +
                normalized_x * source_span_px;
            source_x = fmax(0.0, fmin(source_x, (double)image_width - 1.0));
            const int source_x0 = (int)floor(source_x);
            const int source_x1 = source_x0 + 1 < (int)image_width
                ? source_x0 + 1
                : source_x0;
            const uint32_t weight_x =
                (uint32_t)lround((source_x - source_x0) * 256.0);
            const size_t source_indices[4] = {
                (size_t)source_x0 * 4,
                (size_t)source_x1 * 4,
                (size_t)source_x0 * 4,
                (size_t)source_x1 * 4,
            };
            const size_t output_index =
                (size_t)y * APP_WEATHER_RADAR_OVERLAY_SIZE +
                (size_t)x;
            uint8_t channels[4];
            for (int channel = 0; channel < 4; channel++) {
                const uint32_t top =
                    row0[source_indices[0] + channel] * (256U - weight_x) +
                    row0[source_indices[1] + channel] * weight_x;
                const uint32_t bottom =
                    row1[source_indices[2] + channel] * (256U - weight_x) +
                    row1[source_indices[3] + channel] * weight_x;
                channels[channel] = (uint8_t)(
                    (top * (256U - weight_y) + bottom * weight_y + 32768U) >>
                    16);
            }
            const uint8_t red = channels[0];
            const uint8_t green = channels[1];
            const uint8_t blue = channels[2];
            const uint8_t alpha = channels[3];
            const uint16_t rgb565 =
                (uint16_t)(((uint16_t)(red >> 3) << 11) |
                           ((uint16_t)(green >> 2) << 5) |
                           (uint16_t)(blue >> 3));
            radar_overlay[output_index * 2] = (uint8_t)(rgb565 & 0xff);
            radar_overlay[output_index * 2 + 1] = (uint8_t)(rgb565 >> 8);
            alpha_plane[output_index] =
                alpha < 5 ? 0 : (uint8_t)(((uint16_t)alpha * 185U) / 255U);
        }
    }

    smooth_weather_radar_alpha(alpha_plane);

    heap_caps_free((void *)row0);
    heap_caps_free((void *)row1);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return true;
}

static float weather_radar_motion_score(const uint8_t *previous_alpha,
                                        const uint8_t *current_alpha,
                                        int dx,
                                        int dy,
                                        int stride,
                                        int *sample_count)
{
    uint64_t difference = 0;
    int samples = 0;
    const int size = APP_WEATHER_RADAR_OVERLAY_SIZE;
    for (int y = 4; y < size - 4; y += stride) {
        const int previous_y = y - dy;
        if (previous_y < 0 || previous_y >= size) {
            continue;
        }
        for (int x = 4; x < size - 4; x += stride) {
            const int previous_x = x - dx;
            if (previous_x < 0 || previous_x >= size) {
                continue;
            }
            const uint8_t current =
                current_alpha[(size_t)y * size + x];
            const uint8_t previous =
                previous_alpha[(size_t)previous_y * size + previous_x];
            if (current < 8 && previous < 8) {
                continue;
            }
            difference +=
                current > previous ? current - previous : previous - current;
            samples++;
        }
    }
    if (sample_count != NULL) {
        *sample_count = samples;
    }
    return samples >= 80
        ? (float)difference / (float)samples
        : FLT_MAX;
}

static void estimate_weather_radar_motion(const uint8_t *previous_alpha,
                                          const uint8_t *current_alpha,
                                          int frame_interval_seconds,
                                          float *motion_x,
                                          float *motion_y)
{
    *motion_x = 0.0f;
    *motion_y = 0.0f;
    if (previous_alpha == NULL || current_alpha == NULL ||
        frame_interval_seconds <= 0) {
        return;
    }
    int best_x = 0;
    int best_y = 0;
    float best_score = FLT_MAX;
    for (int dy = -20; dy <= 20; dy += 2) {
        for (int dx = -20; dx <= 20; dx += 2) {
            const float score = weather_radar_motion_score(
                previous_alpha,
                current_alpha,
                dx,
                dy,
                3,
                NULL);
            if (score < best_score) {
                best_score = score;
                best_x = dx;
                best_y = dy;
            }
        }
        /* Motion matching is deliberately CPU-heavy. Give the idle task a
         * tick between scan rows so radar preparation cannot starve CPU 1
         * long enough to trip the task watchdog. */
        vTaskDelay(1);
    }
    const int coarse_x = best_x;
    const int coarse_y = best_y;
    for (int dy = coarse_y - 2; dy <= coarse_y + 2; dy++) {
        for (int dx = coarse_x - 2; dx <= coarse_x + 2; dx++) {
            const float score = weather_radar_motion_score(
                previous_alpha,
                current_alpha,
                dx,
                dy,
                2,
                NULL);
            if (score < best_score) {
                best_score = score;
                best_x = dx;
                best_y = dy;
            }
        }
        vTaskDelay(1);
    }
    if (best_score == FLT_MAX) {
        return;
    }

    const float step_scale =
        (float)(APP_WEATHER_RADAR_FORECAST_STEP_MINUTES * 60) /
        (float)frame_interval_seconds;
    const float forecast_margin =
        ((float)APP_WEATHER_RADAR_OVERLAY_SIZE -
         (float)APP_WEATHER_RADAR_VIEW_SIZE) *
        0.5f - 2.0f;
    const float maximum_step =
        forecast_margin /
        (float)(APP_WEATHER_RADAR_FRAME_COUNT - 1);
    *motion_x = fmaxf(
        -maximum_step,
        fminf(maximum_step, best_x * step_scale));
    *motion_y = fmaxf(
        -maximum_step,
        fminf(maximum_step, best_y * step_scale));
}

static void refresh_requested_weather_radar(void)
{
    float radius_miles = AIRCRAFT_RADIUS_DEFAULT_MI;
    char location_key[sizeof(requested_weather_radar_location_key)] = {0};
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    const bool requested = weather_radar_requested;
    if (requested) {
        radius_miles = clamp_aircraft_radius_miles(requested_weather_radar_radius_miles);
        copy_string(location_key,
                    sizeof(location_key),
                    requested_weather_radar_location_key);
        weather_radar_requested = false;
    }
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    if (!requested) {
        return;
    }

    app_config_t cfg;
    app_config_get(&cfg);
    if (location_key[0] != '\0') {
        (void)app_location_apply_preset(&cfg, location_key);
    }
    app_data_status_t next = read_status();
    if (!fetch_rainviewer(&next) || next.radar_path[0] == '\0') {
        next.weather_radar_overlay_valid = false;
        next.weather_radar_frame_count = 0;
        next.fetched_unix = (int)time(NULL);
        write_status(&next);
        return;
    }

    if (weather_radar_overlays == NULL) {
        weather_radar_overlays = (uint8_t *)heap_caps_malloc(
            APP_WEATHER_RADAR_OVERLAY_BYTES,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (weather_radar_overlays == NULL) {
        ESP_LOGW(TAG, "No PSRAM for weather radar sequence");
        next.weather_radar_overlay_valid = false;
        next.weather_radar_frame_count = 0;
        next.fetched_unix = (int)time(NULL);
        write_status(&next);
        return;
    }

    /* Keep one stable PSRAM allocation for every city and radius.  Clearing
     * it here prevents a failed replacement request from retaining stale
     * pixels without fragmenting the heap through repeated free/allocate. */
    memset(weather_radar_overlays, 0, APP_WEATHER_RADAR_OVERLAY_BYTES);

    next.weather_radar_overlay_valid = false;
    next.weather_radar_frame_count = 0;
    memset(
        next.weather_radar_frame_unix,
        0,
        sizeof(next.weather_radar_frame_unix));
    write_status(&next);

    int decoded_count = 0;
    int decoded_frame_unix[
        WEATHER_RADAR_OBSERVED_FRAME_COUNT] = {0};
    uint8_t *previous_alpha = (uint8_t *)heap_caps_malloc(
        APP_WEATHER_RADAR_OVERLAY_PIXELS,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    float estimated_motion_x = 0.0f;
    float estimated_motion_y = 0.0f;
    int last_http_code = 0;
    size_t last_png_len = 0;
    for (int i = 0; i < rainviewer_frame_count; i++) {
        bool superseded = false;
        if (status_mutex != NULL) {
            xSemaphoreTake(status_mutex, portMAX_DELAY);
        }
        superseded = weather_radar_requested;
        if (status_mutex != NULL) {
            xSemaphoreGive(status_mutex);
        }
        if (superseded) {
            break;
        }
        if (rainviewer_paths[i][0] == '\0' ||
            rainviewer_frame_unix[i] <= 0) {
            continue;
        }
        char url[384];
        snprintf(url,
                 sizeof(url),
                 "%.127s%.95s/%d/%d/%.19s/%.19s/2/1_1.png",
                 rainviewer_host,
                 rainviewer_paths[i],
                 WEATHER_RADAR_SOURCE_SIZE,
                 WEATHER_RADAR_ZOOM,
                 cfg.location_lat,
                 cfg.location_lon);

        size_t png_len = 0;
        int code = 0;
        uint8_t *png_data =
            http_get_binary(url, FETCH_PNG_MAX, &code, &png_len);
        last_http_code = code;
        last_png_len = png_len;
        const bool decoded =
            png_data != NULL &&
            code >= 200 &&
            code < 300 &&
            decode_weather_radar_overlay(
                png_data,
                png_len,
                atof(cfg.location_lat),
                radius_miles,
                weather_radar_overlays);
        heap_caps_free(png_data);
        if (!decoded) {
            continue;
        }
        const uint8_t *current_alpha =
            weather_radar_overlays +
            APP_WEATHER_RADAR_OVERLAY_PIXELS * 2U;
        if (decoded_count == 0 && previous_alpha != NULL) {
            memcpy(previous_alpha,
                   current_alpha,
                   APP_WEATHER_RADAR_OVERLAY_PIXELS);
        } else if (decoded_count > 0 && previous_alpha != NULL) {
            estimate_weather_radar_motion(
                previous_alpha,
                current_alpha,
                rainviewer_frame_unix[i] -
                    decoded_frame_unix[decoded_count - 1],
                &estimated_motion_x,
                &estimated_motion_y);
        }
        next.weather_radar_frame_unix[decoded_count] =
            rainviewer_frame_unix[i];
        decoded_frame_unix[decoded_count] =
            rainviewer_frame_unix[i];
        decoded_count++;
    }
    heap_caps_free(previous_alpha);

    bool superseded = false;
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    superseded = weather_radar_requested;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    if (superseded) {
        weather_radar_observed_frame_count = 0;
        ESP_LOGI(TAG, "Discarded superseded weather radar request");
        return;
    }

    next.weather_radar_frame_count =
        decoded_count > 0 ? APP_WEATHER_RADAR_FRAME_COUNT : 0;
    next.weather_radar_overlay_valid = decoded_count > 0;
    weather_radar_observed_frame_count = decoded_count;
    if (decoded_count > 0) {
        const int latest_observed_unix =
            decoded_frame_unix[decoded_count - 1];
        for (int i = 0; i < APP_WEATHER_RADAR_FRAME_COUNT; i++) {
            next.weather_radar_frame_unix[i] =
                latest_observed_unix +
                i * APP_WEATHER_RADAR_FORECAST_STEP_MINUTES * 60;
        }
        next.weather_radar_motion_x = 0.0f;
        next.weather_radar_motion_y = 0.0f;
        if (decoded_count >= 2) {
            next.weather_radar_motion_x = estimated_motion_x;
            next.weather_radar_motion_y = estimated_motion_y;
        }
        weather_radar_overlay_revision++;
        next.weather_radar_overlay_revision = weather_radar_overlay_revision;
        next.radar_frame_unix = latest_observed_unix;
        next.weather_radar_overlay_radius_miles = radius_miles;
        copy_string(next.weather_radar_overlay_location_key,
                    sizeof(next.weather_radar_overlay_location_key),
                    cfg.location_key);
        ESP_LOGI(TAG,
                 "Weather radar nowcast ready: %s %.0f mi, %d observed, "
                 "%d projected, motion %.1f,%.1f px/10min, %u bytes",
                 cfg.location_key,
                 (double)radius_miles,
                 decoded_count,
                 APP_WEATHER_RADAR_FRAME_COUNT,
                 (double)next.weather_radar_motion_x,
                 (double)next.weather_radar_motion_y,
                 (unsigned)(
                     APP_WEATHER_RADAR_OVERLAY_BYTES));
    } else {
        ESP_LOGW(TAG,
                 "Weather radar tiles failed, code=%d bytes=%u",
                 last_http_code,
                 (unsigned)last_png_len);
    }
    const int now = (int)time(NULL);
    next.fetched_unix = now > next.fetched_unix ? now : next.fetched_unix + 1;
    write_status(&next);
}

static bool publish_aircraft_candidates(app_data_status_t *next,
                                        app_aircraft_t *candidates,
                                        int candidate_count,
                                        int accepted_rows,
                                        int raw_count,
                                        float radius_miles,
                                        const char *source)
{
    if (radius_miles >= 49.5f) {
        int commercial_count = 0;
        for (int index = 0; index < candidate_count; index++) {
            if (!aircraft_is_identified_commercial(&candidates[index])) {
                continue;
            }
            if (commercial_count != index) {
                candidates[commercial_count] = candidates[index];
            }
            commercial_count++;
        }
        candidate_count = commercial_count;
    }
    qsort(candidates,
          candidate_count,
          sizeof(candidates[0]),
          compare_aircraft_by_distance);
    const int stored = candidate_count < APP_MAX_AIRCRAFT
        ? candidate_count
        : APP_MAX_AIRCRAFT;

    // At wider radii retain the nearest half of the display unconditionally,
    // then use the remaining slots for a distance-sensitive preference toward
    // verified airline traffic. This prevents a cluster of tiny nearby targets
    // from hiding every passenger jet without turning the radar into an
    // airline-only display.
    const int nearest_slots = stored < APP_MAX_AIRCRAFT / 2
        ? stored
        : APP_MAX_AIRCRAFT / 2;
    for (int target = nearest_slots; target < stored; target++) {
        int best = target;
        float best_score = aircraft_selection_score(
            &candidates[target], radius_miles);
        for (int index = target + 1; index < candidate_count; index++) {
            const float score = aircraft_selection_score(
                &candidates[index], radius_miles);
            if (score < best_score) {
                best = index;
                best_score = score;
            }
        }
        if (best != target) {
            const app_aircraft_t swap = candidates[target];
            candidates[target] = candidates[best];
            candidates[best] = swap;
        }
    }
    qsort(candidates,
          stored,
          sizeof(candidates[0]),
          compare_aircraft_by_distance);
    for (int index = 0; index < stored; index++) {
        next->aircraft[index] = candidates[index];
    }
    for (int index = stored; index < APP_MAX_AIRCRAFT; index++) {
        memset(&next->aircraft[index], 0, sizeof(next->aircraft[index]));
    }

    next->aircraft_count = stored;
    next->aircraft_raw_count = raw_count;
    next->aircraft_usable_count = accepted_rows;
    next->aircraft_radius_miles = radius_miles;
    next->aircraft_valid = true;
    ESP_LOGI(TAG,
             "Aircraft updated via %s: %d %s of %d usable states (%d raw), radius=%.0f mi",
             source,
             stored,
             radius_miles >= 49.5f
                 ? "commercial"
                 : "balanced",
             accepted_rows,
             raw_count,
             (double)radius_miles);
    return true;
}

static double json_flexible_number(const cJSON *value, double fallback)
{
    if (cJSON_IsNumber(value)) {
        return value->valuedouble;
    }
    if (cJSON_IsString(value) && value->valuestring[0] != '\0') {
        char *end = NULL;
        const double parsed = strtod(value->valuestring, &end);
        if (end != value->valuestring && isfinite(parsed)) {
            return parsed;
        }
    }
    return fallback;
}

static bool fetch_aircraft_airplanes_live(app_data_status_t *next,
                                           double lat,
                                           double lon,
                                           float radius_miles,
                                           float max_distance_nm)
{
    aircraft_last_fetch_used_fallback = true;
    const int query_radius_nm = (int)ceilf(max_distance_nm);
    char url[192];
    snprintf(url,
             sizeof(url),
             "https://api.airplanes.live/v2/point/%.4f/%.4f/%d",
             lat,
             lon,
             query_radius_nm);

    int code = 0;
    char *body = http_get(url, FETCH_AIRCRAFT_MAX, &code);
    next->aircraft_http_status = code;
    next->aircraft_auth_used = false;
    if (body == NULL || code < 200 || code >= 300) {
        ESP_LOGW(TAG, "Aircraft fallback failed, code=%d", code);
        heap_caps_free(body);
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    const cJSON *states = object_item(root, "ac");
    if (!cJSON_IsArray(states)) {
        cJSON_Delete(root);
        return false;
    }

    app_aircraft_t *candidates = (app_aircraft_t *)heap_caps_calloc(
        AIRCRAFT_CANDIDATE_LIMIT,
        sizeof(app_aircraft_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (candidates == NULL) {
        ESP_LOGW(TAG, "No PSRAM for fallback aircraft candidates");
        cJSON_Delete(root);
        return false;
    }

    const int total = cJSON_GetArraySize(states);
    const double response_now = json_flexible_number(
        object_item(root, "now"),
        (double)time(NULL));
    const int response_unix = (int)(response_now > 100000000000.0
        ? response_now / 1000.0
        : response_now);
    int candidate_count = 0;
    int accepted_rows = 0;
    for (int index = 0; index < total; index++) {
        const cJSON *row = cJSON_GetArrayItem(states, index);
        if (!cJSON_IsObject(row)) {
            continue;
        }

        const double aircraft_lat = json_flexible_number(
            object_item(row, "lat"), NAN);
        const double aircraft_lon = json_flexible_number(
            object_item(row, "lon"), NAN);
        const double seen_position = json_flexible_number(
            object_item(row, "seen_pos"),
            json_flexible_number(object_item(row, "seen"), 0.0));
        if (!isfinite(aircraft_lat) ||
            !isfinite(aircraft_lon) ||
            seen_position > AIRCRAFT_STALE_SECONDS) {
            continue;
        }

        const cJSON *altitude = object_item(row, "alt_baro");
        if (cJSON_IsString(altitude) &&
            strcasecmp(altitude->valuestring, "ground") == 0) {
            continue;
        }

        app_aircraft_t aircraft;
        memset(&aircraft, 0, sizeof(aircraft));
        aircraft.valid = true;
        copy_string(aircraft.icao24,
                    sizeof(aircraft.icao24),
                    json_string(row, "hex"));
        copy_string(aircraft.callsign,
                    sizeof(aircraft.callsign),
                    json_string(row, "flight"));
        trim_callsign(aircraft.callsign);
        if (aircraft.callsign[0] == '\0') {
            copy_string(aircraft.callsign,
                        sizeof(aircraft.callsign),
                        aircraft.icao24[0] != '\0'
                            ? aircraft.icao24
                            : "UNKNOWN");
        }
        aircraft.latitude = (float)aircraft_lat;
        aircraft.longitude = (float)aircraft_lon;
        aircraft.altitude_ft = (int)lround(
            json_flexible_number(altitude, 0.0));
        aircraft.speed_kt = (int)lround(json_flexible_number(
            object_item(row, "gs"), 0.0));
        const double track = json_flexible_number(
            object_item(row, "track"), NAN);
        aircraft.track_valid = isfinite(track);
        const double vertical_rate = json_flexible_number(
            object_item(row, "baro_rate"), NAN);
        aircraft.vertical_rate_valid = isfinite(vertical_rate);
        aircraft.vertical_rate_mps = aircraft.vertical_rate_valid
            ? (float)(vertical_rate * 0.00508)
            : 0.0f;
        aircraft.position_unix = response_unix - (int)lround(seen_position);
        aircraft.contact_unix = aircraft.position_unix;
        aircraft.distance_nm = distance_nm(
            lat,
            lon,
            aircraft.latitude,
            aircraft.longitude);
        aircraft.bearing_deg = bearing_deg(
            lat,
            lon,
            aircraft.latitude,
            aircraft.longitude);
        aircraft.track_deg = aircraft.track_valid
            ? ((int)lround(track) + 360) % 360
            : aircraft.bearing_deg;

        apply_metadata_cache(&aircraft);
        if (aircraft.typecode[0] == '\0') {
            copy_icao_type_designator(
                aircraft.typecode,
                sizeof(aircraft.typecode),
                json_string(row, "t"));
        }
        if (aircraft.model[0] == '\0') {
            copy_string(aircraft.model,
                        sizeof(aircraft.model),
                        json_string(row, "desc"));
        }
        if (aircraft.registration[0] == '\0') {
            copy_string(aircraft.registration,
                        sizeof(aircraft.registration),
                        json_string(row, "r"));
        }
        if (aircraft.typecode[0] != '\0' ||
            aircraft.model[0] != '\0' ||
            aircraft.registration[0] != '\0') {
            aircraft.metadata_valid = true;
        }

        const bool rotorcraft = airspace_aircraft_is_rotorcraft(
            aircraft.typecode,
            aircraft.model,
            aircraft.manufacturer,
            aircraft.engine_type,
            aircraft.category);
        if (aircraft.speed_kt < AIRCRAFT_MIN_SPEED_KT && !rotorcraft) {
            continue;
        }
        if (aircraft.distance_nm > max_distance_nm ||
            aircraft.distance_nm > AIRCRAFT_MAX_DISPLAY_NM) {
            continue;
        }

        accepted_rows++;
        insert_aircraft_candidate(
            candidates,
            &candidate_count,
            &aircraft,
            radius_miles);
    }

    const bool published = publish_aircraft_candidates(
        next,
        candidates,
        candidate_count,
        accepted_rows,
        total,
        radius_miles,
        "Airplanes.live fallback");
    heap_caps_free(candidates);
    cJSON_Delete(root);
    return published;
}

static bool fetch_aircraft(app_data_status_t *next, const app_config_t *cfg)
{
    const double lat = atof(cfg->location_lat);
    const double lon = atof(cfg->location_lon);
    const float radius_miles = read_aircraft_radius_miles();
    const double padded_radius_miles = (double)radius_miles * AIRCRAFT_QUERY_PADDING;
    const double lat_half = padded_radius_miles / STATUTE_MI_PER_LAT_DEG;
    const double cos_lat = cos(deg_to_rad(lat));
    const double lon_half = fabs(cos_lat) > 0.02
        ? padded_radius_miles / (STATUTE_MI_PER_LAT_DEG * fabs(cos_lat))
        : lat_half;
    const float max_distance_nm = radius_miles * AIRCRAFT_STATUTE_MI_TO_NM * AIRCRAFT_QUERY_PADDING;
    const bool credentials_configured =
        cfg->opensky_client_id[0] != '\0' &&
        cfg->opensky_client_secret[0] != '\0';
    const int now_unix = (int)time(NULL);
    if (now_unix < opensky_rate_limited_until_unix &&
        (opensky_rate_limit_was_authenticated ||
         !credentials_configured)) {
        return fetch_aircraft_airplanes_live(
            next,
            lat,
            lon,
            radius_miles,
            max_distance_nm);
    }
    char url[240];
    snprintf(url, sizeof(url),
             "https://opensky-network.org/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f&extended=1",
             lat - lat_half, lon - lon_half, lat + lat_half, lon + lon_half);

    int code = 0;
    const char *bearer = opensky_bearer_token_if_available(cfg);
    next->aircraft_auth_used = bearer != NULL;
    char *body = bearer != NULL
        ? http_get_bearer(url, bearer, FETCH_AIRCRAFT_MAX, &code)
        : http_get(url, FETCH_AIRCRAFT_MAX, &code);

    if (code == 401 && cfg->opensky_client_id[0] != '\0' && cfg->opensky_client_secret[0] != '\0') {
        heap_caps_free(body);
        opensky_access_token[0] = '\0';
        opensky_token_expires_unix = 0;
        bearer = opensky_bearer_token_if_available(cfg);
        body = bearer != NULL
            ? http_get_bearer(url, bearer, FETCH_AIRCRAFT_MAX, &code)
            : NULL;
        next->aircraft_auth_used = bearer != NULL;
    }

    next->aircraft_http_status = code;
    if (body == NULL || code < 200 || code >= 300) {
        ESP_LOGW(TAG, "Aircraft states failed, code=%d", code);
        heap_caps_free(body);
        if (code == 429) {
            opensky_rate_limited_until_unix =
                now_unix + AIRCRAFT_RATE_LIMIT_RETRY_MS / 1000;
            opensky_rate_limit_was_authenticated = bearer != NULL;
        }
        return fetch_aircraft_airplanes_live(
            next,
            lat,
            lon,
            radius_miles,
            max_distance_nm);
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    const cJSON *states = object_item(root, "states");
    if (!cJSON_IsArray(states)) {
        cJSON_Delete(root);
        return false;
    }

    int candidate_count = 0;
    int accepted_rows = 0;
    app_aircraft_t *candidates = (app_aircraft_t *)heap_caps_calloc(
        AIRCRAFT_CANDIDATE_LIMIT,
        sizeof(app_aircraft_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (candidates == NULL) {
        ESP_LOGW(TAG, "No PSRAM for aircraft candidate list");
        cJSON_Delete(root);
        return false;
    }
    const int total = cJSON_GetArraySize(states);
    const int response_time_unix = json_int(root, "time", (int)time(NULL));
    next->aircraft_raw_count = total;
    next->aircraft_radius_miles = radius_miles;
    for (int i = 0; i < total; i++) {
        const cJSON *row = cJSON_GetArrayItem(states, i);
        if (!cJSON_IsArray(row)) {
            continue;
        }
        const cJSON *icao24 = cJSON_GetArrayItem(row, 0);
        const cJSON *callsign = cJSON_GetArrayItem(row, 1);
        const cJSON *time_position = cJSON_GetArrayItem(row, 3);
        const cJSON *last_contact = cJSON_GetArrayItem(row, 4);
        const cJSON *longitude = cJSON_GetArrayItem(row, 5);
        const cJSON *latitude = cJSON_GetArrayItem(row, 6);
        const cJSON *altitude_m = cJSON_GetArrayItem(row, 7);
        const cJSON *on_ground = cJSON_GetArrayItem(row, 8);
        const cJSON *speed_mps = cJSON_GetArrayItem(row, 9);
        const cJSON *track = cJSON_GetArrayItem(row, 10);
        const cJSON *vertical_rate = cJSON_GetArrayItem(row, 11);
        const cJSON *category = cJSON_GetArrayItem(row, 17);
        if (!cJSON_IsNumber(latitude) || !cJSON_IsNumber(longitude)) {
            continue;
        }
        if (cJSON_IsTrue(on_ground)) {
            continue;
        }
        if (aircraft_state_is_stale(time_position, last_contact)) {
            continue;
        }

        app_aircraft_t aircraft;
        memset(&aircraft, 0, sizeof(aircraft));
        aircraft.valid = true;
        if (cJSON_IsString(icao24) && icao24->valuestring[0] != '\0') {
            copy_string(aircraft.icao24, sizeof(aircraft.icao24), icao24->valuestring);
        }
        if (cJSON_IsString(callsign) && callsign->valuestring[0] != '\0') {
            copy_string(aircraft.callsign, sizeof(aircraft.callsign), callsign->valuestring);
            trim_callsign(aircraft.callsign);
        } else if (cJSON_IsString(icao24) && icao24->valuestring[0] != '\0') {
            copy_string(aircraft.callsign, sizeof(aircraft.callsign), icao24->valuestring);
        } else {
            copy_string(aircraft.callsign, sizeof(aircraft.callsign), "UNKNOWN");
        }
        aircraft.latitude = (float)latitude->valuedouble;
        aircraft.longitude = (float)longitude->valuedouble;
        aircraft.altitude_ft = cJSON_IsNumber(altitude_m) ? (int)lround(altitude_m->valuedouble * 3.28084) : 0;
        aircraft.speed_kt = cJSON_IsNumber(speed_mps) ? (int)lround(speed_mps->valuedouble * 1.94384) : 0;
        aircraft.track_valid = cJSON_IsNumber(track);
        aircraft.vertical_rate_valid = cJSON_IsNumber(vertical_rate);
        aircraft.vertical_rate_mps =
            aircraft.vertical_rate_valid ? (float)vertical_rate->valuedouble : 0.0f;
        aircraft.position_unix = cJSON_IsNumber(time_position) ? time_position->valueint : response_time_unix;
        aircraft.contact_unix = cJSON_IsNumber(last_contact) ? last_contact->valueint : aircraft.position_unix;
        aircraft.category = cJSON_IsNumber(category) ? category->valueint : 0;
        aircraft.distance_nm = distance_nm(lat, lon, aircraft.latitude, aircraft.longitude);
        aircraft.bearing_deg = bearing_deg(lat, lon, aircraft.latitude, aircraft.longitude);
        aircraft.track_deg = aircraft.track_valid ? ((int)lround(track->valuedouble) + 360) % 360 : aircraft.bearing_deg;
        apply_metadata_cache(&aircraft);

        const bool rotorcraft = airspace_aircraft_is_rotorcraft(
            aircraft.typecode,
            aircraft.model,
            aircraft.manufacturer,
            aircraft.engine_type,
            aircraft.category);
        if (cJSON_IsNumber(speed_mps) && aircraft.speed_kt < AIRCRAFT_MIN_SPEED_KT && !rotorcraft) {
            continue;
        }
        if (aircraft.distance_nm > max_distance_nm || aircraft.distance_nm > AIRCRAFT_MAX_DISPLAY_NM) {
            continue;
        }

        accepted_rows++;
        insert_aircraft_candidate(
            candidates,
            &candidate_count,
            &aircraft,
            radius_miles);
    }

    const bool published = publish_aircraft_candidates(
        next,
        candidates,
        candidate_count,
        accepted_rows,
        total,
        radius_miles,
        "OpenSky");
    aircraft_last_fetch_used_fallback = false;
    heap_caps_free(candidates);
    cJSON_Delete(root);
    return published;
}

static bool ticks_due(TickType_t now, TickType_t due)
{
    return (int32_t)(now - due) >= 0;
}

static TickType_t ticks_until(TickType_t now, TickType_t due)
{
    return ticks_due(now, due) ? 0 : due - now;
}

static void publish_progressive_update(app_data_status_t *next)
{
    if (next == NULL) {
        return;
    }
    next->state = APP_DATA_READY;
    const int now = (int)time(NULL);
    next->fetched_unix = now > next->fetched_unix
        ? now
        : next->fetched_unix + 1;
    write_status(next);
}

static void fetch_updates(bool include_slow_sources, bool include_aircraft)
{
    app_config_t cfg;
    app_config_get(&cfg);
    app_config_t weather_cfg;
    weather_config(&weather_cfg);

    app_data_status_t next = read_status();
    if (next.weather_radar_overlay_location_key[0] != '\0' &&
        strcasecmp(next.weather_radar_overlay_location_key,
                   weather_cfg.location_key) != 0) {
        next.weather_radar_overlay_valid = false;
    }
    set_state(APP_DATA_FETCHING);
    next.state = APP_DATA_FETCHING;

    bool any = false;
    if (include_slow_sources) {
        next.weather_valid = false;
        next.weather_air_quality_valid = false;
        next.metar_valid = false;
        next.radar_valid = false;
        const bool weather_updated = fetch_weather_current(&next, &weather_cfg);
        any |= weather_updated;
        if (weather_updated) {
            /* Current conditions and the five-day forecast are now usable.
             * Do not hold them behind AQ, ISS, METAR, and radar requests. */
            publish_progressive_update(&next);
        }
        const bool air_quality_updated =
            fetch_weather_air_quality(&next, &weather_cfg);
        any |= air_quality_updated;
        if (air_quality_updated) {
            publish_progressive_update(&next);
        }
        any |= fetch_iss_track(&next);
        any |= fetch_metar(&next, &cfg);
        /* Radar metadata and tiles are intentionally demand-loaded only when
         * the user opens the radar view.  They must never delay first paint
         * of current conditions or the forecast. */
    }
    if (include_aircraft && cfg.opensky_enabled) {
        any |= fetch_aircraft(&next, &cfg);
    }

    const bool has_cached_data = next.weather_valid || next.metar_valid || next.radar_valid || next.aircraft_valid;
    next.state = (any || has_cached_data) ? APP_DATA_READY : APP_DATA_ERROR;
    next.fetched_unix = (int)time(NULL);
    write_status(&next);
}

static void fetch_all(void)
{
    fetch_updates(true, true);
}

static void fetch_aircraft_only(void)
{
    fetch_updates(false, true);
}

static void data_task(void *arg)
{
    (void)arg;
    TickType_t next_slow_due = 0;
    TickType_t next_aircraft_due = 0;
    while (true) {
        if (data_paused) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }
        app_network_status_t net;
        app_network_get_status(&net);
        app_launches_tick(net.wifi_connected && net.time_synced);
        if (net.wifi_connected && net.time_synced) {
            app_tracker_tick();
            const bool full_requested = full_refresh_requested;
            const bool aircraft_requested = aircraft_refresh_requested;
            const bool api_requested = api_validation_requested;
            full_refresh_requested = false;
            aircraft_refresh_requested = false;
            api_validation_requested = false;

            if (api_requested) {
                validate_api_credentials();
            }

            /* A detail-page request is interactive and should not sit behind
             * the next periodic weather/aircraft refresh. */
            refresh_requested_aircraft_metadata();

            TickType_t now = xTaskGetTickCount();
            if (full_requested || ticks_due(now, next_slow_due)) {
                fetch_all();
                refresh_aircraft_metadata_background();
                ESP_LOGI(
                    TAG,
                    "Data task stack minimum free: %u bytes",
                    (unsigned)uxTaskGetStackHighWaterMark(NULL));
                now = xTaskGetTickCount();
                next_slow_due = now + pdMS_TO_TICKS(DATA_REFRESH_MS);
                next_aircraft_due = now + pdMS_TO_TICKS(
                    aircraft_refresh_delay_ms());
            } else if (aircraft_requested || ticks_due(now, next_aircraft_due)) {
                fetch_aircraft_only();
                refresh_aircraft_metadata_background();
                now = xTaskGetTickCount();
                next_aircraft_due = now + pdMS_TO_TICKS(
                    aircraft_refresh_delay_ms());
            }

            refresh_requested_aircraft_metadata();
            refresh_requested_airline_logo();
            refresh_requested_weather_radar();

            const TickType_t slow_wait = ticks_until(now, next_slow_due);
            const TickType_t aircraft_wait = ticks_until(now, next_aircraft_due);
            TickType_t wait_ticks = slow_wait < aircraft_wait ? slow_wait : aircraft_wait;
            const TickType_t minimum_wait = pdMS_TO_TICKS(250);
            if (wait_ticks < minimum_wait) {
                wait_ticks = minimum_wait;
            }
            if (wait_ticks > pdMS_TO_TICKS(5000)) {
                wait_ticks = pdMS_TO_TICKS(5000);
            }
            ulTaskNotifyTake(pdTRUE, wait_ticks);
        } else {
            set_state(APP_DATA_WAITING_FOR_WIFI);
            if (api_validation_requested) {
                api_validation_requested = false;
                resolve_api_credentials_offline();
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
        }
    }
}

void app_data_start(void)
{
    if (status_mutex == NULL) {
        status_mutex = xSemaphoreCreateMutex();
    }
    BaseType_t ok = xTaskCreateWithCaps(data_task, "airspace_data", DATA_TASK_STACK_BYTES, NULL, 4, &data_task_handle,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "Could not start data task");
        set_state(APP_DATA_ERROR);
    }
}

void app_data_set_paused(bool paused)
{
    data_paused = paused;
    notify_data_task();
}

void app_data_request_refresh(void)
{
    full_refresh_requested = true;
    notify_data_task();
}

void app_data_request_api_validation(void)
{
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    status.flightaware_api_health = APP_API_HEALTH_PENDING;
    status.opensky_api_health = APP_API_HEALTH_PENDING;
    status.airlabs_api_health = APP_API_HEALTH_PENDING;
    status.api_health_revision++;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    api_validation_requested = true;
    notify_data_task();
}

void app_data_request_aircraft_enrichment(const char *icao24, const char *callsign)
{
    if (icao24 == NULL || icao24[0] == '\0') {
        return;
    }
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    copy_string(requested_aircraft_icao24, sizeof(requested_aircraft_icao24), icao24);
    copy_string(requested_aircraft_callsign, sizeof(requested_aircraft_callsign), callsign);
    aircraft_enrichment_requested = true;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    notify_data_task();
}

bool app_data_aircraft_enrichment_pending(const char *icao24)
{
    if (icao24 == NULL || icao24[0] == '\0') {
        return false;
    }
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    const bool pending =
        (aircraft_enrichment_requested &&
         strcasecmp(requested_aircraft_icao24, icao24) == 0) ||
        (aircraft_enrichment_active &&
         strcasecmp(active_aircraft_icao24, icao24) == 0);
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    return pending;
}

void app_data_request_airline_logo(const char *iata)
{
    char normalized[4] = {0};
    if (!normalize_airline_iata(iata, normalized)) {
        return;
    }
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    if (airline_logo_active_buffer >= 0 &&
        strcasecmp(airline_logo_iata, normalized) == 0) {
        if (status_mutex != NULL) {
            xSemaphoreGive(status_mutex);
        }
        return;
    }
    copy_string(requested_airline_logo_iata,
                sizeof(requested_airline_logo_iata),
                normalized);
    airline_logo_requested = true;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    notify_data_task();
}

const uint8_t *app_data_get_airline_logo(const char *iata,
                                         size_t *len,
                                         uint16_t *width,
                                         uint16_t *height,
                                         uint32_t *revision)
{
    const uint8_t *logo = NULL;
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    if (airline_logo_active_buffer >= 0 &&
        iata != NULL &&
        strcasecmp(airline_logo_iata, iata) == 0) {
        logo = airline_logo_buffers[airline_logo_active_buffer];
        if (len != NULL) {
            *len = airline_logo_bytes;
        }
        if (width != NULL) {
            *width = airline_logo_width;
        }
        if (height != NULL) {
            *height = airline_logo_height;
        }
    } else {
        if (len != NULL) {
            *len = 0;
        }
        if (width != NULL) {
            *width = 0;
        }
        if (height != NULL) {
            *height = 0;
        }
    }
    if (revision != NULL) {
        *revision = airline_logo_revision;
    }
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    return logo;
}

void app_data_request_weather_radar(float radius_miles)
{
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    requested_weather_radar_radius_miles = clamp_aircraft_radius_miles(radius_miles);
    copy_string(requested_weather_radar_location_key,
                sizeof(requested_weather_radar_location_key),
                weather_location_key);
    /* The UI must never display a previous city's/radius's radar while the
     * replacement is loading.  The single backing allocation is reused and
     * overwritten by the data task. */
    status.weather_radar_overlay_valid = false;
    status.weather_radar_frame_count = 0;
    weather_radar_observed_frame_count = 0;
    weather_radar_requested = true;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    notify_data_task();
}

void app_data_set_weather_location_key(const char *location_key)
{
    char normalized[sizeof(weather_location_key)] = {0};
    const app_location_preset_t *preset =
        app_location_preset_find(location_key);
    if (preset != NULL) {
        copy_string(normalized, sizeof(normalized), preset->key);
    }

    bool changed = false;
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    if (strcasecmp(weather_location_key, normalized) != 0) {
        copy_string(weather_location_key,
                    sizeof(weather_location_key),
                    normalized);
        status.weather_valid = false;
        status.weather_air_quality_valid = false;
        status.weather_location_key[0] = '\0';
        changed = true;
    }
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    if (changed) {
        full_refresh_requested = true;
        notify_data_task();
    }
}

const uint8_t *app_data_get_weather_radar_overlay(size_t *len, uint32_t *revision)
{
    return app_data_get_weather_radar_overlay_frame(
        APP_WEATHER_RADAR_FRAME_COUNT - 1,
        len,
        revision,
        NULL);
}

const uint8_t *app_data_get_weather_radar_overlay_frame(
    int frame_index,
    size_t *len,
    uint32_t *revision,
    int *frame_unix)
{
    if (len != NULL) {
        *len = 0;
    }
    if (frame_unix != NULL) {
        *frame_unix = 0;
    }

    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    const bool valid =
        status.weather_radar_overlay_valid &&
        weather_radar_overlays != NULL &&
        weather_radar_observed_frame_count > 0 &&
        status.weather_radar_frame_count > 0;
    int selected = frame_index;
    if (selected >= status.weather_radar_frame_count) {
        selected = status.weather_radar_frame_count - 1;
    }
    if (selected < 0) {
        selected = 0;
    }
    if (revision != NULL) {
        *revision = status.weather_radar_overlay_revision;
    }
    if (valid && frame_unix != NULL) {
        *frame_unix = status.weather_radar_frame_unix[selected];
    }
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    if (!valid) {
        return NULL;
    }
    if (len != NULL) {
        *len = APP_WEATHER_RADAR_OVERLAY_BYTES;
    }
    return weather_radar_overlays;
}

void app_data_get_status(app_data_status_t *out)
{
    if (out == NULL) {
        return;
    }
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    *out = status;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
}

const char *app_data_state_text(app_data_state_t state)
{
    switch (state) {
    case APP_DATA_IDLE:
        return "idle";
    case APP_DATA_WAITING_FOR_WIFI:
        return "waiting";
    case APP_DATA_FETCHING:
        return "fetching";
    case APP_DATA_READY:
        return "ready";
    case APP_DATA_ERROR:
        return "error";
    default:
        return "unknown";
    }
}
