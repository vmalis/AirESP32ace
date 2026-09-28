// AirESP32ace
// Created by VM, August 2026

#include "remote.h"
#include "tracker.h"
#include "launches.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "aircraft_icons.h"
#include "aircraft_type_registry.h"
#include "app_config.h"
#include "cJSON.h"
#include "data.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "locations.h"
#include "mbedtls/base64.h"
#include "network.h"
#include "power_status.h"
#include "wear_levelling.h"

static const char *TAG = "airspace_remote";
#define APP_REMOTE_STORE_FULL_MAP 1
#define APP_REMOTE_STATUS_JSON_MAX 2800
#define MAP_STORE_BUFFER_COUNT 1
#define MAP_STORAGE_BASE_PATH "/spiffs"
#define SHARED_CONFIG_PATH MAP_STORAGE_BASE_PATH "/config.jsn"
#define SHARED_CONFIG_TEMP_PATH MAP_STORAGE_BASE_PATH "/config.tmp"
#define SHARED_CONFIG_SCHEMA_VERSION 1
#define USB_CONFIG_LINE_MAX (24 * 1024)
#define USB_FILE_DECODE_BUFFER_SIZE (12 * 1024)
#define USB_CONFIG_SET_PREFIX "ASTCFG:SET:"
#define USB_CONFIG_GET_COMMAND "ASTCFG:GET"
#define USB_CONFIG_PING_COMMAND "ASTCFG:PING"
#define USB_CONFIG_PONG_PREFIX "ASTCFG:PONG:"
#define USB_CONFIG_CAPS_COMMAND "ASTCFG:CAPS"
#define USB_CONFIG_CAPS_PREFIX "ASTCFG:CAPS:"
#define USB_CONFIG_OK_PREFIX "ASTCFG:OK:"
#define USB_CONFIG_ERROR_PREFIX "ASTCFG:ERROR:"
#define USB_FILE_BEGIN_PREFIX "ASTFILE:BEGIN:"
#define USB_FILE_PREPARE_COMMAND "ASTFILE:PREPARE"
#define USB_FILE_PREPARED_PREFIX "ASTFILE:PREPARED:"
#define USB_FILE_CHUNK_PREFIX "ASTFILE:CHUNK:"
#define USB_FILE_DATA_PREFIX "ASTFILE:DATA:"
#define USB_FILE_END_COMMAND "ASTFILE:END"
#define USB_FILE_READY_PREFIX "ASTFILE:READY:"
#define USB_FILE_CACHED_PREFIX "ASTFILE:CACHED:"
#define USB_FILE_ACK_PREFIX "ASTFILE:ACK:"
#define USB_FILE_DONE_PREFIX "ASTFILE:DONE:"
#define USB_FILE_ERROR_PREFIX "ASTFILE:ERROR:"
#define USB_STATE_SET_PREFIX "ASTSTATE:SET:"
#define USB_STATE_OK_PREFIX "ASTSTATE:OK:"
#define USB_STATE_ERROR_PREFIX "ASTSTATE:ERROR:"
#define USB_FILE_BINARY_CHUNK_MAX (64 * 1024)
#define USB_CONFIG_READ_BUFFER_SIZE (4 * 1024)
#define USB_FILE_IO_BUFFER_SIZE (64 * 1024)
#define MAP_LOCATION_KEY_MAX 16
#define TRACKED_STATE_PATH MAP_STORAGE_BASE_PATH "/tracked.bin"
#define TRACKED_STATE_TEMP_PATH MAP_STORAGE_BASE_PATH "/tracked.tmp"
#define TRACKED_STATE_MAGIC 0x52414452UL
#define TRACKED_STATE_VERSION 11UL
#define TRACKED_STATE_V3_VERSION 3UL
#define TRACKED_STATE_V4_VERSION 4UL
#define TRACKED_STATE_V5_VERSION 5UL
#define TRACKED_STATE_V6_VERSION 6UL
#define TRACKED_STATE_V7_VERSION 7UL
#define TRACKED_STATE_V8_VERSION 8UL
#define TRACKED_STATE_V9_VERSION 9UL
#define TRACKED_STATE_V10_VERSION 10UL
#define TRACKED_STATE_V4_MODEL_BYTES 40U
#define TRACKED_STATE_V3_PAYLOAD_BYTES \
    offsetof(app_remote_status_t, tracked_route_geometry_version)
#define TRACKED_STATE_V5_PAYLOAD_BYTES \
    offsetof(app_remote_status_t, tracked_flight_journey_phase)
#define TRACKED_STATE_V6_PAYLOAD_BYTES \
    offsetof(app_remote_status_t, launch_valid)
#define TRACKED_STATE_V7_PAYLOAD_BYTES \
    offsetof(app_remote_status_t, launch_current_utc_offset_s)
#define TRACKED_STATE_V8_PAYLOAD_BYTES \
    offsetof(app_remote_status_t, launch_options)
#define TRACKED_STATE_V9_PAYLOAD_BYTES \
    offsetof(app_remote_status_t, tracked_flight_origin_terminal)
#define TRACKED_STATE_V10_PAYLOAD_BYTES \
    offsetof(app_remote_status_t, tracked_flight_slot)
#define TRACKED_FLIGHT_SLOT_COUNT 4
#define TRACKED_ASSET_MANIFEST_PATH MAP_STORAGE_BASE_PATH "/tfh.bin"
#define TRACKED_ASSET_MANIFEST_TEMP_PATH MAP_STORAGE_BASE_PATH "/tfh.tmp"
#define TRACKED_ASSET_MANIFEST_MAGIC 0x5446414dUL
#define TRACKED_ASSET_MANIFEST_VERSION 1U
#define TRACKED_ASSET_KIND_COUNT 4

static app_remote_status_t *status;
static bool tracker_owns_display;
static bool launches_own_display;

static void copy_tracker_fields(app_remote_status_t *to, const app_remote_status_t *from)
{
    /* Preserve unrelated weather, maps, launch state and transport counters. */
    memcpy((uint8_t *)to + offsetof(app_remote_status_t, route_progress),
           (const uint8_t *)from + offsetof(app_remote_status_t, route_progress),
           offsetof(app_remote_status_t, map_valid) - offsetof(app_remote_status_t, route_progress));
    memcpy((uint8_t *)to + offsetof(app_remote_status_t, tracked_route_geometry_version),
           (const uint8_t *)from + offsetof(app_remote_status_t, tracked_route_geometry_version),
           offsetof(app_remote_status_t, launch_valid) - offsetof(app_remote_status_t, tracked_route_geometry_version));
    memcpy((uint8_t *)to + offsetof(app_remote_status_t, tracked_flight_origin_terminal),
           (const uint8_t *)from + offsetof(app_remote_status_t, tracked_flight_origin_terminal),
           sizeof(*to) - offsetof(app_remote_status_t, tracked_flight_origin_terminal));
}

static SemaphoreHandle_t status_mutex;
static SemaphoreHandle_t config_mutex;
static httpd_handle_t server;
static int usb_config_fd = -1;
static bool usb_config_driver_ready;
static wl_handle_t map_storage_wl_handle = WL_INVALID_HANDLE;
static const float radar_map_ranges[APP_REMOTE_RADAR_MAP_RANGE_COUNT] = {10.0f, 20.0f, 50.0f};

typedef struct {
    uint8_t *buffers[MAP_STORE_BUFFER_COUNT];
    int active_index;
    char active_location_key[MAP_LOCATION_KEY_MAX];
    uint32_t active_revision;
} map_store_t;

static map_store_t route_map_store = {
    .active_index = -1,
};
static map_store_t radar_map_stores[APP_REMOTE_RADAR_MAP_RANGE_COUNT] = {
    [0] = {.active_index = -1},
    [1] = {.active_index = -1},
    [2] = {.active_index = -1},
};
static uint8_t *route_flag_buffers[2];
static uint32_t route_flag_loaded_revision[2] = {UINT32_MAX, UINT32_MAX};
static uint8_t *weather_city_icon_buffer;
static char weather_city_icon_key[MAP_LOCATION_KEY_MAX];
static uint32_t weather_city_icon_revision = 1;
static volatile uint32_t weather_city_icon_valid_mask_cache;

typedef enum {
    USB_FILE_NONE = 0,
    USB_FILE_ROUTE_GLOBE,
    USB_FILE_ROUTE_FRAME,
    USB_FILE_FLAG_ORIGIN,
    USB_FILE_FLAG_DESTINATION,
} usb_file_kind_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t hashes[TRACKED_FLIGHT_SLOT_COUNT][TRACKED_ASSET_KIND_COUNT];
} tracked_asset_manifest_t;

typedef struct {
    usb_file_kind_t kind;
    uint8_t slot;
    FILE *file;
    char name[24];
    char temporary_path[96];
    char final_path[96];
    size_t expected_size;
    size_t received_size;
    uint32_t expected_hash;
    uint32_t running_hash;
    size_t binary_remaining;
    uint8_t *io_buffer;
    TickType_t last_activity_tick;
} usb_file_transfer_t;

static usb_file_transfer_t usb_file_transfer;
static tracked_asset_manifest_t tracked_asset_manifest;
static uint8_t *usb_file_decode_buffer;
static bool usb_package_active;
static TickType_t usb_package_last_activity_tick;

static esp_err_t apply_state_root(cJSON *root);

typedef struct {
    const char *name;
    const char *storage_name;
    uint16_t width;
    uint16_t height;
    bool has_alpha;
    uint32_t expected_fnv1a;
    uint8_t *buffer;
    uint32_t revision;
} space_asset_t;

static space_asset_t space_assets[] = {
    {.name = "sun", .width = 128, .height = 128, .has_alpha = true, .expected_fnv1a = 0xfa4dfd11, .revision = 1},
    {.name = "earth", .width = 80, .height = 80, .has_alpha = true, .expected_fnv1a = 0xff25238e, .revision = 1},
    {.name = "moon", .width = 64, .height = 64, .has_alpha = true, .expected_fnv1a = 0xa7062955, .revision = 1},
    {.name = "iss", .width = 64, .height = 64, .has_alpha = true, .expected_fnv1a = 0xd0165cbd, .revision = 1},
    {.name = "world", .width = APP_REMOTE_SPACE_WORLD_WIDTH, .height = APP_REMOTE_SPACE_WORLD_HEIGHT, .has_alpha = false, .expected_fnv1a = 0xd2a75cc8, .revision = 1},
    {.name = "nasa", .width = 72, .height = 60, .has_alpha = true, .expected_fnv1a = 0x0bbba422, .revision = 1},
    {.name = "roscosm", .width = 60, .height = 60, .has_alpha = true, .expected_fnv1a = 0x40e12142, .revision = 1},
    {.name = "launch_sls", .storage_name = "ls", .width = 140, .height = 300, .has_alpha = true, .expected_fnv1a = 0x9573288d, .revision = 1},
    {.name = "launch_falcon9", .storage_name = "lf", .width = 140, .height = 300, .has_alpha = true, .expected_fnv1a = 0x7af819fe, .revision = 1},
    {.name = "launch_soyuz", .storage_name = "ly", .width = 140, .height = 300, .has_alpha = true, .expected_fnv1a = 0x7a6fa8a0, .revision = 1},
    {.name = "launch_starship", .storage_name = "lt", .width = 140, .height = 300, .has_alpha = true, .expected_fnv1a = 0x733e6b10, .revision = 1},
    {.name = "launch_spacex", .storage_name = "lx", .width = 144, .height = 32, .has_alpha = true, .expected_fnv1a = 0x77d95c76, .revision = 1},
    {.name = "launch_florida", .storage_name = "lfl", .width = APP_REMOTE_SPACE_WORLD_WIDTH, .height = APP_REMOTE_SPACE_WORLD_HEIGHT, .has_alpha = false, .expected_fnv1a = 0x2f20fd1b, .revision = 1},
    {.name = "launch_starbase", .storage_name = "lsb", .width = APP_REMOTE_SPACE_WORLD_WIDTH, .height = APP_REMOTE_SPACE_WORLD_HEIGHT, .has_alpha = false, .expected_fnv1a = 0x0112ea62, .revision = 1},
    {.name = "launch_baikonur", .storage_name = "lbk", .width = APP_REMOTE_SPACE_WORLD_WIDTH, .height = APP_REMOTE_SPACE_WORLD_HEIGHT, .has_alpha = false, .expected_fnv1a = 0xd4963629, .revision = 1},
    {.name = "iss_flags", .storage_name = "ifg", .width = 402, .height = 131, .has_alpha = true, .expected_fnv1a = 0x9e49fed5, .revision = 3},
};
#define SPACE_ASSET_COUNT \
    ((int)(sizeof(space_assets) / sizeof(space_assets[0])))
static volatile uint32_t launch_asset_valid_mask_cache;
static volatile uint32_t space_asset_valid_mask_cache;
static volatile uint32_t space_asset_load_failures;
static SemaphoreHandle_t space_asset_mutex;

static int launch_agency_index_for_text(const char *agency);
static void copy_legacy_launch_to_option(
    const app_remote_status_t *source,
    app_remote_launch_t *destination);
static void copy_launch_option_to_legacy(
    const app_remote_launch_t *source,
    app_remote_status_t *destination);

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t payload_size;
    app_remote_status_t payload;
} persisted_tracked_state_t;

static app_remote_status_t *ensure_status_storage(void)
{
    if (status != NULL) {
        return status;
    }
    status = (app_remote_status_t *)heap_caps_calloc(
        1,
        sizeof(*status),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (status == NULL) {
        ESP_LOGE(TAG, "Could not allocate remote status in PSRAM");
        return NULL;
    }
    status->style = APP_REMOTE_STYLE_XDR_RADAR;
    status->route_progress = 0.46f;
    return status;
}

static void read_status_into(
    app_remote_status_t *copy)
{
    if (copy == NULL) {
        return;
    }
    if (ensure_status_storage() == NULL) {
        memset(copy, 0, sizeof(*copy));
        copy->style = APP_REMOTE_STYLE_XDR_RADAR;
        copy->route_progress = 0.46f;
        return;
    }
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    *copy = *status;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
}

static app_remote_status_t read_status(void)
{
    app_remote_status_t copy;
    read_status_into(&copy);
    return copy;
}

static void write_status(const app_remote_status_t *next)
{
    if (next == NULL) {
        return;
    }
    if (ensure_status_storage() == NULL) {
        return;
    }
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    /* Do not stage another full status on the caller's stack. Large status
     * snapshots in the map-loading chain exhausted the 16 KiB LVGL stack
     * when opening Tracker, corrupting heap memory before a task switch. */
    app_remote_merge_status(status, next, tracker_owns_display, launches_own_display);
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
}

#if APP_REMOTE_STORE_FULL_MAP
static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static __attribute__((noinline)) bool ensure_map_buffers(map_store_t *store, const char *label)
{
    for (int i = 0; i < MAP_STORE_BUFFER_COUNT; i++) {
        if (store->buffers[i] != NULL) {
            continue;
        }
        store->buffers[i] = (uint8_t *)heap_caps_malloc(APP_REMOTE_MAP_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (store->buffers[i] == NULL) {
            /* The compiler reserves error-path locals even when allocation
             * succeeds. A by-value status read used 6 KiB here, nested below
             * Tracker's render and map snapshots. Keep this leaf bounded. */
            if (ensure_status_storage() != NULL) {
                if (status_mutex != NULL) xSemaphoreTake(status_mutex, portMAX_DELAY);
                snprintf(status->last_error, sizeof(status->last_error),
                         "no PSRAM for %s map buffers", label);
                status->revision++;
                if (status_mutex != NULL) xSemaphoreGive(status_mutex);
            }
            ESP_LOGW(TAG, "No PSRAM for %s map buffer %d", label, i);
            return false;
        }
    }
    return true;
}
#endif

static void location_key_for_storage(const char *input, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }

    size_t w = 0;
    const char *src = (input != NULL && input[0] != '\0') ? input : "SJC";
    for (size_t i = 0; src[i] != '\0' && w + 1 < out_size; i++) {
        const char c = src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            out[w++] = c;
        } else if (c >= 'a' && c <= 'z') {
            out[w++] = (char)(c - 'a' + 'A');
        }
    }
    if (w == 0) {
        out[w++] = 'S';
        if (w + 1 < out_size) {
            out[w++] = 'J';
        }
        if (w + 1 < out_size) {
            out[w++] = 'C';
        }
    }
    out[w] = '\0';
}

static void selected_location_key(char *out, size_t out_size)
{
    app_config_t cfg;
    app_config_get(&cfg);
    const app_location_preset_t *preset = app_location_preset_for_config(&cfg);
    location_key_for_storage(preset != NULL ? preset->key : cfg.location_key, out, out_size);
}

static int tracked_flight_slot_clamped(int slot)
{
    return slot >= 0 && slot < TRACKED_FLIGHT_SLOT_COUNT ? slot : 0;
}

static int active_tracked_flight_slot(void)
{
    return tracked_flight_slot_clamped(read_status().tracked_flight_slot);
}

static void route_map_path_for_slot_view(int slot,
                                         int view_index,
                                         char *path,
                                         size_t path_size)
{
    slot = tracked_flight_slot_clamped(slot);
    const char *stem = view_index == 1 ? "rf" : "rg";
    if (slot == 0) {
        snprintf(path, path_size, MAP_STORAGE_BASE_PATH "/%s.rgb", stem);
    } else {
        snprintf(path, path_size, MAP_STORAGE_BASE_PATH "/%s%d.rgb", stem, slot);
    }
}

static void route_map_path_for_view(int view_index, char *path, size_t path_size)
{
    route_map_path_for_slot_view(
        active_tracked_flight_slot(), view_index, path, path_size);
}

static void route_flag_path_for_slot_endpoint(int slot,
                                              int endpoint_index,
                                              char *path,
                                              size_t path_size)
{
    slot = tracked_flight_slot_clamped(slot);
    const char *stem = endpoint_index == 1 ? "fd" : "fo";
    if (slot == 0) {
        snprintf(path, path_size, MAP_STORAGE_BASE_PATH "/%s.rgb", stem);
    } else {
        snprintf(path, path_size, MAP_STORAGE_BASE_PATH "/%s%d.rgb", stem, slot);
    }
}

static void route_flag_path_for_endpoint(int endpoint_index,
                                         char *path,
                                         size_t path_size)
{
    route_flag_path_for_slot_endpoint(
        active_tracked_flight_slot(), endpoint_index, path, path_size);
}

static void radar_map_path_for_key(const char *location_key, int range_index, char *path, size_t path_size)
{
    char key[MAP_LOCATION_KEY_MAX];
    location_key_for_storage(location_key, key, sizeof(key));
    snprintf(path,
             path_size,
             MAP_STORAGE_BASE_PATH "/r%s%03d.rgb",
             key,
             (int)radar_map_ranges[range_index]);
}

static void weather_city_icon_path_for_key(const char *location_key,
                                           char *path,
                                           size_t path_size)
{
    char key[MAP_LOCATION_KEY_MAX];
    location_key_for_storage(location_key, key, sizeof(key));
    snprintf(
        path,
        path_size,
        MAP_STORAGE_BASE_PATH "/cities/%s.rga",
        key);
}

static space_asset_t *space_asset_for_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (int index = 0; index < SPACE_ASSET_COUNT; index++) {
        if (strcasecmp(space_assets[index].name, name) == 0) {
            return &space_assets[index];
        }
    }
    return NULL;
}

static size_t space_asset_bytes(const space_asset_t *asset)
{
    return asset == NULL
        ? 0
        : (size_t)asset->width *
            (size_t)asset->height *
            (asset->has_alpha ? 3U : 2U);
}

static void space_asset_path(const space_asset_t *asset,
                             char *path,
                             size_t path_size)
{
    snprintf(path,
             path_size,
             MAP_STORAGE_BASE_PATH "/space/%s.%s",
             asset->storage_name != NULL
                 ? asset->storage_name
                 : asset->name,
             asset->has_alpha ? "rga" : "rgb");
}

static bool file_has_exact_size(const char *path, size_t expected_size)
{
    struct stat st;
    return stat(path, &st) == 0 && st.st_size == (off_t)expected_size;
}

static int tracked_asset_kind_index(usb_file_kind_t kind)
{
    switch (kind) {
        case USB_FILE_ROUTE_GLOBE: return 0;
        case USB_FILE_ROUTE_FRAME: return 1;
        case USB_FILE_FLAG_ORIGIN: return 2;
        case USB_FILE_FLAG_DESTINATION: return 3;
        default: return -1;
    }
}

static void tracked_asset_manifest_reset(void)
{
    memset(&tracked_asset_manifest, 0, sizeof(tracked_asset_manifest));
    tracked_asset_manifest.magic = TRACKED_ASSET_MANIFEST_MAGIC;
    tracked_asset_manifest.version = TRACKED_ASSET_MANIFEST_VERSION;
}

static void tracked_asset_manifest_load(void)
{
    tracked_asset_manifest_reset();
    FILE *file = fopen(TRACKED_ASSET_MANIFEST_PATH, "rb");
    if (file == NULL) {
        return;
    }
    tracked_asset_manifest_t stored = {0};
    const bool valid =
        fread(&stored, 1, sizeof(stored), file) == sizeof(stored) &&
        stored.magic == TRACKED_ASSET_MANIFEST_MAGIC &&
        stored.version == TRACKED_ASSET_MANIFEST_VERSION;
    fclose(file);
    if (valid) {
        tracked_asset_manifest = stored;
    }
}

static bool tracked_asset_manifest_save(void)
{
    FILE *file = fopen(TRACKED_ASSET_MANIFEST_TEMP_PATH, "wb");
    if (file == NULL) {
        return false;
    }
    const bool payload_written =
        fwrite(&tracked_asset_manifest,
               1,
               sizeof(tracked_asset_manifest),
               file) == sizeof(tracked_asset_manifest);
    const bool written = fclose(file) == 0 && payload_written;
    if (!written) {
        unlink(TRACKED_ASSET_MANIFEST_TEMP_PATH);
        return false;
    }
    unlink(TRACKED_ASSET_MANIFEST_PATH);
    if (rename(TRACKED_ASSET_MANIFEST_TEMP_PATH,
               TRACKED_ASSET_MANIFEST_PATH) != 0) {
        unlink(TRACKED_ASSET_MANIFEST_TEMP_PATH);
        return false;
    }
    return true;
}

static void tracked_asset_manifest_clear_slot(int slot)
{
    if (slot < 0 || slot >= TRACKED_FLIGHT_SLOT_COUNT) {
        return;
    }
    memset(tracked_asset_manifest.hashes[slot],
           0,
           sizeof(tracked_asset_manifest.hashes[slot]));
}

static bool tracked_asset_manifest_record(usb_file_kind_t kind,
                                          int slot,
                                          uint32_t hash)
{
    const int index = tracked_asset_kind_index(kind);
    if (index < 0 || slot < 0 || slot >= TRACKED_FLIGHT_SLOT_COUNT) {
        return false;
    }
    tracked_asset_manifest.hashes[slot][index] = hash;
    return tracked_asset_manifest_save();
}

static uint32_t fnv1a_update(
    uint32_t hash,
    const uint8_t *data,
    size_t length)
{
    for (size_t index = 0; index < length; index++) {
        hash ^= data[index];
        hash *= 16777619UL;
    }
    return hash;
}

static bool space_asset_file_is_valid(
    const space_asset_t *asset)
{
    if (asset == NULL) {
        return false;
    }
    const size_t expected = space_asset_bytes(asset);
    char path[96];
    space_asset_path(asset, path, sizeof(path));
    if (!file_has_exact_size(path, expected)) {
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    uint8_t scratch[1024];
    size_t total = 0;
    uint32_t hash = 2166136261UL;
    while (total < expected) {
        const size_t remaining = expected - total;
        const size_t wanted =
            remaining < sizeof(scratch)
                ? remaining
                : sizeof(scratch);
        const size_t count = fread(scratch, 1, wanted, file);
        if (count == 0) {
            break;
        }
        hash = fnv1a_update(hash, scratch, count);
        total += count;
    }
    fclose(file);
    return total == expected && hash == asset->expected_fnv1a;
}

static uint32_t space_asset_expected_mask(void)
{
    return SPACE_ASSET_COUNT >= 32
        ? UINT32_MAX
        : ((1UL << SPACE_ASSET_COUNT) - 1UL);
}

static uint32_t space_asset_loaded_mask(void)
{
    uint32_t mask = 0;
    if (space_asset_mutex != NULL) {
        xSemaphoreTake(space_asset_mutex, portMAX_DELAY);
    }
    for (int index = 0; index < SPACE_ASSET_COUNT; index++) {
        if (space_assets[index].buffer != NULL) {
            mask |= 1UL << index;
        }
    }
    if (space_asset_mutex != NULL) {
        xSemaphoreGive(space_asset_mutex);
    }
    return mask;
}

static void refresh_weather_city_icon_valid_mask(void)
{
    uint32_t mask = 0;
    for (size_t index = 0;
         index < app_location_preset_count() && index < 32;
         index++) {
        const app_location_preset_t *preset =
            app_location_preset_at(index);
        if (preset == NULL) {
            continue;
        }
        char path[96];
        weather_city_icon_path_for_key(
            preset->key,
            path,
            sizeof(path));
        if (file_has_exact_size(
                path,
                APP_REMOTE_WEATHER_CITY_ICON_BYTES)) {
            mask |= 1UL << index;
        }
    }
    weather_city_icon_valid_mask_cache = mask;
}

static uint32_t route_map_valid_mask_for_slot(int slot)
{
    uint32_t mask = 0;
    for (int i = 0; i < APP_REMOTE_ROUTE_MAP_VIEW_COUNT; i++) {
        char path[96];
        route_map_path_for_slot_view(slot, i, path, sizeof(path));
        if (file_has_exact_size(path, APP_REMOTE_MAP_BYTES)) {
            mask |= (1UL << i);
        }
    }
    return mask;
}

static uint32_t route_map_valid_mask(void)
{
    return route_map_valid_mask_for_slot(active_tracked_flight_slot());
}

static uint32_t route_flag_valid_mask_for_slot(int slot)
{
    uint32_t mask = 0;
    for (int index = 0; index < 2; index++) {
        char path[96];
        route_flag_path_for_slot_endpoint(slot, index, path, sizeof(path));
        if (file_has_exact_size(path, APP_REMOTE_ROUTE_FLAG_BYTES)) {
            mask |= (1UL << index);
        }
    }
    return mask;
}

static uint32_t route_flag_valid_mask(void)
{
    return route_flag_valid_mask_for_slot(active_tracked_flight_slot());
}

static void persist_tracked_state(const app_remote_status_t *state)
{
    if (state == NULL) {
        return;
    }
    const uint32_t header_values[3] = {
        TRACKED_STATE_MAGIC,
        TRACKED_STATE_VERSION,
        sizeof(app_remote_status_t),
    };
    uint8_t header[offsetof(persisted_tracked_state_t, payload)] = {0};
    memcpy(header, header_values, sizeof(header_values));
    FILE *file = fopen(TRACKED_STATE_TEMP_PATH, "wb");
    if (file == NULL) {
        ESP_LOGW(TAG, "Could not open tracked-state temp file: errno=%d", errno);
        return;
    }
    const bool payload_written =
        fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
        fwrite(state, 1, sizeof(*state), file) == sizeof(*state);
    const bool written = fclose(file) == 0 && payload_written;
    if (!written) {
        unlink(TRACKED_STATE_TEMP_PATH);
        ESP_LOGW(TAG, "Could not save tracked-flight state");
        return;
    }
    unlink(TRACKED_STATE_PATH);
    if (rename(TRACKED_STATE_TEMP_PATH, TRACKED_STATE_PATH) != 0) {
        unlink(TRACKED_STATE_TEMP_PATH);
        ESP_LOGW(TAG, "Could not finalize tracked-flight state: errno=%d", errno);
    }
}

static void clear_tracked_flight_state(app_remote_status_t *state)
{
    if (state == NULL) {
        return;
    }
    state->route_progress = 0.0f;
    state->tracked_flight_valid = false;
    state->tracked_flight_delayed = false;
    state->tracked_flight_number[0] = '\0';
    state->tracked_flight_callsign[0] = '\0';
    state->tracked_flight_airline_iata[0] = '\0';
    state->tracked_flight_airline_name[0] = '\0';
    state->tracked_flight_aircraft_type[0] = '\0';
    state->tracked_flight_origin[0] = '\0';
    state->tracked_flight_origin_city[0] = '\0';
    state->tracked_flight_origin_country[0] = '\0';
    state->tracked_flight_destination[0] = '\0';
    state->tracked_flight_destination_city[0] = '\0';
    state->tracked_flight_destination_country[0] = '\0';
    state->tracked_flight_origin_terminal[0] = '\0';
    state->tracked_flight_origin_gate[0] = '\0';
    state->tracked_flight_destination_terminal[0] = '\0';
    state->tracked_flight_destination_gate[0] = '\0';
    state->tracked_flight_status[0] = '\0';
    state->tracked_flight_duration[0] = '\0';
    state->tracked_flight_distance[0] = '\0';
    state->tracked_flight_eta[0] = '\0';
    state->tracked_flight_elapsed[0] = '\0';
    state->tracked_flight_remaining[0] = '\0';
    state->tracked_flight_phase[0] = '\0';
    state->tracked_flight_journey_phase[0] = '\0';
    state->tracked_flight_altitude_m = 0;
    state->tracked_flight_speed_kph = 0;
    state->tracked_flight_heading_deg = 0;
    state->tracked_flight_heading_valid = false;
    state->tracked_flight_route_distance_km = 0;
    state->tracked_flight_origin_current_utc_offset_s = 0;
    state->tracked_flight_destination_current_utc_offset_s = 0;
    state->tracked_flight_departure_utc_offset_s = 0;
    state->tracked_flight_arrival_utc_offset_s = 0;
    state->tracked_flight_estimated_departure_unix = 0;
    state->tracked_flight_actual_departure_unix = 0;
    state->tracked_flight_estimated_arrival_unix = 0;
    state->tracked_flight_actual_arrival_unix = 0;
    state->tracked_route_point_count = 0;
    memset(
        state->tracked_route_points,
        0,
        sizeof(state->tracked_route_points));
    state->tracked_route_geometry_version++;
}

static void load_persisted_tracked_state(void)
{
    FILE *file = fopen(TRACKED_STATE_PATH, "rb");
    if (file == NULL) {
        return;
    }

    uint32_t header[3] = {0};
    app_remote_status_t restored = {0};
    bool read_ok = false;
    bool migrated = false;
    if (fread(header, 1, sizeof(header), file) == sizeof(header) &&
        header[0] == TRACKED_STATE_MAGIC) {
        if (header[1] == TRACKED_STATE_VERSION &&
            header[2] == sizeof(app_remote_status_t)) {
            if (fseek(
                    file,
                    (long)offsetof(persisted_tracked_state_t, payload),
                    SEEK_SET) == 0 &&
                fread(&restored, 1, sizeof(restored), file) ==
                    sizeof(restored)) {
                read_ok = true;
            }
        } else if (
            header[1] == TRACKED_STATE_V10_VERSION &&
            header[2] == TRACKED_STATE_V10_PAYLOAD_BYTES) {
            if (fseek(
                    file,
                    (long)offsetof(
                        persisted_tracked_state_t,
                        payload),
                    SEEK_SET) == 0 &&
                fread(
                    &restored,
                    1,
                    TRACKED_STATE_V10_PAYLOAD_BYTES,
                    file) ==
                    TRACKED_STATE_V10_PAYLOAD_BYTES) {
                read_ok = true;
                migrated = true;
            }
        } else if (
            header[1] == TRACKED_STATE_V9_VERSION &&
            header[2] == TRACKED_STATE_V9_PAYLOAD_BYTES) {
            if (fseek(
                    file,
                    (long)offsetof(
                        persisted_tracked_state_t,
                        payload),
                    SEEK_SET) == 0 &&
                fread(
                    &restored,
                    1,
                    TRACKED_STATE_V9_PAYLOAD_BYTES,
                    file) ==
                    TRACKED_STATE_V9_PAYLOAD_BYTES) {
                read_ok = true;
                migrated = true;
            }
        } else if (
            header[1] == TRACKED_STATE_V3_VERSION &&
            header[2] == TRACKED_STATE_V3_PAYLOAD_BYTES) {
            if (fseek(
                    file,
                    (long)offsetof(
                        persisted_tracked_state_t,
                        payload),
                    SEEK_SET) == 0 &&
                fread(
                    &restored,
                    1,
                    TRACKED_STATE_V3_PAYLOAD_BYTES,
                    file) ==
                    TRACKED_STATE_V3_PAYLOAD_BYTES) {
                read_ok = true;
                migrated = true;
            }
        } else if (
            header[1] == TRACKED_STATE_V4_VERSION &&
            header[2] ==
                TRACKED_STATE_V3_PAYLOAD_BYTES +
                    TRACKED_STATE_V4_MODEL_BYTES) {
            uint8_t legacy_payload[
                TRACKED_STATE_V3_PAYLOAD_BYTES +
                    TRACKED_STATE_V4_MODEL_BYTES];
            const size_t prefix_bytes =
                offsetof(app_remote_status_t, tracked_flight_origin);
            if (fseek(
                    file,
                    (long)offsetof(persisted_tracked_state_t, payload),
                    SEEK_SET) == 0 &&
                fread(
                    legacy_payload,
                    1,
                    sizeof(legacy_payload),
                    file) == sizeof(legacy_payload)) {
                memcpy(&restored, legacy_payload, prefix_bytes);
                memcpy(
                    (uint8_t *)&restored + prefix_bytes,
                    legacy_payload + prefix_bytes +
                        TRACKED_STATE_V4_MODEL_BYTES,
                    TRACKED_STATE_V3_PAYLOAD_BYTES -
                        prefix_bytes);
                read_ok = true;
                migrated = true;
            }
        } else if (
            header[1] == TRACKED_STATE_V5_VERSION &&
            header[2] == TRACKED_STATE_V5_PAYLOAD_BYTES) {
            if (fseek(
                    file,
                    (long)offsetof(
                        persisted_tracked_state_t,
                        payload),
                    SEEK_SET) == 0 &&
                fread(
                    &restored,
                    1,
                    TRACKED_STATE_V5_PAYLOAD_BYTES,
                    file) ==
                    TRACKED_STATE_V5_PAYLOAD_BYTES) {
                read_ok = true;
                migrated = true;
            }
        } else if (
            header[1] == TRACKED_STATE_V6_VERSION &&
            header[2] == TRACKED_STATE_V6_PAYLOAD_BYTES) {
            if (fseek(
                    file,
                    (long)offsetof(
                        persisted_tracked_state_t,
                        payload),
                    SEEK_SET) == 0 &&
                fread(
                    &restored,
                    1,
                    TRACKED_STATE_V6_PAYLOAD_BYTES,
                    file) ==
                    TRACKED_STATE_V6_PAYLOAD_BYTES) {
                read_ok = true;
                migrated = true;
            }
        } else if (
            header[1] == TRACKED_STATE_V7_VERSION &&
            header[2] == TRACKED_STATE_V7_PAYLOAD_BYTES) {
            if (fseek(
                    file,
                    (long)offsetof(
                        persisted_tracked_state_t,
                        payload),
                    SEEK_SET) == 0 &&
                fread(
                    &restored,
                    1,
                    TRACKED_STATE_V7_PAYLOAD_BYTES,
                    file) ==
                    TRACKED_STATE_V7_PAYLOAD_BYTES) {
                restored
                    .launch_weather_probability_percent =
                    -1;
                read_ok = true;
                migrated = true;
            }
        } else if (
            header[1] == TRACKED_STATE_V8_VERSION &&
            header[2] == TRACKED_STATE_V8_PAYLOAD_BYTES) {
            if (fseek(
                    file,
                    (long)offsetof(
                        persisted_tracked_state_t,
                        payload),
                    SEEK_SET) == 0 &&
                fread(
                    &restored,
                    1,
                    TRACKED_STATE_V8_PAYLOAD_BYTES,
                    file) ==
                    TRACKED_STATE_V8_PAYLOAD_BYTES) {
                read_ok = true;
                migrated = true;
            }
        }
    }
    fclose(file);
    if (!read_ok) {
        ESP_LOGW(TAG, "Ignoring incompatible tracked-flight state");
        return;
    }

    if (restored.launch_valid &&
        restored.launch_option_mask == 0) {
        const int agency_index =
            launch_agency_index_for_text(
                restored.launch_agency);
        if (agency_index >= 0) {
            copy_legacy_launch_to_option(
                &restored,
                &restored.launch_options[agency_index]);
            restored.launch_option_mask |=
                (uint8_t)(1U << agency_index);
        }
    }
    restored.revision++;
    write_status(&restored);
    if (migrated) {
        persist_tracked_state(&restored);
        ESP_LOGI(
            TAG,
            "Migrated tracked-flight state to current format");
    }
    if (restored.tracked_flight_valid) {
        ESP_LOGI(TAG,
                 "Restored tracked flight %s at %.0f%%",
                 restored.tracked_flight_number,
                 (double)restored.route_progress * 100.0);
    } else {
        ESP_LOGI(TAG,
                 "Restored inactive flight map for %s",
                 restored.tracked_flight_number);
    }
}

static uint32_t radar_map_mask_for_location_key(const char *location_key)
{
    uint32_t mask = 0;
    for (int i = 0; i < APP_REMOTE_RADAR_MAP_RANGE_COUNT; i++) {
        char path[96];
        radar_map_path_for_key(location_key, i, path, sizeof(path));
        /* Inventory is queried frequently by the companion. Reading every
         * byte of all 24 cached maps here can monopolize the HTTP task for
         * several seconds. Exact file size is sufficient for inventory;
         * the normal map loader still verifies a complete read before use. */
        if (file_has_exact_size(path, APP_REMOTE_MAP_BYTES)) {
            mask |= (1UL << i);
        }
    }
    return mask;
}

static void release_map_store_buffer(map_store_t *store)
{
    if (store == NULL) {
        return;
    }
    for (int i = 0; i < MAP_STORE_BUFFER_COUNT; i++) {
        if (store->buffers[i] != NULL) {
            heap_caps_free(store->buffers[i]);
            store->buffers[i] = NULL;
        }
    }
    store->active_index = -1;
    store->active_location_key[0] = '\0';
    store->active_revision = 0;
}

static void release_radar_map_buffers_except(int keep_index)
{
    for (int i = 0; i < APP_REMOTE_RADAR_MAP_RANGE_COUNT; i++) {
        if (i == keep_index) {
            continue;
        }
        release_map_store_buffer(&radar_map_stores[i]);
    }
}

static bool load_map_file_into_store(map_store_t *store,
                                     const char *path,
                                     const char *location_key,
                                     uint32_t revision,
                                     const char *label)
{
    if (store == NULL || path == NULL) {
        return false;
    }
    if (!file_has_exact_size(path, APP_REMOTE_MAP_BYTES)) {
        return false;
    }
    char key[MAP_LOCATION_KEY_MAX];
    location_key_for_storage(location_key, key, sizeof(key));
    if (store->active_index >= 0 &&
        store->buffers[store->active_index] != NULL &&
        store->active_revision == revision &&
        strcasecmp(store->active_location_key, key) == 0) {
        return true;
    }

    if (!ensure_map_buffers(store, label)) {
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "Could not open %s: errno=%d", path, errno);
        return false;
    }

    uint8_t *dst = store->buffers[0];
    size_t read = 0;
    while (read < APP_REMOTE_MAP_BYTES) {
        const size_t remaining = APP_REMOTE_MAP_BYTES - read;
        const size_t wanted = remaining < 4096 ? remaining : 4096;
        const size_t chunk = fread(dst + read, 1, wanted, file);
        if (chunk == 0) {
            break;
        }
        read += chunk;
    }
    fclose(file);
    if (read != APP_REMOTE_MAP_BYTES) {
        ESP_LOGW(TAG, "Short read loading %s: %u/%u", path, (unsigned)read, (unsigned)APP_REMOTE_MAP_BYTES);
        return false;
    }

    store->active_index = 0;
    store->active_revision = revision;
    copy_string(store->active_location_key, sizeof(store->active_location_key), key);
    ESP_LOGI(TAG, "%s map loaded from flash: %s", label, path);
    return true;
}

static void refresh_map_storage_index(bool force_revision_bump)
{
    char selected_key[MAP_LOCATION_KEY_MAX];
    selected_location_key(selected_key, sizeof(selected_key));

    const uint32_t route_mask = route_map_valid_mask();
    const uint32_t flag_mask = route_flag_valid_mask();
    const bool route_valid = route_mask != 0;
    const uint32_t radar_mask = radar_map_mask_for_location_key(selected_key);

    app_remote_status_t next = read_status();
    const bool changed = next.map_valid != route_valid ||
                         next.route_map_valid_mask != route_mask ||
                         next.route_flag_valid_mask != flag_mask ||
                         next.radar_map_valid_mask != radar_mask ||
                         next.radar_map_valid != (radar_mask != 0);

    next.map_valid = route_valid;
    next.map_bytes = route_valid ? APP_REMOTE_MAP_BYTES : 0;
    next.route_map_valid_mask = route_mask;
    next.route_flag_valid_mask = flag_mask;
    next.radar_map_valid = radar_mask != 0;
    next.radar_map_bytes = radar_mask != 0 ? APP_REMOTE_MAP_BYTES : 0;
    next.radar_map_valid_mask = radar_mask;

    if (changed || force_revision_bump) {
        next.revision++;
        next.map_revision++;
        next.radar_map_revision++;
        write_status(&next);
    } else {
        write_status(&next);
    }
}

static app_remote_style_t parse_style(const char *style)
{
    if (style == NULL) {
        return APP_REMOTE_STYLE_XDR_RADAR;
    }
    if (strcmp(style, "cartographicGlass") == 0 || strcmp(style, "cartographic") == 0 || strcmp(style, "map") == 0) {
        return APP_REMOTE_STYLE_CARTOGRAPHIC_GLASS;
    }
    if (strcmp(style, "gmtChronograph") == 0 || strcmp(style, "gmt") == 0 || strcmp(style, "clock") == 0) {
        return APP_REMOTE_STYLE_GMT_CHRONOGRAPH;
    }
    if (strcmp(style, "weatherRadar") == 0) {
        return APP_REMOTE_STYLE_WEATHER_RADAR;
    }
    if (strcmp(style, "weather") == 0) {
        return APP_REMOTE_STYLE_WEATHER;
    }
    if (strcmp(style, "spaceIss") == 0) {
        return APP_REMOTE_STYLE_SPACE_ISS;
    }
    if (strcmp(style, "spaceMoon") == 0) {
        return APP_REMOTE_STYLE_SPACE_MOON;
    }
    if (strcmp(style, "space") == 0) {
        return APP_REMOTE_STYLE_SPACE;
    }
    if (strcmp(style, "launchMap") == 0) {
        return APP_REMOTE_STYLE_LAUNCH_MAP;
    }
    if (strcmp(style, "launchQR") == 0) {
        return APP_REMOTE_STYLE_LAUNCH_QR;
    }
    if (strcmp(style, "launch") == 0) {
        return APP_REMOTE_STYLE_LAUNCH;
    }
    return APP_REMOTE_STYLE_XDR_RADAR;
}

int app_remote_radar_range_index_for_miles(float miles)
{
    int best = 0;
    float best_delta = fabsf(miles - radar_map_ranges[0]);
    for (int i = 1; i < APP_REMOTE_RADAR_MAP_RANGE_COUNT; i++) {
        const float delta = fabsf(miles - radar_map_ranges[i]);
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

float app_remote_radar_range_miles_at(int index)
{
    if (index < 0) {
        index = 0;
    }
    if (index >= APP_REMOTE_RADAR_MAP_RANGE_COUNT) {
        index = APP_REMOTE_RADAR_MAP_RANGE_COUNT - 1;
    }
    return radar_map_ranges[index];
}

static int radar_range_index_from_request(httpd_req_t *req)
{
    char query[64];
    char value[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "range", value, sizeof(value)) != ESP_OK) {
        return app_remote_radar_range_index_for_miles(20.0f);
    }

    const int miles = atoi(value);
    for (int i = 0; i < APP_REMOTE_RADAR_MAP_RANGE_COUNT; i++) {
        if ((int)radar_map_ranges[i] == miles) {
            return i;
        }
    }
    return -1;
}

static int route_view_index_from_request(httpd_req_t *req)
{
    char query[64];
    char value[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "view", value, sizeof(value)) != ESP_OK) {
        return 0;
    }
    if (strcasecmp(value, "globe") == 0 ||
        strcasecmp(value, "geodesic") == 0 ||
        strcmp(value, "0") == 0) {
        return 0;
    }
    if (strcasecmp(value, "frame") == 0 ||
        strcasecmp(value, "vertical") == 0 ||
        strcmp(value, "1") == 0) {
        return 1;
    }
    return -1;
}

static int route_endpoint_index_from_request(httpd_req_t *req)
{
    char query[64];
    char value[20];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "endpoint", value, sizeof(value)) != ESP_OK) {
        return -1;
    }
    if (strcasecmp(value, "origin") == 0 || strcmp(value, "0") == 0) {
        return 0;
    }
    if (strcasecmp(value, "destination") == 0 || strcmp(value, "1") == 0) {
        return 1;
    }
    return -1;
}

static int route_slot_from_request(httpd_req_t *req)
{
    char query[64];
    char value[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "slot", value, sizeof(value)) != ESP_OK) {
        return active_tracked_flight_slot();
    }
    const int slot = atoi(value);
    return slot >= 0 && slot < TRACKED_FLIGHT_SLOT_COUNT ? slot : -1;
}

static void radar_location_key_from_request(httpd_req_t *req, char *key, size_t key_size)
{
    if (key_size == 0) {
        return;
    }
    key[0] = '\0';

    char query[96];
    char value[24];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "location", value, sizeof(value)) == ESP_OK) {
        location_key_for_storage(value, key, key_size);
        return;
    }

    selected_location_key(key, key_size);
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, json);
}

static char *read_request_body(httpd_req_t *req, size_t max_len)
{
    if (req->content_len <= 0 || req->content_len > (int)max_len) {
        return NULL;
    }

    char *body = (char *)heap_caps_calloc(1, req->content_len + 1, MALLOC_CAP_8BIT);
    if (body == NULL) {
        return NULL;
    }

    int received = 0;
    while (received < req->content_len) {
        const int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            heap_caps_free(body);
            return NULL;
        }
        received += ret;
    }
    body[received] = '\0';
    return body;
}

static void add_wifi_profile_json(cJSON *parent, const char *name, const app_wifi_profile_t *profile)
{
    cJSON *object = cJSON_AddObjectToObject(parent, name);
    if (object == NULL || profile == NULL) {
        return;
    }
    cJSON_AddStringToObject(object, "ssid", profile->ssid);
    cJSON_AddBoolToObject(object, "hasPassword", strlen(profile->password) > 0);
}

static void add_location_key_array(
    cJSON *parent,
    const char *name,
    const char *csv)
{
    cJSON *cities = cJSON_AddArrayToObject(parent, name);
    char keys[80];
    copy_string(keys, sizeof(keys), csv);
    char *save_pointer = NULL;
    for (char *key = strtok_r(keys, ",", &save_pointer);
         key != NULL;
        key = strtok_r(NULL, ",", &save_pointer)) {
        if (app_location_preset_find(key) != NULL) {
            cJSON_AddItemToArray(cities, cJSON_CreateString(key));
        }
    }
}

static cJSON *read_persisted_companion_config(void)
{
    FILE *file = fopen(SHARED_CONFIG_PATH, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    const long length = ftell(file);
    if (length <= 0 || length > 32768 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *bytes = heap_caps_calloc(1, (size_t)length + 1, MALLOC_CAP_8BIT);
    if (bytes == NULL) {
        fclose(file);
        return NULL;
    }
    const size_t read_count = fread(bytes, 1, (size_t)length, file);
    fclose(file);
    cJSON *root = read_count == (size_t)length ? cJSON_Parse(bytes) : NULL;
    heap_caps_free(bytes);
    if (root == NULL) {
        return NULL;
    }
    const cJSON *companion = cJSON_GetObjectItemCaseSensitive(root, "companion");
    cJSON *copy = cJSON_IsObject(companion) ? cJSON_Duplicate(companion, true) : NULL;
    cJSON_Delete(root);
    return copy;
}

static const char *dock_item_name(uint8_t item)
{
    static const char *const names[APP_DOCK_ITEM_COUNT] = {
        "radar", "tracker", "weather", "clock", "solar",
        "eclipse", "moon", "iss", "launchNasa",
        "launchRoscosmos", "launchSpacex", "trackerFrame",
        "trackerDetail", "weatherForecast", "weatherRadar",
        "clockWorld", "launchNasaMap", "launchRoscosmosMap",
        "launchSpacexMap",
    };
    return item < APP_DOCK_ITEM_COUNT ? names[item] : NULL;
}

static bool parse_dock_item_name(
    const char *name,
    uint8_t *item)
{
    if (name == NULL || item == NULL) {
        return false;
    }
    for (uint8_t index = 0;
         index < APP_DOCK_ITEM_COUNT;
         index++) {
        if (strcmp(name, dock_item_name(index)) == 0) {
            *item = index;
            return true;
        }
    }
    return false;
}

static void add_dock_schedule_json(
    cJSON *companion,
    const app_config_t *config)
{
    cJSON *schedule = cJSON_AddArrayToObject(
        companion,
        "dockSchedule");
    for (uint8_t index = 0;
         index < config->dock_entry_count;
         index++) {
        const app_dock_entry_t *entry =
            &config->dock_entries[index];
        const char *name = dock_item_name(entry->item);
        if (name == NULL) {
            continue;
        }
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "screen", name);
        cJSON_AddNumberToObject(
            item,
            "durationSeconds",
            entry->duration_seconds);
        cJSON_AddItemToArray(schedule, item);
    }
}

static void apply_dock_schedule_json(
    app_config_t *config,
    const cJSON *schedule)
{
    if (!cJSON_IsArray(schedule)) {
        return;
    }
    app_dock_entry_t entries[APP_DOCK_ENTRY_MAX] = {0};
    uint32_t seen = 0;
    uint8_t count = 0;
    const cJSON *node = NULL;
    cJSON_ArrayForEach(node, schedule) {
        if (count >= APP_DOCK_ENTRY_MAX ||
            !cJSON_IsObject(node)) {
            continue;
        }
        const cJSON *screen = cJSON_GetObjectItemCaseSensitive(
            node,
            "screen");
        const cJSON *duration = cJSON_GetObjectItemCaseSensitive(
            node,
            "durationSeconds");
        uint8_t parsed = 0;
        if (!cJSON_IsString(screen) ||
            !cJSON_IsNumber(duration) ||
            !parse_dock_item_name(screen->valuestring, &parsed) ||
            duration->valueint < 5 ||
            duration->valueint > 300 ||
            (seen & (1u << parsed)) != 0) {
            continue;
        }
        entries[count++] = (app_dock_entry_t) {
            .item = parsed,
            .duration_seconds = (uint16_t)duration->valueint,
        };
        seen |= 1u << parsed;
    }
    if (count < APP_DOCK_ENTRY_MIN) {
        return;
    }
    memset(config->dock_entries, 0, sizeof(config->dock_entries));
    memcpy(
        config->dock_entries,
        entries,
        sizeof(entries));
    config->dock_entry_count = count;
}

static const char *main_screen_name(uint8_t screen)
{
    static const char *const names[APP_MAIN_SCREEN_COUNT] = {
        "radar", "tracker", "weather", "clock", "space", "launches",
    };
    return screen < APP_MAIN_SCREEN_COUNT ? names[screen] : NULL;
}

static bool parse_main_screen_name(const char *name, uint8_t *screen)
{
    if (name == NULL || screen == NULL) {
        return false;
    }
    for (uint8_t index = 0; index < APP_MAIN_SCREEN_COUNT; index++) {
        if (strcmp(name, main_screen_name(index)) == 0) {
            *screen = index;
            return true;
        }
    }
    return false;
}

static void add_main_screens_json(
    cJSON *companion,
    const app_config_t *config)
{
    cJSON *screens = cJSON_AddArrayToObject(companion, "mainScreens");
    for (uint8_t index = 0; index < config->main_screen_count; index++) {
        const char *name = main_screen_name(config->main_screens[index]);
        if (name != NULL) {
            cJSON_AddItemToArray(screens, cJSON_CreateString(name));
        }
    }
}

static void apply_main_screens_json(
    app_config_t *config,
    const cJSON *screens)
{
    if (!cJSON_IsArray(screens)) {
        return;
    }
    uint8_t order[APP_MAIN_SCREEN_COUNT] = {0};
    uint8_t count = 0;
    uint8_t seen = 0;
    const cJSON *node = NULL;
    cJSON_ArrayForEach(node, screens) {
        uint8_t parsed = 0;
        if (count >= APP_MAIN_SCREEN_COUNT ||
            !cJSON_IsString(node) ||
            !parse_main_screen_name(node->valuestring, &parsed) ||
            (seen & (1u << parsed)) != 0) {
            continue;
        }
        order[count++] = parsed;
        seen |= 1u << parsed;
    }
    if (count < 1) {
        return;
    }
    memset(config->main_screens, 0, sizeof(config->main_screens));
    memcpy(config->main_screens, order, sizeof(order));
    config->main_screen_count = count;
}

static cJSON *build_shared_config_json(
    const cJSON *companion_override,
    bool reconnecting,
    bool conflict)
{
    app_config_t cfg;
    app_config_get(&cfg);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "schemaVersion", SHARED_CONFIG_SCHEMA_VERSION);
    cJSON_AddNumberToObject(root, "savedAtUnix", (double)time(NULL));
    cJSON_AddBoolToObject(root, "reconnecting", reconnecting);
    cJSON_AddBoolToObject(root, "conflict", conflict);

    cJSON *ownership = cJSON_AddObjectToObject(root, "ownership");
    cJSON *device_owned = cJSON_AddArrayToObject(ownership, "device");
    const char *device_keys[] = {
        "trackedFlightRuntime",
        "launchRuntime",
        "activeWifiProfile",
        "location",
        "display.aircraftLabels",
        "display.radarWedge",
        "display.issFlags",
        "display.weatherUnits",
        "display.accentColor",
        "display.brightnessPercent",
        "radarRadiusMiles",
    };
    for (size_t index = 0; index < sizeof(device_keys) / sizeof(device_keys[0]); index++) {
        cJSON_AddItemToArray(device_owned, cJSON_CreateString(device_keys[index]));
    }
    cJSON *companion_owned = cJSON_AddArrayToObject(ownership, "companion");
    cJSON_AddItemToArray(companion_owned, cJSON_CreateString("wifiProfiles"));
    cJSON_AddItemToArray(companion_owned, cJSON_CreateString("credentials"));
    cJSON_AddItemToArray(companion_owned, cJSON_CreateString("display.accentColors"));
    cJSON_AddItemToArray(companion_owned, cJSON_CreateString("radarCities"));
    cJSON_AddItemToArray(companion_owned, cJSON_CreateString("radarLocations"));
    cJSON_AddItemToArray(companion_owned, cJSON_CreateString("clockCities"));
    cJSON_AddItemToArray(companion_owned, cJSON_CreateString("launchAgencies"));
    cJSON_AddItemToArray(companion_owned, cJSON_CreateString("dockSchedule"));
    cJSON_AddItemToArray(companion_owned, cJSON_CreateString("mainScreens"));
    cJSON_AddItemToArray(companion_owned, cJSON_CreateString("trackedFlights"));
    cJSON_AddItemToArray(companion_owned, cJSON_CreateString("activeTrackedFlightId"));

    cJSON *device = cJSON_AddObjectToObject(root, "device");
    cJSON_AddNumberToObject(device, "revision", cfg.revision);
    cJSON_AddStringToObject(device, "activeWifiProfile", app_config_wifi_profile_name(cfg.active_wifi_profile));
    cJSON *wifi_profiles = cJSON_AddObjectToObject(device, "wifiProfiles");
    add_wifi_profile_json(wifi_profiles, "home", &cfg.wifi_profiles[APP_WIFI_PROFILE_HOME]);
    add_wifi_profile_json(wifi_profiles, "hotspot", &cfg.wifi_profiles[APP_WIFI_PROFILE_HOTSPOT]);

    cJSON *location = cJSON_AddObjectToObject(device, "location");
    cJSON_AddStringToObject(location, "key", cfg.location_key);
    cJSON_AddStringToObject(location, "label", cfg.location_label);
    cJSON_AddStringToObject(location, "lat", cfg.location_lat);
    cJSON_AddStringToObject(location, "lon", cfg.location_lon);
    cJSON_AddStringToObject(location, "timezone", cfg.timezone);
    cJSON_AddStringToObject(location, "metar", cfg.metar_station);
    cJSON_AddStringToObject(location, "radarHalfDegrees", cfg.radar_half_degrees);

    cJSON *display = cJSON_AddObjectToObject(device, "display");
    cJSON_AddBoolToObject(display, "aircraftLabels", cfg.aircraft_labels_enabled);
    cJSON_AddBoolToObject(display, "radarWedge", cfg.radar_wedge_enabled);
    cJSON_AddBoolToObject(display, "issFlags", cfg.iss_flags_enabled);
    cJSON_AddStringToObject(
        display,
        "weatherUnits",
        cfg.weather_units == APP_WEATHER_UNITS_FAHRENHEIT ? "F" : "C");
    cJSON_AddStringToObject(display, "accentColor", app_config_accent_color_name(cfg.accent_color));
    cJSON *accent_colors = cJSON_AddObjectToObject(display, "accentColors");
    char accent_hex[7];
    snprintf(accent_hex, sizeof(accent_hex), "%06lX", (unsigned long)(cfg.accent_orange_rgb & 0xFFFFFF));
    cJSON_AddStringToObject(accent_colors, "orange", accent_hex);
    snprintf(accent_hex, sizeof(accent_hex), "%06lX", (unsigned long)(cfg.accent_cyan_rgb & 0xFFFFFF));
    cJSON_AddStringToObject(accent_colors, "cyan", accent_hex);
    snprintf(accent_hex, sizeof(accent_hex), "%06lX", (unsigned long)(cfg.accent_red_rgb & 0xFFFFFF));
    cJSON_AddStringToObject(accent_colors, "red", accent_hex);
    cJSON_AddNumberToObject(display, "brightnessPercent", cfg.display_brightness_percent);
    cJSON_AddNumberToObject(device, "radarRadiusMiles", cfg.radar_range_miles);

    app_data_status_t data_status;
    app_data_get_status(&data_status);
    cJSON *services = cJSON_AddObjectToObject(device, "services");
    cJSON_AddBoolToObject(
        services,
        "openSkyReady",
        data_status.opensky_api_health == APP_API_HEALTH_VALID);
    cJSON_AddBoolToObject(
        services,
        "airLabsReady",
        data_status.airlabs_api_health == APP_API_HEALTH_VALID);
    cJSON_AddBoolToObject(
        services,
        "flightAwareReady",
        data_status.flightaware_api_health == APP_API_HEALTH_VALID);

    cJSON *companion = cJSON_IsObject(companion_override)
        ? cJSON_Duplicate(companion_override, true)
        : read_persisted_companion_config();
    if (companion == NULL) {
        companion = cJSON_CreateObject();
    }
    if (cfg.launch_agency_mask) {
        cJSON_DeleteItemFromObjectCaseSensitive(companion, "launchAgencies");
        cJSON *agencies = cJSON_AddArrayToObject(companion, "launchAgencies");
        const char *names[] = {"nasa", "roscosmos", "spacex"};
        for (unsigned a = 0; a < 3; a++) if (cfg.launch_agency_mask & (1U << a))
            cJSON_AddItemToArray(agencies, cJSON_CreateString(names[a]));
    }
    if (!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(companion, "clockCities"))) {
        add_location_key_array(companion, "clockCities", cfg.clock_city_keys);
    }
    if (!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(companion, "radarCities"))) {
        add_location_key_array(companion, "radarCities", cfg.radar_city_keys);
    }
    if (!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(companion, "dockSchedule"))) {
        add_dock_schedule_json(companion, &cfg);
    }
    if (!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(companion, "mainScreens"))) {
        add_main_screens_json(companion, &cfg);
    }
    if (!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(companion, "trackedFlights"))) {
        cJSON_AddArrayToObject(companion, "trackedFlights");
    }
    if (cJSON_GetObjectItemCaseSensitive(companion, "activeTrackedFlightId") == NULL) {
        cJSON_AddNullToObject(companion, "activeTrackedFlightId");
    }
    cJSON_DeleteItemFromObjectCaseSensitive(companion, "lastSyncedDeviceRevision");
    cJSON_AddNumberToObject(companion, "lastSyncedDeviceRevision", cfg.revision);
    cJSON_AddItemToObject(root, "companion", companion);
    return root;
}

static bool write_shared_config_file(const cJSON *root)
{
    if (root == NULL || map_storage_wl_handle == WL_INVALID_HANDLE) {
        return false;
    }
    char *body = cJSON_Print(root);
    if (body == NULL) {
        return false;
    }
    FILE *file = fopen(SHARED_CONFIG_TEMP_PATH, "wb");
    if (file == NULL) {
        cJSON_free(body);
        return false;
    }
    const size_t length = strlen(body);
    bool ok = fwrite(body, 1, length, file) == length;
    if (fclose(file) != 0) {
        ok = false;
    }
    cJSON_free(body);
    if (!ok) {
        unlink(SHARED_CONFIG_TEMP_PATH);
        return false;
    }
    unlink(SHARED_CONFIG_PATH);
    if (rename(SHARED_CONFIG_TEMP_PATH, SHARED_CONFIG_PATH) != 0) {
        unlink(SHARED_CONFIG_TEMP_PATH);
        return false;
    }
    return true;
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    cJSON *root = build_shared_config_json(NULL, false, false);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not build config");
        return ESP_FAIL;
    }
    (void)write_shared_config_file(root);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not encode config");
        return ESP_FAIL;
    }

    esp_err_t err = send_json(req, body);
    cJSON_free(body);
    return err;
}

static void copy_json_string(char *dst, size_t dst_size, const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_IsObject(object) ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    if (cJSON_IsString(item)) {
        copy_string(dst, dst_size, item->valuestring);
    } else if (cJSON_IsNumber(item)) {
        snprintf(dst, dst_size, "%.6g", item->valuedouble);
    }
}

static bool parse_rgb_hex_value(
    const cJSON *object,
    const char *name,
    uint32_t *value)
{
    if (!cJSON_IsObject(object) || name == NULL || value == NULL) {
        return false;
    }
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    const char *text = item->valuestring;
    if (*text == '#') {
        text++;
    }
    if (strlen(text) != 6) {
        return false;
    }
    char *end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, 16);
    if (errno != 0 || end == text || *end != '\0' || parsed > 0xFFFFFFUL) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static int launch_agency_index_for_text(const char *agency)
{
    if (agency == NULL) {
        return -1;
    }
    if (strcasecmp(agency, "nasa") == 0) {
        return APP_REMOTE_LAUNCH_NASA;
    }
    if (strcasecmp(agency, "roscosmos") == 0) {
        return APP_REMOTE_LAUNCH_ROSCOSMOS;
    }
    if (strcasecmp(agency, "spacex") == 0) {
        return APP_REMOTE_LAUNCH_SPACEX;
    }
    return -1;
}

static void parse_launch_json(
    const cJSON *launch,
    app_remote_launch_t *result)
{
    if (!cJSON_IsObject(launch) || result == NULL) {
        return;
    }
    memset(result, 0, sizeof(*result));
    const cJSON *available =
        cJSON_GetObjectItemCaseSensitive(
            launch,
            "available");
    result->valid =
        !cJSON_IsBool(available) ||
        cJSON_IsTrue(available);
    copy_json_string(result->id, sizeof(result->id), launch, "id");
    copy_json_string(result->mission, sizeof(result->mission), launch, "missionName");
    copy_json_string(result->full_name, sizeof(result->full_name), launch, "fullName");
    copy_json_string(result->agency, sizeof(result->agency), launch, "agency");
    copy_json_string(result->vehicle, sizeof(result->vehicle), launch, "vehicle");
    copy_json_string(result->site, sizeof(result->site), launch, "site");
    copy_json_string(result->pad, sizeof(result->pad), launch, "padName");
    copy_json_string(result->location, sizeof(result->location), launch, "locationName");
    copy_json_string(result->precision, sizeof(result->precision), launch, "precisionName");
    copy_json_string(result->precision_abbrev, sizeof(result->precision_abbrev), launch, "precisionAbbreviation");
    copy_json_string(result->status, sizeof(result->status), launch, "statusName");
    copy_json_string(result->status_abbrev, sizeof(result->status_abbrev), launch, "statusAbbreviation");
    copy_json_string(result->watch_url, sizeof(result->watch_url), launch, "watchURL");

    const cJSON *net = cJSON_GetObjectItemCaseSensitive(launch, "netUnix");
    result->net_unix = cJSON_IsNumber(net) ? (int64_t)net->valuedouble : 0;
    const cJSON *updated = cJSON_GetObjectItemCaseSensitive(launch, "updatedUnix");
    result->updated_unix = cJSON_IsNumber(updated) ? (int64_t)updated->valuedouble : 0;
    const cJSON *utc_offset = cJSON_GetObjectItemCaseSensitive(launch, "currentUTCOffsetSeconds");
    result->current_utc_offset_s = cJSON_IsNumber(utc_offset) ? utc_offset->valueint : 0;
    const cJSON *weather = cJSON_GetObjectItemCaseSensitive(launch, "weatherProbabilityPercent");
    result->weather_probability_percent = cJSON_IsNumber(weather) ? weather->valueint : -1;
}

static void copy_legacy_launch_to_option(
    const app_remote_status_t *source,
    app_remote_launch_t *destination)
{
    if (source == NULL || destination == NULL) {
        return;
    }
    memset(destination, 0, sizeof(*destination));
    destination->valid = source->launch_valid;
    copy_string(destination->id, sizeof(destination->id), source->launch_id);
    copy_string(destination->mission, sizeof(destination->mission), source->launch_mission);
    copy_string(destination->full_name, sizeof(destination->full_name), source->launch_full_name);
    copy_string(destination->agency, sizeof(destination->agency), source->launch_agency);
    copy_string(destination->vehicle, sizeof(destination->vehicle), source->launch_vehicle);
    copy_string(destination->site, sizeof(destination->site), source->launch_site);
    copy_string(destination->pad, sizeof(destination->pad), source->launch_pad);
    copy_string(destination->location, sizeof(destination->location), source->launch_location);
    copy_string(destination->precision, sizeof(destination->precision), source->launch_precision);
    copy_string(destination->precision_abbrev, sizeof(destination->precision_abbrev), source->launch_precision_abbrev);
    copy_string(destination->status, sizeof(destination->status), source->launch_status);
    copy_string(destination->status_abbrev, sizeof(destination->status_abbrev), source->launch_status_abbrev);
    copy_string(destination->watch_url, sizeof(destination->watch_url), source->launch_watch_url);
    destination->net_unix = source->launch_net_unix;
    destination->updated_unix = source->launch_updated_unix;
    destination->current_utc_offset_s = source->launch_current_utc_offset_s;
    destination->weather_probability_percent = source->launch_weather_probability_percent;
}

static void copy_launch_option_to_legacy(
    const app_remote_launch_t *source,
    app_remote_status_t *destination)
{
    if (source == NULL || destination == NULL) {
        return;
    }
    destination->launch_valid = source->valid;
    copy_string(destination->launch_id, sizeof(destination->launch_id), source->id);
    copy_string(destination->launch_mission, sizeof(destination->launch_mission), source->mission);
    copy_string(destination->launch_full_name, sizeof(destination->launch_full_name), source->full_name);
    copy_string(destination->launch_agency, sizeof(destination->launch_agency), source->agency);
    copy_string(destination->launch_vehicle, sizeof(destination->launch_vehicle), source->vehicle);
    copy_string(destination->launch_site, sizeof(destination->launch_site), source->site);
    copy_string(destination->launch_pad, sizeof(destination->launch_pad), source->pad);
    copy_string(destination->launch_location, sizeof(destination->launch_location), source->location);
    copy_string(destination->launch_precision, sizeof(destination->launch_precision), source->precision);
    copy_string(destination->launch_precision_abbrev, sizeof(destination->launch_precision_abbrev), source->precision_abbrev);
    copy_string(destination->launch_status, sizeof(destination->launch_status), source->status);
    copy_string(destination->launch_status_abbrev, sizeof(destination->launch_status_abbrev), source->status_abbrev);
    copy_string(destination->launch_watch_url, sizeof(destination->launch_watch_url), source->watch_url);
    destination->launch_net_unix = source->net_unix;
    destination->launch_updated_unix = source->updated_unix;
    destination->launch_current_utc_offset_s = source->current_utc_offset_s;
    destination->launch_weather_probability_percent = source->weather_probability_percent;
}

static void apply_wifi_profile_json(app_wifi_profile_t *profile, const cJSON *profiles, const char *name)
{
    const cJSON *object = cJSON_IsObject(profiles) ? cJSON_GetObjectItemCaseSensitive(profiles, name) : NULL;
    if (!cJSON_IsObject(object) || profile == NULL) {
        return;
    }
    copy_json_string(profile->ssid, sizeof(profile->ssid), object, "ssid");
    copy_json_string(profile->password, sizeof(profile->password), object, "password");
}

static void apply_location_keys_json(
    char *destination,
    size_t destination_size,
    const cJSON *cities,
    int maximum_count)
{
    if (destination == NULL ||
        destination_size == 0 ||
        !cJSON_IsArray(cities)) {
        return;
    }

    char csv[80] = {0};
    int stored = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, cities) {
        if (stored >= maximum_count ||
            !cJSON_IsString(item) ||
            app_location_preset_find(item->valuestring) == NULL) {
            continue;
        }

        char normalized[8] = {0};
        snprintf(
            normalized,
            sizeof(normalized),
            "%s",
            item->valuestring);
        for (char *character = normalized;
             *character != '\0';
             character++) {
            *character = (char)toupper((unsigned char)*character);
        }

        bool duplicate = false;
        char existing[sizeof(csv)];
        copy_string(existing, sizeof(existing), csv);
        char *save_pointer = NULL;
        for (char *key = strtok_r(existing, ",", &save_pointer);
             key != NULL;
             key = strtok_r(NULL, ",", &save_pointer)) {
            if (strcasecmp(key, normalized) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        const size_t used = strlen(csv);
        snprintf(
            csv + used,
            sizeof(csv) - used,
            "%s%s",
            used > 0 ? "," : "",
            normalized);
        stored++;
    }

    if (stored > 0) {
        copy_string(destination, destination_size, csv);
    }
}

static bool location_field_is_safe(const char *value, size_t maximum_length)
{
    if (value == NULL || strlen(value) > maximum_length) {
        return false;
    }
    return strchr(value, '|') == NULL &&
           strchr(value, '\n') == NULL &&
           strchr(value, '\r') == NULL;
}

static bool location_key_is_safe(const char *key)
{
    const size_t length = key != NULL ? strlen(key) : 0;
    if (length < 2 || length > 7) {
        return false;
    }
    for (size_t index = 0; index < length; index++) {
        if (!isalnum((unsigned char)key[index])) {
            return false;
        }
    }
    return true;
}

static void apply_location_definitions_json(
    app_config_t *config,
    const cJSON *locations)
{
    if (config == NULL || !cJSON_IsArray(locations)) {
        return;
    }

    config->custom_location_data[0] = '\0';
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, locations) {
        if (!cJSON_IsObject(item)) {
            continue;
        }
        const cJSON *key_json = cJSON_GetObjectItemCaseSensitive(item, "key");
        const cJSON *label_json = cJSON_GetObjectItemCaseSensitive(item, "label");
        const cJSON *lat_json = cJSON_GetObjectItemCaseSensitive(item, "lat");
        const cJSON *lon_json = cJSON_GetObjectItemCaseSensitive(item, "lon");
        const cJSON *timezone_json = cJSON_GetObjectItemCaseSensitive(item, "timezone");
        const cJSON *metar_json = cJSON_GetObjectItemCaseSensitive(item, "metar");
        const cJSON *half_json = cJSON_GetObjectItemCaseSensitive(item, "radarHalfDegrees");
        if (!cJSON_IsString(key_json) ||
            !cJSON_IsString(label_json) ||
            !cJSON_IsString(lat_json) ||
            !cJSON_IsString(lon_json) ||
            !cJSON_IsString(timezone_json) ||
            !cJSON_IsString(metar_json) ||
            !cJSON_IsString(half_json) ||
            !location_key_is_safe(key_json->valuestring) ||
            app_location_preset_is_builtin(key_json->valuestring) ||
            !location_field_is_safe(label_json->valuestring, 47) ||
            !location_field_is_safe(lat_json->valuestring, 19) ||
            !location_field_is_safe(lon_json->valuestring, 19) ||
            !location_field_is_safe(timezone_json->valuestring, 63) ||
            !location_field_is_safe(metar_json->valuestring, 7) ||
            !location_field_is_safe(half_json->valuestring, 19)) {
            continue;
        }

        char *end = NULL;
        const double latitude = strtod(lat_json->valuestring, &end);
        if (end == lat_json->valuestring || *end != '\0' ||
            latitude < -90.0 || latitude > 90.0) {
            continue;
        }
        const double longitude = strtod(lon_json->valuestring, &end);
        if (end == lon_json->valuestring || *end != '\0' ||
            longitude < -180.0 || longitude > 180.0) {
            continue;
        }

        char normalized_key[8];
        snprintf(normalized_key, sizeof(normalized_key), "%s", key_json->valuestring);
        for (char *character = normalized_key; *character != '\0'; character++) {
            *character = (char)toupper((unsigned char)*character);
        }
        const size_t used = strlen(config->custom_location_data);
        const int written = snprintf(
            config->custom_location_data + used,
            sizeof(config->custom_location_data) - used,
            "%s|%s|%s|%s|%s|%s|%s\n",
            normalized_key,
            label_json->valuestring,
            lat_json->valuestring,
            lon_json->valuestring,
            timezone_json->valuestring[0] != '\0'
                ? timezone_json->valuestring
                : "UTC0",
            metar_json->valuestring[0] != '\0'
                ? metar_json->valuestring
                : "-",
            half_json->valuestring[0] != '\0'
                ? half_json->valuestring
                : "1.55");
        if (written < 0 ||
            (size_t)written >= sizeof(config->custom_location_data) - used) {
            config->custom_location_data[used] = '\0';
            break;
        }
    }
}

static bool wifi_config_changed(const app_config_t *before, const app_config_t *after)
{
    if (before->active_wifi_profile != after->active_wifi_profile) {
        return true;
    }

    const int active = after->active_wifi_profile;
    if (active < 0 || active >= APP_WIFI_PROFILE_COUNT) {
        return true;
    }

    return strcmp(before->wifi_profiles[active].ssid, after->wifi_profiles[active].ssid) != 0 ||
           strcmp(before->wifi_profiles[active].password, after->wifi_profiles[active].password) != 0;
}

static void delayed_wifi_reconnect_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1200));
    app_network_apply_config(true);
    vTaskDelete(NULL);
}

static esp_err_t apply_shared_config_root(
    cJSON *root,
    cJSON **response_out,
    bool *reconnect_wifi_out,
    bool *location_changed_out,
    char *error,
    size_t error_size)
{
    if (!cJSON_IsObject(root) || response_out == NULL ||
        reconnect_wifi_out == NULL || location_changed_out == NULL) {
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "invalid config JSON");
        }
        return ESP_ERR_INVALID_ARG;
    }

    *response_out = NULL;
    *reconnect_wifi_out = false;
    *location_changed_out = false;

    app_config_t before;
    app_config_get(&before);
    app_config_t next = before;

    const cJSON *device = cJSON_GetObjectItemCaseSensitive(root, "device");
    const cJSON *settings = cJSON_IsObject(device) ? device : root;
    const cJSON *base_revision = cJSON_GetObjectItemCaseSensitive(root, "baseDeviceRevision");
    const bool conflict = cJSON_IsNumber(base_revision) &&
        (uint32_t)base_revision->valuedouble != before.revision;

    const cJSON *active = cJSON_GetObjectItemCaseSensitive(settings, "activeWifiProfile");
    if (!conflict && cJSON_IsString(active)) {
        app_wifi_profile_id_t parsed;
        if (app_config_parse_wifi_profile_name(active->valuestring, &parsed)) {
            next.active_wifi_profile = parsed;
        }
    }

    const cJSON *wifi_profiles = cJSON_GetObjectItemCaseSensitive(settings, "wifiProfiles");
    /* Wi-Fi profile contents are companion-owned. A device revision conflict
     * only protects controls that can also be changed on the instrument. */
    apply_wifi_profile_json(&next.wifi_profiles[APP_WIFI_PROFILE_HOME], wifi_profiles, "home");
    apply_wifi_profile_json(&next.wifi_profiles[APP_WIFI_PROFILE_HOTSPOT], wifi_profiles, "hotspot");

    const cJSON *location = cJSON_GetObjectItemCaseSensitive(settings, "location");
    const cJSON *location_key = cJSON_IsObject(location) ? cJSON_GetObjectItemCaseSensitive(location, "key") : NULL;
    if (!conflict && cJSON_IsString(location_key)) {
        if (!app_location_apply_preset(&next, location_key->valuestring)) {
            copy_json_string(next.location_key, sizeof(next.location_key), location, "key");
        }
    }
    if (!conflict) {
        copy_json_string(next.location_label, sizeof(next.location_label), location, "label");
        copy_json_string(next.location_lat, sizeof(next.location_lat), location, "lat");
        copy_json_string(next.location_lon, sizeof(next.location_lon), location, "lon");
        copy_json_string(next.timezone, sizeof(next.timezone), location, "timezone");
        copy_json_string(next.metar_station, sizeof(next.metar_station), location, "metar");
        copy_json_string(next.radar_half_degrees, sizeof(next.radar_half_degrees), location, "radarHalfDegrees");
    }

    const cJSON *display = cJSON_GetObjectItemCaseSensitive(settings, "display");
    const cJSON *aircraft_labels = cJSON_IsObject(display) ? cJSON_GetObjectItemCaseSensitive(display, "aircraftLabels") : NULL;
    if (!conflict && cJSON_IsBool(aircraft_labels)) {
        next.aircraft_labels_enabled = cJSON_IsTrue(aircraft_labels);
    }
    const cJSON *radar_wedge = cJSON_IsObject(display) ? cJSON_GetObjectItemCaseSensitive(display, "radarWedge") : NULL;
    if (!conflict && cJSON_IsBool(radar_wedge)) {
        next.radar_wedge_enabled = cJSON_IsTrue(radar_wedge);
    }
    const cJSON *iss_flags = cJSON_IsObject(display) ? cJSON_GetObjectItemCaseSensitive(display, "issFlags") : NULL;
    if (!conflict && cJSON_IsBool(iss_flags)) {
        next.iss_flags_enabled = cJSON_IsTrue(iss_flags);
    }
    const cJSON *weather_units = cJSON_IsObject(display)
        ? cJSON_GetObjectItemCaseSensitive(display, "weatherUnits")
        : NULL;
    if (!conflict && cJSON_IsString(weather_units)) {
        if (strcasecmp(weather_units->valuestring, "F") == 0) {
            next.weather_units = APP_WEATHER_UNITS_FAHRENHEIT;
        } else if (strcasecmp(weather_units->valuestring, "C") == 0) {
            next.weather_units = APP_WEATHER_UNITS_CELSIUS;
        }
    }
    const cJSON *accent_color = cJSON_IsObject(display) ? cJSON_GetObjectItemCaseSensitive(display, "accentColor") : NULL;
    if (!conflict && cJSON_IsString(accent_color)) {
        app_accent_color_t parsed;
        if (app_config_parse_accent_color_name(accent_color->valuestring, &parsed)) {
            next.accent_color = parsed;
        }
    }
    const cJSON *accent_colors = cJSON_IsObject(display)
        ? cJSON_GetObjectItemCaseSensitive(display, "accentColors")
        : NULL;
    uint32_t custom_accent = 0;
    /* The custom palette is editable only in the companion, so it remains
     * authoritative even if a device-owned setting changed concurrently. */
    if (parse_rgb_hex_value(accent_colors, "orange", &custom_accent)) {
        next.accent_orange_rgb = custom_accent;
    }
    if (parse_rgb_hex_value(accent_colors, "cyan", &custom_accent)) {
        next.accent_cyan_rgb = custom_accent;
    }
    if (parse_rgb_hex_value(accent_colors, "red", &custom_accent)) {
        next.accent_red_rgb = custom_accent;
    }
    const cJSON *brightness = cJSON_IsObject(display)
        ? cJSON_GetObjectItemCaseSensitive(display, "brightnessPercent")
        : NULL;
    if (!conflict && cJSON_IsNumber(brightness)) {
        const int percent = brightness->valueint;
        if (percent >= 10 && percent <= 100) {
            next.display_brightness_percent = (uint8_t)percent;
        }
    }

    const cJSON *companion = cJSON_GetObjectItemCaseSensitive(root, "companion");
    const cJSON *radar_locations = cJSON_IsObject(companion)
        ? cJSON_GetObjectItemCaseSensitive(companion, "radarLocations")
        : NULL;
    const cJSON *launch_agencies = cJSON_GetObjectItemCaseSensitive(companion, "launchAgencies");
    if (launch_agencies && !app_launch_agencies(launch_agencies, &next.launch_agency_mask)) {
        snprintf(error, error_size, "invalid launch agencies");
        return ESP_ERR_INVALID_ARG;
    }
    if (cJSON_IsArray(radar_locations)) {
        apply_location_definitions_json(&next, radar_locations);
        /* The city lists below validate against the synchronized catalog. */
        app_location_set_custom_data(next.custom_location_data);
    }
    const cJSON *clock_cities = cJSON_IsObject(companion)
        ? cJSON_GetObjectItemCaseSensitive(companion, "clockCities")
        : cJSON_GetObjectItemCaseSensitive(root, "clockCities");
    apply_location_keys_json(
        next.clock_city_keys,
        sizeof(next.clock_city_keys),
        clock_cities,
        APP_CLOCK_CITY_COUNT);
    const cJSON *radar_cities = cJSON_IsObject(companion)
        ? cJSON_GetObjectItemCaseSensitive(companion, "radarCities")
        : cJSON_GetObjectItemCaseSensitive(root, "radarCities");
    apply_location_keys_json(
        next.radar_city_keys,
        sizeof(next.radar_city_keys),
        radar_cities,
        APP_RADAR_CITY_COUNT);
    const cJSON *dock_schedule = cJSON_IsObject(companion)
        ? cJSON_GetObjectItemCaseSensitive(companion, "dockSchedule")
        : cJSON_GetObjectItemCaseSensitive(root, "dockSchedule");
    apply_dock_schedule_json(&next, dock_schedule);
    const cJSON *main_screens = cJSON_IsObject(companion)
        ? cJSON_GetObjectItemCaseSensitive(companion, "mainScreens")
        : cJSON_GetObjectItemCaseSensitive(root, "mainScreens");
    apply_main_screens_json(&next, main_screens);

    const cJSON *radar_radius = cJSON_GetObjectItemCaseSensitive(settings, "radarRadiusMiles");
    if (!conflict && cJSON_IsNumber(radar_radius)) {
        const int miles = radar_radius->valueint;
        if (miles == 10 || miles == 20 || miles == 50) {
            next.radar_range_miles = (uint16_t)miles;
        }
    }

    const cJSON *credentials = cJSON_GetObjectItemCaseSensitive(root, "credentials");
    const cJSON *open_sky = cJSON_IsObject(credentials)
        ? cJSON_GetObjectItemCaseSensitive(credentials, "openSky")
        : cJSON_GetObjectItemCaseSensitive(root, "openSky");
    const cJSON *open_sky_enabled = cJSON_IsObject(open_sky) ? cJSON_GetObjectItemCaseSensitive(open_sky, "enabled") : NULL;
    if (cJSON_IsBool(open_sky_enabled)) {
        next.opensky_enabled = cJSON_IsTrue(open_sky_enabled);
    }
    copy_json_string(next.opensky_client_id, sizeof(next.opensky_client_id), open_sky, "clientId");
    copy_json_string(next.opensky_client_secret, sizeof(next.opensky_client_secret), open_sky, "clientSecret");

    const cJSON *air_labs = cJSON_IsObject(credentials)
        ? cJSON_GetObjectItemCaseSensitive(credentials, "airLabs")
        : cJSON_GetObjectItemCaseSensitive(root, "airLabs");
    copy_json_string(next.airlabs_api_key, sizeof(next.airlabs_api_key), air_labs, "apiKey");

    const cJSON *flight_aware = cJSON_IsObject(credentials)
        ? cJSON_GetObjectItemCaseSensitive(credentials, "flightAware")
        : cJSON_GetObjectItemCaseSensitive(root, "flightAware");
    const cJSON *flight_aware_available = cJSON_IsObject(flight_aware)
        ? cJSON_GetObjectItemCaseSensitive(flight_aware, "available")
        : NULL;
    if (cJSON_IsBool(flight_aware_available)) {
        next.flightaware_available = cJSON_IsTrue(flight_aware_available);
    }
    copy_json_string(next.flightaware_api_key, sizeof(next.flightaware_api_key), flight_aware, "apiKey");

    const bool reconnect_wifi = wifi_config_changed(&before, &next);
    const bool location_changed = strcmp(before.location_key, next.location_key) != 0 ||
                                  strcmp(before.location_lat, next.location_lat) != 0 ||
                                  strcmp(before.location_lon, next.location_lon) != 0;
    esp_err_t err = app_config_save(&next);
    if (err != ESP_OK) {
        app_location_set_custom_data(before.custom_location_data);
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "could not save config: %s", esp_err_to_name(err));
        }
        return err;
    }

    cJSON *response = build_shared_config_json(companion, reconnect_wifi, conflict);
    if (response == NULL) {
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "could not build saved config");
        }
        return ESP_ERR_NO_MEM;
    }
    (void)write_shared_config_file(response);
    *response_out = response;
    *reconnect_wifi_out = reconnect_wifi;
    *location_changed_out = location_changed;
    return ESP_OK;
}

static void finish_config_application(bool reconnect_wifi, bool location_changed)
{
    if (reconnect_wifi) {
        const BaseType_t task_ok = xTaskCreate(delayed_wifi_reconnect_task, "wifi_reconfig", 4096, NULL, 4, NULL);
        if (task_ok != pdPASS) {
            ESP_LOGW(TAG, "Could not defer Wi-Fi reconnect; reconnecting immediately");
            app_network_apply_config(true);
        }
    } else {
        app_network_apply_config(false);
        if (location_changed) {
            refresh_map_storage_index(true);
            release_radar_map_buffers_except(-1);
        }
        app_data_request_refresh();
    }
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    char *body = read_request_body(req, 16384);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "config JSON must be 1..16384 bytes");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }

    cJSON *response = NULL;
    bool reconnect_wifi = false;
    bool location_changed = false;
    char error[96] = {0};
    if (config_mutex != NULL) {
        xSemaphoreTake(config_mutex, portMAX_DELAY);
    }
    esp_err_t err = apply_shared_config_root(
        root,
        &response,
        &reconnect_wifi,
        &location_changed,
        error,
        sizeof(error));
    if (config_mutex != NULL) {
        xSemaphoreGive(config_mutex);
    }
    cJSON_Delete(root);
    if (err != ESP_OK || response == NULL) {
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            error[0] != '\0' ? error : "could not save config");
        return ESP_FAIL;
    }

    char *response_body = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (response_body == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not encode saved config");
        return ESP_FAIL;
    }
    err = send_json(req, response_body);
    cJSON_free(response_body);
    finish_config_application(reconnect_wifi, location_changed);
    return err;
}

static void usb_config_send(const char *prefix, const char *payload)
{
    if (prefix == NULL || payload == NULL || usb_config_fd < 0) {
        return;
    }
    const size_t prefix_length = strlen(prefix);
    const size_t payload_length = strlen(payload);
    const size_t response_length = prefix_length + payload_length + 1;
    char *response = (char *)heap_caps_malloc(
        response_length + 1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (response == NULL) {
        return;
    }
    memcpy(response, prefix, prefix_length);
    memcpy(response + prefix_length, payload, payload_length);
    response[prefix_length + payload_length] = '\n';
    response[response_length] = '\0';
    size_t written = 0;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
    /* Keep console logs from splitting a framed JSON response. Never pass a
     * whole configuration/launch snapshot to the 512-byte driver ring: its
     * all-or-nothing enqueue cannot accept a write larger than that ring. */
    flockfile(stdout);
    while (written < response_length &&
           (int32_t)(deadline - xTaskGetTickCount()) > 0) {
        const size_t block_size = app_remote_usb_tx_chunk(response_length - written);
        const ssize_t count = usb_config_driver_ready
            ? usb_serial_jtag_write_bytes(
                  response + written,
                  block_size,
                  pdMS_TO_TICKS(25))
            : write(
                  usb_config_fd,
                  response + written,
                  block_size);
        if (count > 0) {
            written += (size_t)count;
        } else {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    funlockfile(stdout);
    if (written != response_length) ESP_LOGW(TAG, "USB response incomplete: %u/%u bytes", (unsigned)written, (unsigned)response_length);
    /* /dev/secondary is the USB Serial/JTAG character device, not a file.
     * The write above queues the complete acknowledgement. fsync() on every
     * 12 KiB chunk serialized the transport and made multi-map packages take
     * minutes, without adding durability or flow-control guarantees. */
    heap_caps_free(response);
}

static void usb_file_abort(void)
{
    const bool cancel_package = usb_file_transfer.kind != USB_FILE_NONE;
    if (usb_file_transfer.file != NULL) {
        fclose(usb_file_transfer.file);
    }
    heap_caps_free(usb_file_transfer.io_buffer);
    if (usb_file_transfer.temporary_path[0] != '\0') {
        unlink(usb_file_transfer.temporary_path);
    }
    memset(&usb_file_transfer, 0, sizeof(usb_file_transfer));
    if (cancel_package && usb_package_active) {
        usb_package_active = false;
        app_data_set_paused(false);
    }
}

static bool usb_file_open_temporary(void)
{
    usb_file_transfer.file = fopen(usb_file_transfer.temporary_path, "wb");
    if (usb_file_transfer.file == NULL) {
        const int open_errno = errno;
        ESP_LOGE(TAG,
                 "Could not open USB transfer file %s: errno=%d (%s)",
                 usb_file_transfer.temporary_path,
                 open_errno,
                 strerror(open_errno));
        return false;
    }
    usb_file_transfer.io_buffer = (uint8_t *)heap_caps_malloc(
        USB_FILE_IO_BUFFER_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (usb_file_transfer.io_buffer != NULL &&
        setvbuf(usb_file_transfer.file,
                (char *)usb_file_transfer.io_buffer,
                _IOFBF,
                USB_FILE_IO_BUFFER_SIZE) != 0) {
        heap_caps_free(usb_file_transfer.io_buffer);
        usb_file_transfer.io_buffer = NULL;
    }
    return true;
}

/* The storage partition uses FAT with 8.3-compatible names. Appending another
 * extension (for example rg.rgb.usb) is rejected by fopen() with EINVAL on
 * this board. Replace the existing extension instead: rg.rgb -> rg.usb. */
static void usb_sibling_path(const char *final_path,
                             const char *extension,
                             char *path,
                             size_t path_size)
{
    if (final_path == NULL || extension == NULL || path == NULL ||
        path_size == 0) {
        return;
    }
    const char *slash = strrchr(final_path, '/');
    const char *dot = strrchr(final_path, '.');
    if (dot == NULL || (slash != NULL && dot < slash)) {
        snprintf(path, path_size, "%s%s", final_path, extension);
        return;
    }
    const size_t stem_length = (size_t)(dot - final_path);
    snprintf(path,
             path_size,
             "%.*s%s",
             (int)stem_length,
             final_path,
             extension);
}

static void usb_restore_interrupted_file(const char *final_path)
{
    if (final_path == NULL || final_path[0] == '\0') {
        return;
    }
    char temporary_path[101];
    char backup_path[101];
    usb_sibling_path(
        final_path, ".usb", temporary_path, sizeof(temporary_path));
    usb_sibling_path(
        final_path, ".bak", backup_path, sizeof(backup_path));

    if (access(final_path, F_OK) != 0 &&
        access(backup_path, F_OK) == 0) {
        if (rename(backup_path, final_path) == 0) {
            ESP_LOGW(TAG, "Restored interrupted USB asset: %s", final_path);
        }
    } else {
        unlink(backup_path);
    }
    unlink(temporary_path);
}

static void usb_restore_interrupted_files(void)
{
    char path[96];
    for (int slot = 0; slot < TRACKED_FLIGHT_SLOT_COUNT; slot++) {
        for (int view = 0; view < APP_REMOTE_ROUTE_MAP_VIEW_COUNT; view++) {
            route_map_path_for_slot_view(slot, view, path, sizeof(path));
            usb_restore_interrupted_file(path);
        }
        for (int endpoint = 0; endpoint < 2; endpoint++) {
            route_flag_path_for_slot_endpoint(slot, endpoint, path, sizeof(path));
            usb_restore_interrupted_file(path);
        }
    }
}

/* A tracked-flight sync is authoritative: the companion sends every map and
 * flag that should remain on the device in one package. Clear only the old
 * tracked-flight assets before that package starts. This avoids needing a
 * second full 466x466 map of temporary headroom while preserving radar,
 * weather, clock, space, launch, and configuration assets. */
static void prepare_tracked_flight_storage(uint32_t retained_slot_mask)
{
    usb_file_abort();

    bool manifest_changed = false;
    char path[96];
    char temporary_path[101];
    char backup_path[101];
    for (int slot = 0; slot < TRACKED_FLIGHT_SLOT_COUNT; slot++) {
        const bool retain_final_files =
            (retained_slot_mask & (1UL << slot)) != 0;
        if (!retain_final_files) {
            tracked_asset_manifest_clear_slot(slot);
            manifest_changed = true;
        }
        for (int view = 0; view < APP_REMOTE_ROUTE_MAP_VIEW_COUNT; view++) {
            route_map_path_for_slot_view(slot, view, path, sizeof(path));
            if (!retain_final_files) {
                unlink(path);
            }
            usb_sibling_path(path, ".usb", temporary_path, sizeof(temporary_path));
            usb_sibling_path(path, ".bak", backup_path, sizeof(backup_path));
            unlink(temporary_path);
            unlink(backup_path);
        }
        for (int endpoint = 0; endpoint < 2; endpoint++) {
            route_flag_path_for_slot_endpoint(slot, endpoint, path, sizeof(path));
            if (!retain_final_files) {
                unlink(path);
            }
            usb_sibling_path(path, ".usb", temporary_path, sizeof(temporary_path));
            usb_sibling_path(path, ".bak", backup_path, sizeof(backup_path));
            unlink(temporary_path);
            unlink(backup_path);
        }
    }
    if (manifest_changed && !tracked_asset_manifest_save()) {
        ESP_LOGW(TAG, "Could not update tracked-asset manifest");
    }
    unlink(MAP_STORAGE_BASE_PATH "/route.rgb");

    /* The files above are authoritative renderer inputs. Invalidate their
     * published masks immediately so an interrupted USB package cannot leave
     * the UI believing that deleted maps and flags are still available. */
    app_remote_status_t next = read_status();
    const uint32_t route_map_mask = route_map_valid_mask();
    const uint32_t route_flag_mask = route_flag_valid_mask();
    if (next.route_map_valid_mask != route_map_mask ||
        next.route_flag_valid_mask != route_flag_mask) {
        next.route_map_valid_mask = route_map_mask;
        next.route_flag_valid_mask = route_flag_mask;
        next.map_valid = next.route_map_valid_mask != 0;
        next.map_bytes = next.map_valid ? APP_REMOTE_MAP_BYTES : 0;
        next.map_revision++;
        next.route_flag_revision++;
        route_flag_loaded_revision[0] = UINT32_MAX;
        route_flag_loaded_revision[1] = UINT32_MAX;
        next.revision++;
        write_status(&next);
    }
    ESP_LOGI(TAG,
             "Tracked-flight storage prepared (retained slots=0x%lx)",
             (unsigned long)retained_slot_mask);
}

static bool usb_file_matches(const char *path,
                             usb_file_kind_t kind,
                             uint8_t slot,
                             size_t expected_size,
                             uint32_t expected_hash)
{
    if (!file_has_exact_size(path, expected_size)) {
        return false;
    }
    const int index = tracked_asset_kind_index(kind);
    return index >= 0 && slot < TRACKED_FLIGHT_SLOT_COUNT &&
        tracked_asset_manifest.hashes[slot][index] == expected_hash;
}

static bool usb_file_name_slot(const char *name,
                               const char *base,
                               uint8_t *slot)
{
    if (strcmp(name, base) == 0) {
        *slot = (uint8_t)active_tracked_flight_slot();
        return true;
    }
    const size_t base_length = strlen(base);
    if (strncmp(name, base, base_length) != 0 ||
        name[base_length] != '_' ||
        name[base_length + 1] < '0' ||
        name[base_length + 1] >= '0' + TRACKED_FLIGHT_SLOT_COUNT ||
        name[base_length + 2] != '\0') {
        return false;
    }
    *slot = (uint8_t)(name[base_length + 1] - '0');
    return true;
}

static bool usb_file_target(const char *name,
                            usb_file_kind_t *kind,
                            uint8_t *slot,
                            size_t *expected_size,
                            char *path,
                            size_t path_size)
{
    if (name == NULL || kind == NULL || slot == NULL || expected_size == NULL ||
        path == NULL || path_size == 0) {
        return false;
    }
    if (usb_file_name_slot(name, "route_globe", slot)) {
        *kind = USB_FILE_ROUTE_GLOBE;
        *expected_size = APP_REMOTE_MAP_BYTES;
        route_map_path_for_slot_view(*slot, 0, path, path_size);
        return true;
    }
    if (usb_file_name_slot(name, "route_frame", slot)) {
        *kind = USB_FILE_ROUTE_FRAME;
        *expected_size = APP_REMOTE_MAP_BYTES;
        route_map_path_for_slot_view(*slot, 1, path, path_size);
        return true;
    }
    if (usb_file_name_slot(name, "flag_origin", slot)) {
        *kind = USB_FILE_FLAG_ORIGIN;
        *expected_size = APP_REMOTE_ROUTE_FLAG_BYTES;
        route_flag_path_for_slot_endpoint(*slot, 0, path, path_size);
        return true;
    }
    if (usb_file_name_slot(name, "flag_destination", slot)) {
        *kind = USB_FILE_FLAG_DESTINATION;
        *expected_size = APP_REMOTE_ROUTE_FLAG_BYTES;
        route_flag_path_for_slot_endpoint(*slot, 1, path, path_size);
        return true;
    }
    return false;
}

static void usb_file_begin_transfer(const char *name,
                                    usb_file_kind_t kind,
                                    uint8_t slot,
                                    const char *final_path,
                                    size_t expected_size,
                                    uint32_t expected_hash)
{
    if (usb_file_matches(
            final_path, kind, slot, expected_size, expected_hash)) {
        usb_config_send(USB_FILE_CACHED_PREFIX, name);
        return;
    }

    usb_file_abort();
    usb_file_transfer.kind = kind;
    usb_file_transfer.slot = slot;
    snprintf(usb_file_transfer.name,
             sizeof(usb_file_transfer.name), "%.23s", name);
    snprintf(usb_file_transfer.final_path,
             sizeof(usb_file_transfer.final_path), "%s", final_path);
    usb_sibling_path(final_path,
                     ".usb",
                     usb_file_transfer.temporary_path,
                     sizeof(usb_file_transfer.temporary_path));
    usb_file_transfer.expected_size = expected_size;
    usb_file_transfer.expected_hash = expected_hash;
    usb_file_transfer.running_hash = 2166136261UL;
    usb_file_transfer.last_activity_tick = xTaskGetTickCount();
    unlink(usb_file_transfer.temporary_path);

    /* The map partition intentionally holds several full-resolution route
     * packs and normally has less than one additional map of headroom. Remove
     * only the target before installing its verified replacement. */
    if (access(usb_file_transfer.final_path, F_OK) == 0 &&
        unlink(usb_file_transfer.final_path) != 0) {
        usb_file_abort();
        usb_config_send(USB_FILE_ERROR_PREFIX,
                        "could not replace existing file");
        return;
    }
    if (!usb_file_open_temporary()) {
        usb_file_abort();
        usb_config_send(USB_FILE_ERROR_PREFIX,
                        "could not open temporary file");
        return;
    }
    usb_package_last_activity_tick = xTaskGetTickCount();
    usb_config_send(USB_FILE_READY_PREFIX, usb_file_transfer.name);
}

static void usb_file_begin(const char *payload)
{
    if (payload == NULL) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "invalid begin command");
        return;
    }
    char command[96];
    snprintf(command, sizeof(command), "%s", payload);
    char *size_text = strchr(command, ':');
    if (size_text == NULL) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "missing file size");
        return;
    }
    *size_text++ = '\0';
    char *hash_text = strchr(size_text, ':');
    if (hash_text == NULL) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "missing file checksum");
        return;
    }
    *hash_text++ = '\0';

    char *size_end = NULL;
    char *hash_end = NULL;
    const unsigned long requested_size = strtoul(size_text, &size_end, 10);
    const unsigned long requested_hash = strtoul(hash_text, &hash_end, 16);
    if (size_end == size_text || *size_end != '\0' ||
        hash_end == hash_text || *hash_end != '\0' ||
        requested_size == 0 || requested_size > SIZE_MAX) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "invalid file metadata");
        return;
    }

    usb_file_kind_t kind = USB_FILE_NONE;
    uint8_t slot = 0;
    size_t exact_size = 0;
    char final_path[96] = {0};
    if (!usb_file_target(command, &kind, &slot, &exact_size,
                         final_path, sizeof(final_path))) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "unsupported file name");
        return;
    }
    if ((size_t)requested_size != exact_size) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "unexpected file size");
        return;
    }

    usb_file_begin_transfer(command,
                            kind,
                            slot,
                            final_path,
                            exact_size,
                            (uint32_t)requested_hash);
}

static void usb_file_chunk(const char *payload)
{
    if (usb_file_transfer.kind == USB_FILE_NONE ||
        usb_file_transfer.file == NULL) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "no active file transfer");
        return;
    }
    if (payload == NULL) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "invalid chunk command");
        return;
    }
    const char *separator = strchr(payload, ':');
    if (separator == NULL) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "missing chunk payload");
        return;
    }
    char offset_text[24];
    const size_t offset_length = (size_t)(separator - payload);
    if (offset_length == 0 || offset_length >= sizeof(offset_text)) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "invalid chunk offset");
        return;
    }
    memcpy(offset_text, payload, offset_length);
    offset_text[offset_length] = '\0';
    char *offset_end = NULL;
    const unsigned long requested_offset =
        strtoul(offset_text, &offset_end, 10);
    if (offset_end == offset_text || *offset_end != '\0' ||
        requested_offset != usb_file_transfer.received_size) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "unexpected chunk offset");
        return;
    }

    const unsigned char *encoded =
        (const unsigned char *)(separator + 1);
    const size_t encoded_length = strlen((const char *)encoded);
    if (usb_file_decode_buffer == NULL) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "USB decode buffer unavailable");
        return;
    }
    size_t decoded_length = 0;
    const int decode_result = mbedtls_base64_decode(
        usb_file_decode_buffer,
        USB_FILE_DECODE_BUFFER_SIZE,
        &decoded_length,
        encoded,
        encoded_length);
    if (decode_result != 0 || decoded_length == 0 ||
        usb_file_transfer.received_size + decoded_length >
            usb_file_transfer.expected_size) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "invalid chunk data");
        return;
    }
    if (fwrite(usb_file_decode_buffer,
               1,
               decoded_length,
               usb_file_transfer.file) != decoded_length) {
        usb_file_abort();
        usb_config_send(USB_FILE_ERROR_PREFIX, "file write failed");
        return;
    }
    usb_file_transfer.running_hash = fnv1a_update(
        usb_file_transfer.running_hash,
        usb_file_decode_buffer,
        decoded_length);
    usb_file_transfer.received_size += decoded_length;
    usb_file_transfer.last_activity_tick = xTaskGetTickCount();
    usb_package_last_activity_tick = usb_file_transfer.last_activity_tick;
    char acknowledgement[24];
    snprintf(acknowledgement,
             sizeof(acknowledgement),
             "%u",
             (unsigned)usb_file_transfer.received_size);
    usb_config_send(USB_FILE_ACK_PREFIX, acknowledgement);
}

static void usb_file_binary_begin(const char *payload)
{
    if (usb_file_transfer.kind == USB_FILE_NONE ||
        usb_file_transfer.file == NULL) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "no active file transfer");
        return;
    }
    if (payload == NULL || usb_file_transfer.binary_remaining != 0) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "invalid binary chunk command");
        return;
    }
    const char *separator = strchr(payload, ':');
    if (separator == NULL) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "missing binary chunk length");
        return;
    }
    char offset_text[24];
    const size_t offset_length = (size_t)(separator - payload);
    if (offset_length == 0 || offset_length >= sizeof(offset_text)) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "invalid binary chunk offset");
        return;
    }
    memcpy(offset_text, payload, offset_length);
    offset_text[offset_length] = '\0';

    char *offset_end = NULL;
    char *length_end = NULL;
    const unsigned long requested_offset =
        strtoul(offset_text, &offset_end, 10);
    const unsigned long requested_length =
        strtoul(separator + 1, &length_end, 10);
    if (offset_end == offset_text || *offset_end != '\0' ||
        length_end == separator + 1 || *length_end != '\0' ||
        requested_offset != usb_file_transfer.received_size ||
        requested_length == 0 ||
        requested_length > USB_FILE_BINARY_CHUNK_MAX ||
        requested_length >
            usb_file_transfer.expected_size -
                usb_file_transfer.received_size) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "invalid binary chunk metadata");
        return;
    }
    usb_file_transfer.binary_remaining = (size_t)requested_length;
    usb_file_transfer.last_activity_tick = xTaskGetTickCount();
    usb_package_last_activity_tick = usb_file_transfer.last_activity_tick;
}

static bool usb_file_binary_write(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0 ||
        usb_file_transfer.kind == USB_FILE_NONE ||
        usb_file_transfer.file == NULL ||
        length > usb_file_transfer.binary_remaining) {
        return false;
    }
    if (fwrite(data, 1, length, usb_file_transfer.file) != length) {
        usb_file_abort();
        usb_config_send(USB_FILE_ERROR_PREFIX, "binary file write failed");
        return false;
    }
    usb_file_transfer.running_hash = fnv1a_update(
        usb_file_transfer.running_hash,
        data,
        length);
    usb_file_transfer.received_size += length;
    usb_file_transfer.binary_remaining -= length;
    usb_file_transfer.last_activity_tick = xTaskGetTickCount();
    if (usb_file_transfer.binary_remaining == 0) {
        char acknowledgement[24];
        snprintf(acknowledgement,
                 sizeof(acknowledgement),
                 "%u",
                 (unsigned)usb_file_transfer.received_size);
        usb_config_send(USB_FILE_ACK_PREFIX, acknowledgement);
    }
    return true;
}

static void usb_file_finish(void)
{
    if (usb_file_transfer.kind == USB_FILE_NONE ||
        usb_file_transfer.file == NULL) {
        usb_config_send(USB_FILE_ERROR_PREFIX, "no active file transfer");
        return;
    }
    if (usb_file_transfer.received_size !=
            usb_file_transfer.expected_size ||
        usb_file_transfer.running_hash !=
            usb_file_transfer.expected_hash) {
        usb_file_abort();
        usb_config_send(USB_FILE_ERROR_PREFIX, "file verification failed");
        return;
    }
    if (fclose(usb_file_transfer.file) != 0) {
        usb_file_transfer.file = NULL;
        heap_caps_free(usb_file_transfer.io_buffer);
        usb_file_transfer.io_buffer = NULL;
        usb_file_abort();
        usb_config_send(USB_FILE_ERROR_PREFIX, "could not close file");
        return;
    }
    usb_file_transfer.file = NULL;
    heap_caps_free(usb_file_transfer.io_buffer);
    usb_file_transfer.io_buffer = NULL;
    char backup_path[sizeof(usb_file_transfer.final_path) + 5];
    usb_sibling_path(usb_file_transfer.final_path,
                     ".bak",
                     backup_path,
                     sizeof(backup_path));
    unlink(backup_path);
    const bool had_previous_file =
        access(usb_file_transfer.final_path, F_OK) == 0;
    if (had_previous_file &&
        rename(usb_file_transfer.final_path, backup_path) != 0) {
        usb_file_abort();
        usb_config_send(
            USB_FILE_ERROR_PREFIX,
            "could not preserve previous file");
        return;
    }
    if (rename(usb_file_transfer.temporary_path,
               usb_file_transfer.final_path) != 0) {
        if (had_previous_file) {
            (void)rename(backup_path, usb_file_transfer.final_path);
        }
        usb_file_abort();
        usb_config_send(USB_FILE_ERROR_PREFIX, "could not install file");
        return;
    }
    if (had_previous_file) {
        unlink(backup_path);
    }
    usb_file_transfer.temporary_path[0] = '\0';
    if (!tracked_asset_manifest_record(
            usb_file_transfer.kind,
            usb_file_transfer.slot,
            usb_file_transfer.expected_hash)) {
        ESP_LOGW(TAG,
                 "Could not persist checksum for USB asset %s",
                 usb_file_transfer.name);
    }

    /* A package can contain eight full maps plus its flags. Do not publish
     * each installed file to the renderer independently: that made LVGL load
     * the first route map while the second was still crossing USB, competing
     * for display DMA memory and occasionally starving the transport. The
     * final ASTSTATE command below validates the complete package, indexes the
     * selected slot, and emits one renderer revision change atomically. A
     * standalone legacy file transfer still retains its immediate-publish
     * behavior. */
    if (!usb_package_active) {
        app_remote_status_t next = read_status();
        const bool asset_is_active =
            usb_file_transfer.slot == next.tracked_flight_slot;
        if (asset_is_active &&
            (usb_file_transfer.kind == USB_FILE_ROUTE_GLOBE ||
             usb_file_transfer.kind == USB_FILE_ROUTE_FRAME)) {
            next.route_map_valid_mask = route_map_valid_mask();
            next.map_valid = next.route_map_valid_mask != 0;
            next.map_bytes = APP_REMOTE_MAP_BYTES;
            next.map_revision++;
            if (next.route_map_valid_mask ==
                ((1UL << APP_REMOTE_ROUTE_MAP_VIEW_COUNT) - 1UL)) {
                unlink(MAP_STORAGE_BASE_PATH "/route.rgb");
            }
        } else if (asset_is_active) {
            const int endpoint =
                usb_file_transfer.kind == USB_FILE_FLAG_DESTINATION ? 1 : 0;
            route_flag_loaded_revision[endpoint] = UINT32_MAX;
            next.route_flag_valid_mask = route_flag_valid_mask();
            next.route_flag_revision++;
        }
        if (usb_file_transfer.kind == USB_FILE_ROUTE_GLOBE ||
            usb_file_transfer.kind == USB_FILE_ROUTE_FRAME) {
            next.map_pushes++;
        }
        next.revision++;
        next.last_push_unix = (int)time(NULL);
        next.last_error[0] = '\0';
        write_status(&next);
    }

    char completed_name[sizeof(usb_file_transfer.name)];
    snprintf(completed_name,
             sizeof(completed_name),
             "%s",
             usb_file_transfer.name);
    memset(&usb_file_transfer, 0, sizeof(usb_file_transfer));
    usb_config_send(USB_FILE_DONE_PREFIX, completed_name);
}

static void usb_state_set(const char *body)
{
    cJSON *root = body != NULL ? cJSON_Parse(body) : NULL;
    if (root == NULL) {
        usb_config_send(USB_STATE_ERROR_PREFIX, "invalid JSON");
        if (usb_package_active) {
            usb_package_active = false;
            app_data_set_paused(false);
        }
        return;
    }
    const esp_err_t err = apply_state_root(root);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        usb_config_send(USB_STATE_ERROR_PREFIX, "invalid state JSON");
        if (usb_package_active) {
            usb_package_active = false;
            app_data_set_paused(false);
        }
        return;
    }
    /* A transport-level ACK is not enough for tracked-flight packages: the
     * Mac must be able to prove that the firmware retained the intended
     * flight after parsing, persistence, and slot selection. */
    /* Assets arrive before state because the state chooses which of the four
     * flight slots is active. Re-index that selected slot after applying the
     * state even when the slot number did not change. Without this pass a
     * same-slot USB refresh could retain an empty/stale mask and acknowledge
     * the flight while the renderer saw no map or flags. Bump the revisions
     * to make LVGL reload newly installed files instead of cached pixels. */
    app_remote_status_t applied = read_status();
    applied.route_map_valid_mask = route_map_valid_mask_for_slot(
        applied.tracked_flight_slot);
    applied.route_flag_valid_mask = route_flag_valid_mask_for_slot(
        applied.tracked_flight_slot);
    applied.map_valid = applied.route_map_valid_mask != 0;
    applied.map_bytes = applied.map_valid ? APP_REMOTE_MAP_BYTES : 0;
    applied.map_revision++;
    applied.route_flag_revision++;
    route_flag_loaded_revision[0] = UINT32_MAX;
    route_flag_loaded_revision[1] = UINT32_MAX;
    applied.revision++;
    persist_tracked_state(&applied);
    write_status(&applied);

    cJSON *tracker = app_tracker_snapshot();
    const cJSON *count = cJSON_GetObjectItemCaseSensitive(tracker, "preparedFlightCount");
    int prepared_count = cJSON_IsNumber(count) ? count->valueint : 0;
    cJSON_Delete(tracker);
    char response[208];
    snprintf(
        response,
        sizeof(response),
        "{\"trackedFlightValid\":%s,\"trackedFlightNumber\":\"%.15s\","
        "\"deviceSlot\":%u,\"routeMapMask\":%lu,\"routeFlagMask\":%lu,\"preparedFlightCount\":%d}",
        applied.tracked_flight_valid ? "true" : "false",
        applied.tracked_flight_number,
        (unsigned)applied.tracked_flight_slot,
        (unsigned long)applied.route_map_valid_mask,
        (unsigned long)applied.route_flag_valid_mask,
        prepared_count);
    usb_config_send(USB_STATE_OK_PREFIX, response);
    if (usb_package_active) {
        usb_package_active = false;
        app_data_set_paused(false);
    }
}

static void usb_config_process_line(char *line)
{
    if (line == NULL) {
        return;
    }
    while (*line != '\0' && isspace((unsigned char)*line)) {
        line++;
    }
    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }

    if (strcmp(line, USB_CONFIG_PING_COMMAND) == 0) {
        usb_config_send(USB_CONFIG_PONG_PREFIX, "READY");
        return;
    }
    if (strcmp(line, USB_CONFIG_CAPS_COMMAND) == 0) {
        usb_config_send(USB_CONFIG_CAPS_PREFIX, "BIN1,TRACKER1,LAUNCH1");
        return;
    }

    if (strncmp(line,
                USB_FILE_PREPARE_COMMAND,
                strlen(USB_FILE_PREPARE_COMMAND)) == 0 &&
        (line[strlen(USB_FILE_PREPARE_COMMAND)] == '\0' ||
         line[strlen(USB_FILE_PREPARE_COMMAND)] == ':')) {
        uint32_t retained_slot_mask = 0;
        const char *mask_text = line + strlen(USB_FILE_PREPARE_COMMAND);
        if (*mask_text == ':') {
            char *mask_end = NULL;
            const unsigned long parsed = strtoul(mask_text + 1, &mask_end, 16);
            if (mask_end == mask_text + 1 || *mask_end != '\0' ||
                parsed >= (1UL << TRACKED_FLIGHT_SLOT_COUNT)) {
                usb_config_send(USB_FILE_ERROR_PREFIX, "invalid retained slot mask");
                return;
            }
            retained_slot_mask = (uint32_t)parsed;
        }
        /* End any abandoned stream before starting a new authoritative
         * package. The new package immediately re-pauses background fetches. */
        usb_file_abort();
        usb_package_active = true;
        usb_package_last_activity_tick = xTaskGetTickCount();
        app_data_set_paused(true);
        prepare_tracked_flight_storage(retained_slot_mask);
        usb_config_send(USB_FILE_PREPARED_PREFIX, "READY");
        return;
    }

    if (strncmp(line,
                USB_FILE_BEGIN_PREFIX,
                strlen(USB_FILE_BEGIN_PREFIX)) == 0) {
        usb_file_begin(line + strlen(USB_FILE_BEGIN_PREFIX));
        return;
    }
    if (strncmp(line,
                USB_FILE_CHUNK_PREFIX,
                strlen(USB_FILE_CHUNK_PREFIX)) == 0) {
        usb_file_chunk(line + strlen(USB_FILE_CHUNK_PREFIX));
        return;
    }
    if (strncmp(line,
                USB_FILE_DATA_PREFIX,
                strlen(USB_FILE_DATA_PREFIX)) == 0) {
        usb_file_binary_begin(line + strlen(USB_FILE_DATA_PREFIX));
        return;
    }
    if (strcmp(line, USB_FILE_END_COMMAND) == 0) {
        usb_file_finish();
        return;
    }
    if (strncmp(line,
                USB_STATE_SET_PREFIX,
                strlen(USB_STATE_SET_PREFIX)) == 0) {
        usb_state_set(line + strlen(USB_STATE_SET_PREFIX));
        return;
    }

    if (strcmp(line, "ASTTRACK:GET") == 0 || strcmp(line, "ASTLAUNCH:GET") == 0) {
        const bool launch_request = strcmp(line, "ASTLAUNCH:GET") == 0;
        if (launch_request) ESP_LOGI(TAG, "USB launch snapshot requested");
        cJSON *snapshot = strcmp(line, "ASTLAUNCH:GET") == 0 ? app_launches_json() : app_tracker_snapshot();
        char *body = cJSON_PrintUnformatted(snapshot);
        cJSON_Delete(snapshot);
        if (launch_request) ESP_LOGI(TAG, "USB launch snapshot encoded: %u bytes", body ? (unsigned)strlen(body) : 0);
        if (body) { usb_config_send(USB_CONFIG_OK_PREFIX, body); cJSON_free(body); }
        else usb_config_send(USB_CONFIG_ERROR_PREFIX, "could not read tracker");
        return;
    }
    if (strcmp(line, USB_CONFIG_GET_COMMAND) == 0) {
        if (config_mutex != NULL) {
            xSemaphoreTake(config_mutex, portMAX_DELAY);
        }
        cJSON *root = build_shared_config_json(NULL, false, false);
        if (config_mutex != NULL) {
            xSemaphoreGive(config_mutex);
        }
        if (root == NULL) {
            usb_config_send(USB_CONFIG_ERROR_PREFIX, "could not build config");
            return;
        }
        char *body = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (body == NULL) {
            usb_config_send(USB_CONFIG_ERROR_PREFIX, "could not encode config");
            return;
        }
        usb_config_send(USB_CONFIG_OK_PREFIX, body);
        cJSON_free(body);
        return;
    }

    if (strncmp(
            line,
            USB_CONFIG_SET_PREFIX,
            strlen(USB_CONFIG_SET_PREFIX)) != 0) {
        return;
    }
    const char *body = line + strlen(USB_CONFIG_SET_PREFIX);
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        usb_config_send(USB_CONFIG_ERROR_PREFIX, "invalid JSON");
        return;
    }

    cJSON *response = NULL;
    bool reconnect_wifi = false;
    bool location_changed = false;
    char error[96] = {0};
    if (config_mutex != NULL) {
        xSemaphoreTake(config_mutex, portMAX_DELAY);
    }
    esp_err_t err = apply_shared_config_root(
        root,
        &response,
        &reconnect_wifi,
        &location_changed,
        error,
        sizeof(error));
    if (config_mutex != NULL) {
        xSemaphoreGive(config_mutex);
    }
    cJSON_Delete(root);
    if (err != ESP_OK || response == NULL) {
        usb_config_send(
            USB_CONFIG_ERROR_PREFIX,
            error[0] != '\0' ? error : "could not save config");
        return;
    }

    char *response_body = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (response_body == NULL) {
        usb_config_send(USB_CONFIG_ERROR_PREFIX, "could not encode saved config");
        return;
    }
    usb_config_send(USB_CONFIG_OK_PREFIX, response_body);
    cJSON_free(response_body);
    finish_config_application(reconnect_wifi, location_changed);
}

static void usb_config_task(void *arg)
{
    (void)arg;
    /* The ROM flashing path already uses USB in buffered blocks. Runtime sync
     * previously read the 64-byte hardware FIFO directly through the console
     * VFS, so an otherwise fast 434 KiB map took about fourteen seconds. Keep
     * the same USB peripheral, but put its small interrupt-driven ring buffer
     * in front of both the console and the configuration protocol. Direct
     * block reads below avoid the VFS's per-character loop as well. */
    usb_serial_jtag_driver_config_t driver_config = {
        .rx_buffer_size = 4096,
        .tx_buffer_size = APP_REMOTE_USB_TX_BUFFER_BYTES,
    };
    esp_err_t driver_error = usb_serial_jtag_driver_install(&driver_config);
    if (driver_error == ESP_OK ||
        (driver_error == ESP_ERR_INVALID_STATE &&
         usb_serial_jtag_is_driver_installed())) {
        usb_serial_jtag_vfs_use_driver();
        usb_config_driver_ready = true;
        ESP_LOGI(TAG, "Buffered USB-C config transport ready");
    } else {
        usb_config_driver_ready = false;
        ESP_LOGW(TAG,
                 "Buffered USB transport unavailable (%s); using console fallback",
                 esp_err_to_name(driver_error));
    }
    /* The console VFS normally treats CR as a newline. That is correct for an
     * interactive terminal, but corrupts an arbitrary RGB565 byte stream.
     * Commands already use LF, so keep every received byte intact. */
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
    usb_config_fd = open("/dev/secondary", O_RDWR | O_NONBLOCK);
    if (usb_config_fd < 0) {
        ESP_LOGW(TAG, "Could not open USB config channel: errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "USB-C config channel ready");

    char *line = (char *)heap_caps_malloc(
        USB_CONFIG_LINE_MAX,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *input = (uint8_t *)heap_caps_malloc(
        USB_CONFIG_READ_BUFFER_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    usb_file_decode_buffer = (uint8_t *)heap_caps_malloc(
        USB_FILE_DECODE_BUFFER_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (line == NULL || input == NULL || usb_file_decode_buffer == NULL) {
        ESP_LOGW(TAG, "Could not allocate USB config transfer buffers");
        heap_caps_free(line);
        heap_caps_free(input);
        heap_caps_free(usb_file_decode_buffer);
        usb_file_decode_buffer = NULL;
        vTaskDelete(NULL);
        return;
    }
    size_t used = 0;
    int64_t last_binary_yield_us = esp_timer_get_time();
    while (true) {
        /* Drain native USB in page-sized blocks. The previous 256-byte reads
         * turned every 466x466 map into thousands of tiny filesystem writes;
         * on a busy display that could exceed the Mac's ACK deadline even
         * though the cable itself was idle. */
        const int count = usb_config_driver_ready
            ? usb_serial_jtag_read_bytes(
                  input,
                  USB_CONFIG_READ_BUFFER_SIZE,
                  0)
            : (int)read(
                  usb_config_fd, input, USB_CONFIG_READ_BUFFER_SIZE);
        for (int index = 0; index < count;) {
            if (usb_file_transfer.binary_remaining > 0) {
                const size_t available = (size_t)(count - index);
                const size_t binary_count =
                    available < usb_file_transfer.binary_remaining
                        ? available
                        : usb_file_transfer.binary_remaining;
                if (!usb_file_binary_write(input + index, binary_count)) {
                    used = 0;
                    break;
                }
                index += (int)binary_count;
                continue;
            }
            const char character = (char)input[index];
            index++;
            if (character == '\n') {
                line[used] = '\0';
                usb_config_process_line(line);
                used = 0;
                continue;
            }
            if (character == '\r') {
                continue;
            }
            if (used + 1 < USB_CONFIG_LINE_MAX) {
                line[used++] = character;
            } else {
                used = 0;
                usb_config_send(USB_CONFIG_ERROR_PREFIX, "config command too large");
            }
        }
        if (count <= 0) {
            if (usb_file_transfer.binary_remaining > 0) {
                /* Native USB delivers small hardware packets. Sleeping after
                 * every temporarily empty FIFO limited the old path to only a
                 * few KB/s. Poll continuously while a declared binary block is
                 * in flight, with a brief scheduler yield every 4 ms so the UI
                 * remains healthy without inserting a pause per USB packet. */
                const int64_t now_us = esp_timer_get_time();
                if (now_us - last_binary_yield_us >= 4000LL) {
                    vTaskDelay(1);
                    last_binary_yield_us = esp_timer_get_time();
                }
            } else {
                /* While a package is active the next framed block follows an
                 * ACK immediately. A 10 ms terminal-style polling delay per
                 * frame made a four-flight/eight-map package needlessly slow;
                 * 1 ms remains responsive without busy-spinning. */
                vTaskDelay(pdMS_TO_TICKS(usb_package_active ? 1 : 10));
                last_binary_yield_us = esp_timer_get_time();
            }
        }
        if (usb_file_transfer.kind != USB_FILE_NONE &&
            (xTaskGetTickCount() - usb_file_transfer.last_activity_tick) >
                pdMS_TO_TICKS(30000)) {
            ESP_LOGW(TAG, "USB file transfer timed out: %s",
                     usb_file_transfer.name);
            usb_file_abort();
        }
        if (usb_package_active &&
            usb_file_transfer.kind == USB_FILE_NONE &&
            (xTaskGetTickCount() - usb_package_last_activity_tick) >
                pdMS_TO_TICKS(30000)) {
            ESP_LOGW(TAG, "USB package transaction timed out");
            usb_package_active = false;
            app_data_set_paused(false);
        }
    }
}

static esp_err_t data_refresh_post_handler(httpd_req_t *req)
{
    (void)req;
    app_launches_request_refresh();
    app_data_request_refresh();
    return send_json(req, "{\"ok\":true}");
}

static void refresh_launch_asset_valid_mask(void)
{
    uint32_t mask = 0;
    int launch_index = 0;
    for (int index = 0; index < SPACE_ASSET_COUNT; index++) {
        const space_asset_t *asset = &space_assets[index];
        if (strncmp(asset->name, "launch_", 7) != 0) {
            continue;
        }
        if ((space_asset_valid_mask_cache &
             (1UL << index)) != 0) {
            mask |= 1UL << launch_index;
        }
        launch_index++;
    }
    launch_asset_valid_mask_cache = mask;
}

static void refresh_space_asset_valid_mask(void)
{
    uint32_t mask = 0;
    for (int index = 0; index < SPACE_ASSET_COUNT; index++) {
        if (space_asset_file_is_valid(&space_assets[index])) {
            mask |= 1UL << index;
        } else {
            ESP_LOGW(
                TAG,
                "Space artwork failed integrity check: %s",
                space_assets[index].name);
        }
    }
    space_asset_valid_mask_cache = mask;
    refresh_launch_asset_valid_mask();
    ESP_LOGI(
        TAG,
        "Space artwork inventory: 0x%04lx/0x%04lx valid",
        (unsigned long)mask,
        (unsigned long)space_asset_expected_mask());
}

static uint32_t launch_asset_valid_mask(void)
{
    /*
     * Never touch FAT from the HTTP status handler. The display may be loading
     * an asset on the other core, and newlib/FAT stat lock contention can hold
     * the HTTP task long enough to trip the interrupt watchdog.
     */
    return launch_asset_valid_mask_cache;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    app_remote_status_t remote = read_status();
    app_network_status_t net;
    app_network_get_status(&net);
    app_data_status_t data;
    app_data_get_status(&data);
    app_power_battery_status_t battery;
    app_power_get_battery_status(&battery);

    int route_progress_milli = (int)(remote.route_progress * 1000.0f + 0.5f);
    if (route_progress_milli < 0) {
        route_progress_milli = 0;
    } else if (route_progress_milli > 1000) {
        route_progress_milli = 1000;
    }

    char radar_map_inventory[112] = "";
    char query[64];
    char inventory_value[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(
            query,
            "mapInventory",
            inventory_value,
            sizeof(inventory_value)) == ESP_OK &&
        strcmp(inventory_value, "1") == 0) {
        size_t used = 0;
        for (size_t i = 0; i < app_location_preset_count(); i++) {
            const app_location_preset_t *preset = app_location_preset_at(i);
            if (preset == NULL) {
                continue;
            }
            const uint32_t mask =
                radar_map_mask_for_location_key(preset->key);
            const int written = snprintf(
                radar_map_inventory + used,
                sizeof(radar_map_inventory) - used,
                "%s%s:%lu",
                used == 0 ? "" : ",",
                preset->key,
                (unsigned long)mask);
            if (written < 0 ||
                (size_t)written >= sizeof(radar_map_inventory) - used) {
                break;
            }
            used += (size_t)written;
        }
    }

    char *body = (char *)heap_caps_malloc(APP_REMOTE_STATUS_JSON_MAX, MALLOC_CAP_8BIT);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not allocate status JSON");
        return ESP_FAIL;
    }

    snprintf(body, APP_REMOTE_STATUS_JSON_MAX,
             "{\"ok\":true,\"ip\":\"%s\",\"style\":\"%s\",\"routeProgress\":%d.%03d,"
             "\"batteryAvailable\":%s,\"batteryPresent\":%s,\"batteryCharging\":%s,\"batteryPercent\":%d,\"lastPushUnix\":%d,"
             "\"trackedFlightValid\":%s,\"trackedFlightNumber\":\"%s\","
             "\"trackedFlightAltitudeM\":%d,\"trackedFlightSpeedKPH\":%d,"
             "\"trackedFlightHeadingDegrees\":%d,\"trackedFlightHeadingValid\":%s,"
             "\"deviceLaunchesSupported\":true,\"launchValid\":%s,\"launchOptionMask\":%u,\"launchAssetMask\":%lu,"
             "\"spaceAssetFileMask\":%lu,\"spaceAssetLoadedMask\":%lu,"
             "\"spaceAssetExpectedMask\":%lu,\"spaceAssetLoadFailures\":%lu,"
             "\"weatherCityIconMask\":%lu,\"weatherCityIconExpectedMask\":%lu,"
             "\"psramFreeKB\":%u,\"psramLargestKB\":%u,"
             "\"routePointCount\":%u,"
             "\"wifiState\":\"%s\",\"ssid\":\"%s\",\"locationKey\":\"%s\",\"location\":\"%s\",\"lat\":\"%s\",\"lon\":\"%s\","
             "\"dataState\":\"%s\",\"fetchedUnix\":%d,\"weatherValid\":%s,\"metarValid\":%s,"
             "\"radarValid\":%s,\"aircraftValid\":%s,\"aircraftCount\":%d,"
             "\"aircraftAuthUsed\":%s,\"aircraftHttpStatus\":%d,"
             "\"aircraftLabels\":%s,\"radarWedge\":%s,\"accentColor\":\"%s\","
             "\"mapValid\":%s,\"mapBytes\":%u,\"routeMapMask\":%lu,\"routeFlagMask\":%lu,"
             "\"radarMapValid\":%s,\"radarMapBytes\":%u,"
             "\"radarMapMask\":%lu,\"radarMapInventory\":\"%s\","
             "\"revision\":%lu,\"mapRevision\":%lu,\"radarMapRevision\":%lu,"
             "\"statePushes\":%lu,\"mapPushes\":%lu,\"radarMapPushes\":%lu,\"lastError\":\"%s\"}",
             net.ip,
             app_remote_style_text(remote.style),
             route_progress_milli / 1000,
             route_progress_milli % 1000,
             battery.available ? "true" : "false",
             battery.present ? "true" : "false",
             battery.charging ? "true" : "false",
             battery.percent,
             remote.last_push_unix,
             remote.tracked_flight_valid ? "true" : "false",
             remote.tracked_flight_number,
             remote.tracked_flight_altitude_m,
             remote.tracked_flight_speed_kph,
             remote.tracked_flight_heading_deg,
             remote.tracked_flight_heading_valid ? "true" : "false",
             remote.launch_valid ? "true" : "false",
             (unsigned)remote.launch_option_mask,
             (unsigned long)launch_asset_valid_mask(),
             (unsigned long)space_asset_valid_mask_cache,
             (unsigned long)space_asset_loaded_mask(),
             (unsigned long)space_asset_expected_mask(),
             (unsigned long)space_asset_load_failures,
             (unsigned long)weather_city_icon_valid_mask_cache,
             (unsigned long)(
                 app_location_preset_count() >= 32
                     ? UINT32_MAX
                     : ((1UL << app_location_preset_count()) - 1UL)),
             (unsigned)(
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM) /
                 1024U),
             (unsigned)(
                 heap_caps_get_largest_free_block(
                     MALLOC_CAP_SPIRAM) /
                 1024U),
             (unsigned)remote.tracked_route_point_count,
             app_wifi_state_text(net.wifi_state),
             net.configured_ssid,
             net.location_key,
             net.location_label,
             net.latitude,
             net.longitude,
             app_data_state_text(data.state),
             data.fetched_unix,
             data.weather_valid ? "true" : "false",
             data.metar_valid ? "true" : "false",
             data.radar_valid ? "true" : "false",
             data.aircraft_valid ? "true" : "false",
             data.aircraft_count,
             data.aircraft_auth_used ? "true" : "false",
             data.aircraft_http_status,
             net.aircraft_labels_enabled ? "true" : "false",
             net.radar_wedge_enabled ? "true" : "false",
             net.accent_color,
             remote.map_valid ? "true" : "false",
             (unsigned)remote.map_bytes,
             (unsigned long)remote.route_map_valid_mask,
             (unsigned long)remote.route_flag_valid_mask,
             remote.radar_map_valid ? "true" : "false",
             (unsigned)remote.radar_map_bytes,
             (unsigned long)remote.radar_map_valid_mask,
             radar_map_inventory,
             (unsigned long)remote.revision,
             (unsigned long)remote.map_revision,
             (unsigned long)remote.radar_map_revision,
             (unsigned long)remote.state_pushes,
             (unsigned long)remote.map_pushes,
             (unsigned long)remote.radar_map_pushes,
             remote.last_error);
    esp_err_t err = send_json(req, body);
    heap_caps_free(body);
    return err;
}

static esp_err_t aircraft_get_handler(httpd_req_t *req)
{
    app_data_status_t data;
    app_data_get_status(&data);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not allocate aircraft JSON");
        return ESP_FAIL;
    }

    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "aircraftValid", data.aircraft_valid);
    cJSON_AddNumberToObject(root, "aircraftCount", data.aircraft_count);
    cJSON_AddNumberToObject(root, "aircraftRawCount", data.aircraft_raw_count);
    cJSON_AddNumberToObject(root, "aircraftUsableCount", data.aircraft_usable_count);
    cJSON_AddNumberToObject(root, "aircraftHttpStatus", data.aircraft_http_status);
    cJSON_AddNumberToObject(root, "aircraftRadiusMiles", data.aircraft_radius_miles);
    cJSON_AddBoolToObject(root, "aircraftAuthUsed", data.aircraft_auth_used);
    cJSON_AddNumberToObject(root, "fetchedUnix", data.fetched_unix);
    cJSON_AddNumberToObject(root, "nowUnix", (double)time(NULL));

    cJSON *items = cJSON_AddArrayToObject(root, "aircraft");
    if (items != NULL) {
        const int count = data.aircraft_count < APP_MAX_AIRCRAFT ? data.aircraft_count : APP_MAX_AIRCRAFT;
        for (int i = 0; i < count; i++) {
            const app_aircraft_t *aircraft = &data.aircraft[i];
            if (!aircraft->valid) {
                continue;
            }
            cJSON *item = cJSON_CreateObject();
            if (item == NULL) {
                continue;
            }
            cJSON_AddStringToObject(item, "icao24", aircraft->icao24);
            cJSON_AddStringToObject(item, "callsign", aircraft->callsign);
            cJSON_AddStringToObject(item, "typecode", aircraft->typecode);
            cJSON_AddStringToObject(item, "model", aircraft->model);
            cJSON_AddStringToObject(item, "registration", aircraft->registration);
            cJSON_AddStringToObject(item, "manufacturer", aircraft->manufacturer);
            cJSON_AddStringToObject(item, "engineType", aircraft->engine_type);
            cJSON_AddNumberToObject(item, "engineCount", aircraft->engine_count);
            cJSON_AddStringToObject(
                item,
                "typeSource",
                airspace_aircraft_type_source_text(aircraft->type_source));
            cJSON_AddStringToObject(item, "airlineName", aircraft->airline_name);
            cJSON_AddStringToObject(item, "airlineIata", aircraft->airline_iata);
            cJSON_AddStringToObject(item, "airlineIcao", aircraft->airline_icao);
            cJSON_AddStringToObject(item, "origin", aircraft->origin_iata);
            cJSON_AddStringToObject(item, "destination", aircraft->destination_iata);
            char origin_city[24] = {0};
            char destination_city[24] = {0};
            app_data_get_airport_city(aircraft->origin_iata,
                                      origin_city,
                                      sizeof(origin_city));
            app_data_get_airport_city(aircraft->destination_iata,
                                      destination_city,
                                      sizeof(destination_city));
            cJSON_AddStringToObject(item, "originCity", origin_city);
            cJSON_AddStringToObject(item, "destinationCity", destination_city);
            cJSON_AddBoolToObject(item, "metadataValid", aircraft->metadata_valid);
            cJSON_AddBoolToObject(item, "routeValid", aircraft->route_valid);
            cJSON_AddBoolToObject(item,
                                  "routeProgressValid",
                                  aircraft->route_progress_valid);
            cJSON_AddNumberToObject(item,
                                    "routeProgress",
                                    aircraft->route_progress);
            cJSON_AddNumberToObject(item,
                                    "routeDistanceNm",
                                    aircraft->route_distance_nm);
            cJSON_AddNumberToObject(item,
                                    "routeDistanceCoveredNm",
                                    aircraft->route_distance_covered_nm);
            cJSON_AddNumberToObject(item,
                                    "routeDistanceRemainingNm",
                                    aircraft->route_distance_remaining_nm);
            cJSON_AddStringToObject(item, "iconLabel", airspace_aircraft_icon_label_for_aircraft(aircraft));
            cJSON_AddNumberToObject(item, "latitude", aircraft->latitude);
            cJSON_AddNumberToObject(item, "longitude", aircraft->longitude);
            cJSON_AddNumberToObject(item, "altitudeFt", aircraft->altitude_ft);
            cJSON_AddNumberToObject(item, "speedKt", aircraft->speed_kt);
            cJSON_AddNumberToObject(item, "trackDeg", aircraft->track_deg);
            cJSON_AddBoolToObject(item, "trackValid", aircraft->track_valid);
            cJSON_AddNumberToObject(item, "verticalRateMps", aircraft->vertical_rate_mps);
            cJSON_AddBoolToObject(item, "verticalRateValid", aircraft->vertical_rate_valid);
            cJSON_AddNumberToObject(item, "positionUnix", aircraft->position_unix);
            cJSON_AddNumberToObject(item, "contactUnix", aircraft->contact_unix);
            cJSON_AddNumberToObject(item, "category", aircraft->category);
            cJSON_AddNumberToObject(item, "distanceNm", aircraft->distance_nm);
            cJSON_AddNumberToObject(item, "bearingDeg", aircraft->bearing_deg);
            cJSON_AddItemToArray(items, item);
        }
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not encode aircraft JSON");
        return ESP_FAIL;
    }
    esp_err_t err = send_json(req, body);
    cJSON_free(body);
    return err;
}

static esp_err_t weather_get_handler(httpd_req_t *req)
{
    app_data_status_t data;
    app_data_get_status(&data);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "could not allocate weather JSON");
        return ESP_FAIL;
    }

    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "weatherValid", data.weather_valid);
    cJSON_AddBoolToObject(
        root,
        "airQualityValid",
        data.weather_air_quality_valid);
    cJSON_AddStringToObject(root, "locationKey", data.weather_location_key);
    cJSON_AddNumberToObject(root, "fetchedUnix", data.fetched_unix);
    cJSON_AddNumberToObject(root, "temperature", data.weather_temperature);
    cJSON_AddNumberToObject(
        root,
        "apparentTemperature",
        data.weather_apparent_temperature);
    cJSON_AddNumberToObject(root, "code", data.weather_code);
    cJSON_AddNumberToObject(
        root,
        "humidityPercent",
        data.weather_humidity_percent);
    cJSON_AddNumberToObject(root, "pressureHPA", data.weather_pressure_hpa);
    cJSON_AddNumberToObject(
        root,
        "precipitationTenthsMM",
        data.weather_precipitation_tenths_mm);
    cJSON_AddNumberToObject(root, "uvTenths", data.weather_uv_tenths);
    cJSON_AddNumberToObject(root, "aqi", data.weather_aqi);
    cJSON_AddNumberToObject(root, "windKT", data.weather_wind_kt);
    cJSON_AddNumberToObject(root, "windDegrees", data.weather_wind_deg);
    app_config_t weather_cfg;
    app_config_get(&weather_cfg);
    cJSON_AddStringToObject(
        root,
        "unit",
        weather_cfg.weather_units == APP_WEATHER_UNITS_FAHRENHEIT
            ? "F"
            : "C");
    cJSON_AddStringToObject(root, "summary", data.weather_summary);
    cJSON_AddStringToObject(root, "wind", data.weather_wind);
    cJSON_AddStringToObject(root, "sunrise", data.weather_sunrise);
    cJSON_AddStringToObject(root, "sunset", data.weather_sunset);

    cJSON *forecast = cJSON_AddArrayToObject(root, "forecast");
    if (forecast != NULL) {
        for (int index = 0; index < APP_WEATHER_FORECAST_DAYS; index++) {
            cJSON *item = cJSON_CreateObject();
            if (item == NULL) {
                continue;
            }
            cJSON_AddStringToObject(item, "day", data.forecast_day[index]);
            cJSON_AddNumberToObject(item, "code", data.forecast_code[index]);
            cJSON_AddNumberToObject(item, "high", data.forecast_high[index]);
            cJSON_AddNumberToObject(item, "low", data.forecast_low[index]);
            cJSON_AddNumberToObject(
                item,
                "precipitationPercent",
                data.forecast_precip_percent[index]);
            cJSON_AddItemToArray(forecast, item);
        }
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == NULL) {
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "could not encode weather JSON");
        return ESP_FAIL;
    }
    esp_err_t err = send_json(req, body);
    cJSON_free(body);
    return err;
}

bool app_remote_decode_state(const cJSON *root, app_remote_status_t *out)
{
    if (!cJSON_IsObject(root) || out == NULL) {
        return false;
    }

    app_remote_status_t next = *out;
    const cJSON *flight_slot =
        cJSON_GetObjectItemCaseSensitive(root, "deviceSlot");
    if (cJSON_IsNumber(flight_slot)) {
        const uint8_t previous_slot = next.tracked_flight_slot;
        next.tracked_flight_slot = (uint8_t)tracked_flight_slot_clamped(
            flight_slot->valueint);
        if (next.tracked_flight_slot != previous_slot) {
            next.route_map_valid_mask = route_map_valid_mask_for_slot(
                next.tracked_flight_slot);
            next.route_flag_valid_mask = route_flag_valid_mask_for_slot(
                next.tracked_flight_slot);
            next.map_valid = next.route_map_valid_mask != 0;
            next.map_bytes = next.map_valid ? APP_REMOTE_MAP_BYTES : 0;
            next.map_revision++;
            next.route_flag_revision++;
            route_flag_loaded_revision[0] = UINT32_MAX;
            route_flag_loaded_revision[1] = UINT32_MAX;
        }
    }
    const cJSON *style = cJSON_GetObjectItemCaseSensitive(root, "style");
    if (cJSON_IsString(style)) {
        next.style = parse_style(style->valuestring);
    }

    const cJSON *progress = cJSON_GetObjectItemCaseSensitive(root, "routeProgress");
    if (cJSON_IsNumber(progress)) {
        float p = (float)progress->valuedouble;
        if (p < 0.0f) {
            p = 0.0f;
        }
        if (p > 1.0f) {
            p = 1.0f;
        }
        next.route_progress = p;
    }

    const cJSON *route_points =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "routePoints");
    if (cJSON_IsArray(route_points)) {
        next.tracked_route_point_count = 0;
        const int route_point_count =
            cJSON_GetArraySize(route_points);
        for (int index = 0;
             index < route_point_count &&
             index < APP_REMOTE_TRACKED_ROUTE_MAX_POINTS;
             index++) {
            const cJSON *point =
                cJSON_GetArrayItem(route_points, index);
            const cJSON *x = cJSON_IsObject(point)
                ? cJSON_GetObjectItemCaseSensitive(
                    point,
                    "x")
                : NULL;
            const cJSON *y = cJSON_IsObject(point)
                ? cJSON_GetObjectItemCaseSensitive(
                    point,
                    "y")
                : NULL;
            if (!cJSON_IsNumber(x) ||
                !cJSON_IsNumber(y)) {
                continue;
            }
            next.tracked_route_points[
                next.tracked_route_point_count
            ][0] = (int16_t)fmin(
                APP_REMOTE_MAP_WIDTH - 1,
                fmax(0, x->valuedouble));
            next.tracked_route_points[
                next.tracked_route_point_count
            ][1] = (int16_t)fmin(
                APP_REMOTE_MAP_HEIGHT - 1,
                fmax(0, y->valuedouble));
            next.tracked_route_point_count++;
        }
        next.tracked_route_geometry_version++;
    }

    const cJSON *tracking_active =
        cJSON_GetObjectItemCaseSensitive(root, "trackingActive");
    const bool tracking_active_provided = cJSON_IsBool(tracking_active);
    const bool tracking_active_value =
        !tracking_active_provided || cJSON_IsTrue(tracking_active);

    const cJSON *flight = cJSON_GetObjectItemCaseSensitive(root, "flight");
    if (cJSON_IsObject(flight)) {
        next.tracked_flight_valid = tracking_active_value;
        copy_json_string(next.tracked_flight_number, sizeof(next.tracked_flight_number), flight, "number");
        copy_json_string(next.tracked_flight_callsign, sizeof(next.tracked_flight_callsign), flight, "callsign");
        copy_json_string(next.tracked_flight_airline_iata, sizeof(next.tracked_flight_airline_iata), flight, "airlineIata");
        copy_json_string(next.tracked_flight_airline_name, sizeof(next.tracked_flight_airline_name), flight, "airlineName");
        next.tracked_flight_aircraft_type[0] = '\0';
        copy_json_string(next.tracked_flight_aircraft_type,
                         sizeof(next.tracked_flight_aircraft_type),
                         flight,
                         "aircraftTypeCode");
        if (next.tracked_flight_aircraft_type[0] == '\0') {
            copy_json_string(next.tracked_flight_aircraft_type,
                             sizeof(next.tracked_flight_aircraft_type),
                             flight,
                             "aircraftType");
        }
        copy_json_string(next.tracked_flight_origin, sizeof(next.tracked_flight_origin), flight, "origin");
        copy_json_string(next.tracked_flight_origin_city, sizeof(next.tracked_flight_origin_city), flight, "originCity");
        copy_json_string(next.tracked_flight_origin_country,
                         sizeof(next.tracked_flight_origin_country),
                         flight,
                         "originCountryCode");
        copy_json_string(next.tracked_flight_destination, sizeof(next.tracked_flight_destination), flight, "destination");
        copy_json_string(next.tracked_flight_destination_city, sizeof(next.tracked_flight_destination_city), flight, "destinationCity");
        copy_json_string(next.tracked_flight_destination_country,
                         sizeof(next.tracked_flight_destination_country),
                         flight,
                         "destinationCountryCode");
        copy_json_string(next.tracked_flight_origin_terminal,
                         sizeof(next.tracked_flight_origin_terminal),
                         flight,
                         "originTerminal");
        copy_json_string(next.tracked_flight_origin_gate,
                         sizeof(next.tracked_flight_origin_gate),
                         flight,
                         "originGate");
        copy_json_string(next.tracked_flight_destination_terminal,
                         sizeof(next.tracked_flight_destination_terminal),
                         flight,
                         "destinationTerminal");
        copy_json_string(next.tracked_flight_destination_gate,
                         sizeof(next.tracked_flight_destination_gate),
                         flight,
                         "destinationGate");
        copy_json_string(next.tracked_flight_status, sizeof(next.tracked_flight_status), flight, "status");
        copy_json_string(next.tracked_flight_duration, sizeof(next.tracked_flight_duration), flight, "duration");
        copy_json_string(next.tracked_flight_distance, sizeof(next.tracked_flight_distance), flight, "distance");
        copy_json_string(next.tracked_flight_eta, sizeof(next.tracked_flight_eta), flight, "eta");
        copy_json_string(next.tracked_flight_elapsed, sizeof(next.tracked_flight_elapsed), flight, "elapsed");
        copy_json_string(next.tracked_flight_remaining, sizeof(next.tracked_flight_remaining), flight, "remaining");
        copy_json_string(next.tracked_flight_phase, sizeof(next.tracked_flight_phase), flight, "phase");
        copy_json_string(next.tracked_flight_journey_phase,
                         sizeof(next.tracked_flight_journey_phase),
                         flight,
                         "journeyPhase");
        const cJSON *route_distance_km =
            cJSON_GetObjectItemCaseSensitive(
                flight,
                "routeDistanceKilometers");
        next.tracked_flight_route_distance_km =
            cJSON_IsNumber(route_distance_km)
                ? route_distance_km->valueint
                : 0;
        const cJSON *origin_current_offset =
            cJSON_GetObjectItemCaseSensitive(
                flight,
                "originCurrentUTCOffsetSeconds");
        next.tracked_flight_origin_current_utc_offset_s =
            cJSON_IsNumber(origin_current_offset)
                ? origin_current_offset->valueint
                : 0;
        const cJSON *destination_current_offset =
            cJSON_GetObjectItemCaseSensitive(
                flight,
                "destinationCurrentUTCOffsetSeconds");
        next.tracked_flight_destination_current_utc_offset_s =
            cJSON_IsNumber(destination_current_offset)
                ? destination_current_offset->valueint
                : 0;
        const cJSON *departure_offset =
            cJSON_GetObjectItemCaseSensitive(
                flight,
                "departureUTCOffsetSeconds");
        next.tracked_flight_departure_utc_offset_s =
            cJSON_IsNumber(departure_offset)
                ? departure_offset->valueint
                : 0;
        const cJSON *arrival_offset =
            cJSON_GetObjectItemCaseSensitive(
                flight,
                "arrivalUTCOffsetSeconds");
        next.tracked_flight_arrival_utc_offset_s =
            cJSON_IsNumber(arrival_offset)
                ? arrival_offset->valueint
                : 0;
        const cJSON *estimated_departure =
            cJSON_GetObjectItemCaseSensitive(
                flight,
                "estimatedDepartureUnix");
        next.tracked_flight_estimated_departure_unix =
            cJSON_IsNumber(estimated_departure)
                ? (int64_t)estimated_departure->valuedouble
                : 0;
        const cJSON *actual_departure =
            cJSON_GetObjectItemCaseSensitive(
                flight,
                "actualDepartureUnix");
        next.tracked_flight_actual_departure_unix =
            cJSON_IsNumber(actual_departure)
                ? (int64_t)actual_departure->valuedouble
                : 0;
        const cJSON *estimated_arrival =
            cJSON_GetObjectItemCaseSensitive(
                flight,
                "estimatedArrivalUnix");
        next.tracked_flight_estimated_arrival_unix =
            cJSON_IsNumber(estimated_arrival)
                ? (int64_t)estimated_arrival->valuedouble
                : 0;
        const cJSON *actual_arrival =
            cJSON_GetObjectItemCaseSensitive(
                flight,
                "actualArrivalUnix");
        next.tracked_flight_actual_arrival_unix =
            cJSON_IsNumber(actual_arrival)
                ? (int64_t)actual_arrival->valuedouble
                : 0;
        const cJSON *altitude_m = cJSON_GetObjectItemCaseSensitive(flight, "altitudeMeters");
        next.tracked_flight_altitude_m = cJSON_IsNumber(altitude_m) ? altitude_m->valueint : 0;
        const cJSON *speed_kph = cJSON_GetObjectItemCaseSensitive(flight, "speedKPH");
        next.tracked_flight_speed_kph = cJSON_IsNumber(speed_kph) ? speed_kph->valueint : 0;
        const cJSON *heading_deg = cJSON_GetObjectItemCaseSensitive(flight, "headingDegrees");
        next.tracked_flight_heading_valid = cJSON_IsNumber(heading_deg);
        next.tracked_flight_heading_deg = next.tracked_flight_heading_valid
            ? heading_deg->valueint
            : 0;
        ESP_LOGI(
            TAG,
            "Tracked update %s: altitude=%dm speed=%dkm/h heading=%s%d",
            next.tracked_flight_number,
            next.tracked_flight_altitude_m,
            next.tracked_flight_speed_kph,
            next.tracked_flight_heading_valid ? "" : "invalid/",
            next.tracked_flight_heading_deg);
        const cJSON *delayed = cJSON_GetObjectItemCaseSensitive(flight, "delayed");
        if (cJSON_IsBool(delayed)) {
            next.tracked_flight_delayed = cJSON_IsTrue(delayed);
        }
        const bool tracked_flight_terminal =
            next.tracked_flight_actual_arrival_unix > 0 ||
            strcasecmp(
                next.tracked_flight_journey_phase,
                "arrived") == 0;
        if (tracked_flight_terminal) {
            next.route_progress = 1.0f;
            next.tracked_flight_altitude_m = 0;
            next.tracked_flight_speed_kph = 0;
            snprintf(
                next.tracked_flight_remaining,
                sizeof(next.tracked_flight_remaining),
                "0m");
            snprintf(
                next.tracked_flight_phase,
                sizeof(next.tracked_flight_phase),
                "landed");
        }
    } else if (tracking_active_provided) {
        next.tracked_flight_valid = tracking_active_value;
    }
    if (tracking_active_provided && !tracking_active_value) {
        clear_tracked_flight_state(&next);
    }

    const cJSON *launch =
        cJSON_GetObjectItemCaseSensitive(root, "launch");
    if (cJSON_IsObject(launch)) {
        const cJSON *available =
            cJSON_GetObjectItemCaseSensitive(
                launch,
                "available");
        next.launch_valid =
            !cJSON_IsBool(available) ||
            cJSON_IsTrue(available);
        copy_json_string(
            next.launch_id,
            sizeof(next.launch_id),
            launch,
            "id");
        copy_json_string(
            next.launch_mission,
            sizeof(next.launch_mission),
            launch,
            "missionName");
        copy_json_string(
            next.launch_full_name,
            sizeof(next.launch_full_name),
            launch,
            "fullName");
        copy_json_string(
            next.launch_agency,
            sizeof(next.launch_agency),
            launch,
            "agency");
        copy_json_string(
            next.launch_vehicle,
            sizeof(next.launch_vehicle),
            launch,
            "vehicle");
        copy_json_string(
            next.launch_site,
            sizeof(next.launch_site),
            launch,
            "site");
        copy_json_string(
            next.launch_pad,
            sizeof(next.launch_pad),
            launch,
            "padName");
        copy_json_string(
            next.launch_location,
            sizeof(next.launch_location),
            launch,
            "locationName");
        copy_json_string(
            next.launch_precision,
            sizeof(next.launch_precision),
            launch,
            "precisionName");
        copy_json_string(
            next.launch_precision_abbrev,
            sizeof(next.launch_precision_abbrev),
            launch,
            "precisionAbbreviation");
        copy_json_string(
            next.launch_status,
            sizeof(next.launch_status),
            launch,
            "statusName");
        copy_json_string(
            next.launch_status_abbrev,
            sizeof(next.launch_status_abbrev),
            launch,
            "statusAbbreviation");
        copy_json_string(
            next.launch_watch_url,
            sizeof(next.launch_watch_url),
            launch,
            "watchURL");
        const cJSON *net =
            cJSON_GetObjectItemCaseSensitive(
                launch,
                "netUnix");
        next.launch_net_unix =
            cJSON_IsNumber(net)
                ? (int64_t)net->valuedouble
                : 0;
        const cJSON *updated =
            cJSON_GetObjectItemCaseSensitive(
                launch,
                "updatedUnix");
        next.launch_updated_unix =
            cJSON_IsNumber(updated)
                ? (int64_t)updated->valuedouble
                : 0;
        const cJSON *utc_offset =
            cJSON_GetObjectItemCaseSensitive(
                launch,
                "currentUTCOffsetSeconds");
        next.launch_current_utc_offset_s =
            cJSON_IsNumber(utc_offset)
                ? utc_offset->valueint
                : 0;
        const cJSON *weather_probability =
            cJSON_GetObjectItemCaseSensitive(
                launch,
                "weatherProbabilityPercent");
        next.launch_weather_probability_percent =
            cJSON_IsNumber(weather_probability)
                ? weather_probability->valueint
                : -1;

        const int agency_index =
            launch_agency_index_for_text(
                next.launch_agency);
        if (agency_index >= 0) {
            copy_legacy_launch_to_option(
                &next,
                &next.launch_options[agency_index]);
            next.launch_option_mask |=
                (uint8_t)(1U << agency_index);
        }
    }

    const cJSON *launches =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "launches");
    if (cJSON_IsArray(launches)) {
        memset(
            next.launch_options,
            0,
            sizeof(next.launch_options));
        next.launch_option_mask = 0;
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, launches) {
            app_remote_launch_t parsed = {0};
            parse_launch_json(item, &parsed);
            const int agency_index =
                launch_agency_index_for_text(
                    parsed.agency);
            if (agency_index < 0 || !parsed.valid) {
                continue;
            }
            next.launch_options[agency_index] = parsed;
            next.launch_option_mask |=
                (uint8_t)(1U << agency_index);
        }
    }

    *out = next;
    return true;
}

static esp_err_t apply_state_root(cJSON *root)
{
    const cJSON *agencies = cJSON_GetObjectItemCaseSensitive(root, "launchAgencies");
    if (agencies) {
        app_config_t cfg; app_config_get(&cfg);
        if (!app_launch_agencies(agencies, &cfg.launch_agency_mask)) return ESP_ERR_INVALID_ARG;
        esp_err_t error = app_config_save(&cfg);
        if (error != ESP_OK) return error;
        app_launches_request_refresh();
    }
    app_remote_status_t next;
    if (cJSON_HasObjectItem(root, "trackerQueue")) {
        if (!app_tracker_install(root)) return ESP_ERR_INVALID_ARG;
        next = read_status();
        const cJSON *style = cJSON_GetObjectItemCaseSensitive(root, "style");
        if (cJSON_IsString(style)) next.style = parse_style(style->valuestring);
        next.last_push_unix = (int)time(NULL);
        next.state_pushes++;
        next.revision++;
        write_status(&next);
        return ESP_OK;
    }
    next = read_status();
    if (!app_remote_decode_state(root, &next)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "trackingActive"))) {
        if (!app_tracker_clear()) return ESP_FAIL;
    }
    next.revision++;
    next.state_pushes++;
    next.last_push_unix = (int)time(NULL);
    next.last_error[0] = '\0';
    persist_tracked_state(&next);
    write_status(&next);

    ESP_LOGI(TAG, "State pushed: style=%s progress=%.2f", app_remote_style_text(next.style), next.route_progress);
    return ESP_OK;
}

static esp_err_t state_post_handler(httpd_req_t *req)
{
    char *body = read_request_body(req, 20 * 1024);
    if (body == NULL) {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "state JSON must be 1..20480 bytes");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }
    const esp_err_t err = apply_state_root(root);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid state JSON");
        return ESP_FAIL;
    }
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t receive_map(httpd_req_t *req,
                             map_store_t *store,
                             const char *label,
                             bool radar_map,
                             int radar_range_index,
                             int route_view_index,
                             int route_slot,
                             const char *location_key)
{
    (void)store;
    if (req->content_len != APP_REMOTE_MAP_BYTES) {
        char error[96];
        snprintf(error, sizeof(error), "expected %u bytes RGB565", (unsigned)APP_REMOTE_MAP_BYTES);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
        return ESP_FAIL;
    }

    int received = 0;

    char path[96];
    if (radar_map) {
        radar_map_path_for_key(location_key, radar_range_index, path, sizeof(path));
    } else {
        route_map_path_for_slot_view(route_slot, route_view_index, path, sizeof(path));
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        char error[96];
        snprintf(error, sizeof(error), "could not open map file: errno=%d", errno);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error);
        return ESP_FAIL;
    }

    uint8_t scratch[2048];
    while (received < req->content_len) {
        const int remaining = req->content_len - received;
        const int want = remaining < (int)sizeof(scratch) ? remaining : (int)sizeof(scratch);
        const int chunk = httpd_req_recv(req, (char *)scratch, want);
        if (chunk <= 0) {
            if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            fclose(file);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "map receive failed");
            return ESP_FAIL;
        }
        if (fwrite(scratch, 1, chunk, file) != (size_t)chunk) {
            fclose(file);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "map write failed");
            return ESP_FAIL;
        }
        received += chunk;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (fclose(file) != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "map close failed");
        return ESP_FAIL;
    }

    app_remote_status_t next = read_status();
    /* Do not free the decoded map here. The HTTP task can finish an upload
     * while the LVGL task is copying the currently active map into its padded
     * tile buffer. Freeing it from this task created a narrow use-after-free
     * window and intermittent reboots during tracked-flight map pushes. The
     * incremented revision below makes the LVGL task reload the new closed
     * file safely on its next render; normal screen changes still reclaim
     * inactive route/radar stores. */
    if (radar_map) {
        if (radar_range_index < 0 || radar_range_index >= APP_REMOTE_RADAR_MAP_RANGE_COUNT) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "radar range must be one of 10,20,50");
            return ESP_FAIL;
        }
        char selected_key[MAP_LOCATION_KEY_MAX];
        selected_location_key(selected_key, sizeof(selected_key));
        next.radar_map_valid_mask = radar_map_mask_for_location_key(selected_key);
        next.radar_map_valid = next.radar_map_valid_mask != 0;
        next.radar_map_bytes = next.radar_map_valid ? APP_REMOTE_MAP_BYTES : 0;
        next.radar_map_revision++;
        next.radar_map_pushes++;
    } else if (route_slot == next.tracked_flight_slot) {
        next.route_map_valid_mask = route_map_valid_mask_for_slot(route_slot);
        next.map_valid = next.route_map_valid_mask != 0;
        next.map_bytes = next.map_valid ? APP_REMOTE_MAP_BYTES : 0;
        next.map_revision++;
        next.map_pushes++;
        if (next.route_map_valid_mask == ((1UL << APP_REMOTE_ROUTE_MAP_VIEW_COUNT) - 1UL)) {
            unlink(MAP_STORAGE_BASE_PATH "/route.rgb");
        }
    }
    next.revision++;
    next.last_push_unix = (int)time(NULL);
    next.last_error[0] = '\0';
    write_status(&next);

    ESP_LOGI(TAG, "%s map pushed to flash: %d bytes rev=%lu path=%s",
             label,
             received,
             (unsigned long)(radar_map ? next.radar_map_revision : next.map_revision),
             path);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t map_post_handler(httpd_req_t *req)
{
    char query[64];
    char prepare_value[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(
            query,
            "prepare",
            prepare_value,
            sizeof(prepare_value)) == ESP_OK &&
        (strcmp(prepare_value, "1") == 0 ||
         strcasecmp(prepare_value, "true") == 0)) {
        prepare_tracked_flight_storage(0);
        return send_json(req, "{\"ok\":true,\"prepared\":true}");
    }

    const int view_index = route_view_index_from_request(req);
    const int slot = route_slot_from_request(req);
    if (view_index < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "route view must be globe or frame");
        return ESP_FAIL;
    }
    if (slot < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "flight slot must be 0..3");
        return ESP_FAIL;
    }
    return receive_map(req,
                       &route_map_store,
                       view_index == 1 ? "route frame" : "route globe",
                       false,
                       -1,
                       view_index,
                       slot,
                       NULL);
}

static esp_err_t flag_post_handler(httpd_req_t *req)
{
    const int endpoint_index = route_endpoint_index_from_request(req);
    const int slot = route_slot_from_request(req);
    if (endpoint_index < 0 || endpoint_index > 1) {
        httpd_resp_send_err(req,
                            HTTPD_400_BAD_REQUEST,
                            "flag endpoint must be origin or destination");
        return ESP_FAIL;
    }
    if (slot < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "flight slot must be 0..3");
        return ESP_FAIL;
    }
    if (req->content_len != APP_REMOTE_ROUTE_FLAG_BYTES) {
        char error[96];
        snprintf(error,
                 sizeof(error),
                 "expected %u bytes RGB565 flag",
                 (unsigned)APP_REMOTE_ROUTE_FLAG_BYTES);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
        return ESP_FAIL;
    }

    char path[96];
    route_flag_path_for_slot_endpoint(slot, endpoint_index, path, sizeof(path));
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        httpd_resp_send_err(req,
                            HTTPD_500_INTERNAL_SERVER_ERROR,
                            "could not open flag file");
        return ESP_FAIL;
    }

    uint8_t scratch[512];
    int received = 0;
    bool write_ok = true;
    while (received < req->content_len) {
        const int remaining = req->content_len - received;
        const int wanted =
            remaining < (int)sizeof(scratch) ? remaining : (int)sizeof(scratch);
        const int chunk = httpd_req_recv(req, (char *)scratch, wanted);
        if (chunk <= 0) {
            if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            write_ok = false;
            break;
        }
        if (fwrite(scratch, 1, chunk, file) != (size_t)chunk) {
            write_ok = false;
            break;
        }
        received += chunk;
    }
    if (fclose(file) != 0) {
        write_ok = false;
    }
    if (!write_ok || received != APP_REMOTE_ROUTE_FLAG_BYTES) {
        unlink(path);
        httpd_resp_send_err(req,
                            HTTPD_500_INTERNAL_SERVER_ERROR,
                            "flag receive failed");
        return ESP_FAIL;
    }

    app_remote_status_t next = read_status();
    if (slot == next.tracked_flight_slot && route_flag_buffers[endpoint_index] != NULL) {
        heap_caps_free(route_flag_buffers[endpoint_index]);
        route_flag_buffers[endpoint_index] = NULL;
    }
    if (slot == next.tracked_flight_slot) {
        route_flag_loaded_revision[endpoint_index] = UINT32_MAX;
        next.route_flag_valid_mask = route_flag_valid_mask_for_slot(slot);
        next.route_flag_revision++;
    }
    next.revision++;
    next.last_push_unix = (int)time(NULL);
    write_status(&next);
    ESP_LOGI(TAG,
             "Route %s flag saved",
             endpoint_index == 1 ? "destination" : "origin");
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t radar_map_post_handler(httpd_req_t *req)
{
    const int range_index = radar_range_index_from_request(req);
    if (range_index < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "radar range must be one of 10,20,50");
        return ESP_FAIL;
    }

    char location_key[MAP_LOCATION_KEY_MAX];
    radar_location_key_from_request(req, location_key, sizeof(location_key));

    char label[40];
    snprintf(label, sizeof(label), "radar %s %.0fmi", location_key, radar_map_ranges[range_index]);
    return receive_map(req,
                       &radar_map_stores[range_index],
                       label,
                       true,
                       range_index,
                       -1,
                       -1,
                       location_key);
}

static esp_err_t weather_city_icon_post_handler(httpd_req_t *req)
{
    if (req->content_len != APP_REMOTE_WEATHER_CITY_ICON_BYTES) {
        char error[96];
        snprintf(
            error,
            sizeof(error),
            "expected %u bytes RGB565A8 icon",
            (unsigned)APP_REMOTE_WEATHER_CITY_ICON_BYTES);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
        return ESP_FAIL;
    }

    char location_key[MAP_LOCATION_KEY_MAX];
    radar_location_key_from_request(
        req,
        location_key,
        sizeof(location_key));
    mkdir(MAP_STORAGE_BASE_PATH "/cities", 0775);
    char path[96];
    weather_city_icon_path_for_key(
        location_key,
        path,
        sizeof(path));
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "could not open city icon file");
        return ESP_FAIL;
    }

    uint8_t scratch[1024];
    int received = 0;
    bool write_ok = true;
    while (received < req->content_len) {
        const int remaining = req->content_len - received;
        const int wanted =
            remaining < (int)sizeof(scratch)
                ? remaining
                : (int)sizeof(scratch);
        const int chunk =
            httpd_req_recv(req, (char *)scratch, wanted);
        if (chunk <= 0) {
            if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            write_ok = false;
            break;
        }
        if (fwrite(scratch, 1, chunk, file) !=
            (size_t)chunk) {
            write_ok = false;
            break;
        }
        received += chunk;
    }
    if (fclose(file) != 0) {
        write_ok = false;
    }
    if (!write_ok ||
        received != APP_REMOTE_WEATHER_CITY_ICON_BYTES) {
        unlink(path);
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "city icon receive failed");
        return ESP_FAIL;
    }

    weather_city_icon_key[0] = '\0';
    weather_city_icon_revision++;
    refresh_weather_city_icon_valid_mask();
    app_remote_status_t next = read_status();
    next.revision++;
    next.last_push_unix = (int)time(NULL);
    write_status(&next);
    ESP_LOGI(
        TAG,
        "Weather city icon saved: %s (%d bytes)",
        location_key,
        received);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t space_asset_post_handler(httpd_req_t *req)
{
    char query[64];
    char name[24];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "space asset name is required");
        return ESP_FAIL;
    }
    space_asset_t *asset = space_asset_for_name(name);
    if (asset == NULL) {
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "unknown space asset");
        return ESP_FAIL;
    }
    const size_t expected = space_asset_bytes(asset);
    if ((size_t)req->content_len != expected) {
        char error[96];
        snprintf(
            error,
            sizeof(error),
            "expected %u bytes for %s",
            (unsigned)expected,
            asset->name);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
        return ESP_FAIL;
    }

    const int asset_index = (int)(asset - space_assets);
    if (space_asset_mutex != NULL) {
        xSemaphoreTake(space_asset_mutex, portMAX_DELAY);
    }

    mkdir(MAP_STORAGE_BASE_PATH "/space", 0775);
    char path[96];
    space_asset_path(asset, path, sizeof(path));
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        if (space_asset_mutex != NULL) {
            xSemaphoreGive(space_asset_mutex);
        }
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "could not open space asset file");
        return ESP_FAIL;
    }

    uint8_t scratch[1024];
    int received = 0;
    bool write_ok = true;
    uint32_t hash = 2166136261UL;
    while (received < req->content_len) {
        const int remaining = req->content_len - received;
        const int wanted =
            remaining < (int)sizeof(scratch)
                ? remaining
                : (int)sizeof(scratch);
        const int chunk =
            httpd_req_recv(req, (char *)scratch, wanted);
        if (chunk <= 0) {
            if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            write_ok = false;
            break;
        }
        if (fwrite(scratch, 1, chunk, file) !=
            (size_t)chunk) {
            write_ok = false;
            break;
        }
        hash = fnv1a_update(
            hash,
            scratch,
            (size_t)chunk);
        received += chunk;
    }
    if (fclose(file) != 0) {
        write_ok = false;
    }
    if (!write_ok || (size_t)received != expected) {
        unlink(path);
        space_asset_valid_mask_cache &=
            ~(1UL << asset_index);
        refresh_launch_asset_valid_mask();
        if (space_asset_mutex != NULL) {
            xSemaphoreGive(space_asset_mutex);
        }
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "space asset receive failed");
        return ESP_FAIL;
    }
    if (hash != asset->expected_fnv1a) {
        unlink(path);
        space_asset_valid_mask_cache &=
            ~(1UL << asset_index);
        refresh_launch_asset_valid_mask();
        if (space_asset_mutex != NULL) {
            xSemaphoreGive(space_asset_mutex);
        }
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "space artwork checksum mismatch");
        return ESP_FAIL;
    }

    /*
     * Never free an allocated asset from the HTTP task: an LVGL image may
     * still reference it.  The display thread releases it after dropping the
     * descriptor cache; the newly installed file is used on the next load.
     */
    asset->revision++;
    space_asset_valid_mask_cache |= 1UL << asset_index;
    refresh_launch_asset_valid_mask();
    if (space_asset_mutex != NULL) {
        xSemaphoreGive(space_asset_mutex);
    }
    app_remote_status_t next = read_status();
    next.revision++;
    next.last_push_unix = (int)time(NULL);
    write_status(&next);
    ESP_LOGI(
        TAG,
        "Space asset saved: %s (%d bytes)",
        asset->name,
        received);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t tracker_get_handler(httpd_req_t *req)
{
    cJSON *snapshot = strcmp(req->uri, "/api/launches") == 0 ? app_launches_json() : app_tracker_snapshot();
    char *body = cJSON_PrintUnformatted(snapshot);
    cJSON_Delete(snapshot);
    if (!body) return ESP_ERR_NO_MEM;
    esp_err_t result = send_json(req, body);
    cJSON_free(body);
    return result;
}

static void register_handlers(httpd_handle_t handle)
{
    const httpd_uri_t launches_uri = {
        .uri = "/api/launches", .method = HTTP_GET, .handler = tracker_get_handler,
    };
    httpd_register_uri_handler(handle, &launches_uri);
    const httpd_uri_t tracker_uri = {
        .uri = "/api/tracker", .method = HTTP_GET, .handler = tracker_get_handler,
    };
    httpd_register_uri_handler(handle, &tracker_uri);
    const httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
    };
    const httpd_uri_t aircraft_uri = {
        .uri = "/api/aircraft",
        .method = HTTP_GET,
        .handler = aircraft_get_handler,
    };
    const httpd_uri_t weather_uri = {
        .uri = "/api/weather",
        .method = HTTP_GET,
        .handler = weather_get_handler,
    };
    const httpd_uri_t state_uri = {
        .uri = "/api/state",
        .method = HTTP_POST,
        .handler = state_post_handler,
    };
    const httpd_uri_t map_uri = {
        .uri = "/api/map",
        .method = HTTP_POST,
        .handler = map_post_handler,
    };
    const httpd_uri_t radar_map_uri = {
        .uri = "/api/radar-map",
        .method = HTTP_POST,
        .handler = radar_map_post_handler,
    };
    const httpd_uri_t flag_uri = {
        .uri = "/api/flag",
        .method = HTTP_POST,
        .handler = flag_post_handler,
    };
    const httpd_uri_t weather_city_icon_uri = {
        .uri = "/api/weather-city-icon",
        .method = HTTP_POST,
        .handler = weather_city_icon_post_handler,
    };
    const httpd_uri_t space_asset_uri = {
        .uri = "/api/space-asset",
        .method = HTTP_POST,
        .handler = space_asset_post_handler,
    };
    const httpd_uri_t config_get_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = config_get_handler,
    };
    const httpd_uri_t config_post_uri = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = config_post_handler,
    };
    const httpd_uri_t data_refresh_uri = {
        .uri = "/api/data/refresh",
        .method = HTTP_POST,
        .handler = data_refresh_post_handler,
    };
    httpd_register_uri_handler(handle, &status_uri);
    httpd_register_uri_handler(handle, &aircraft_uri);
    httpd_register_uri_handler(handle, &weather_uri);
    httpd_register_uri_handler(handle, &state_uri);
    httpd_register_uri_handler(handle, &map_uri);
    httpd_register_uri_handler(handle, &radar_map_uri);
    httpd_register_uri_handler(handle, &flag_uri);
    httpd_register_uri_handler(handle, &weather_city_icon_uri);
    httpd_register_uri_handler(handle, &space_asset_uri);
    httpd_register_uri_handler(handle, &config_get_uri);
    httpd_register_uri_handler(handle, &config_post_uri);
    httpd_register_uri_handler(handle, &data_refresh_uri);
}

static void mount_map_storage(void)
{
    const esp_vfs_fat_mount_config_t conf = {
        /* Rendering, networking, cached logos, configuration persistence, and
         * a USB route-package upload can overlap briefly. Six descriptors was
         * enough during normal rendering but could reject the first route-map
         * temporary file during a live sync. The FAT VFS table is inexpensive
         * compared with the map buffers, so reserve enough descriptors for
         * the transfer without changing asset lifetime or rendering logic. */
        .max_files = 16,
        .format_if_mount_failed = true,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .use_one_fat = false,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(MAP_STORAGE_BASE_PATH, "storage", &conf, &map_storage_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not mount FAT map storage: %s", esp_err_to_name(err));
        app_remote_status_t next = read_status();
        snprintf(next.last_error, sizeof(next.last_error), "Map storage mount failed: %s", esp_err_to_name(err));
        write_status(&next);
        return;
    }

    ESP_LOGI(TAG, "Map storage mounted on %s", MAP_STORAGE_BASE_PATH);
    usb_restore_interrupted_files();
    tracked_asset_manifest_load();
    load_persisted_tracked_state();
    app_tracker_load();
    app_launches_load();
    refresh_map_storage_index(false);
    refresh_space_asset_valid_mask();
    refresh_weather_city_icon_valid_mask();
    app_remote_refresh_config_file();
}

static void remote_task(void *arg)
{
    (void)arg;

    mount_map_storage();

    while (true) {
        app_network_status_t net;
        app_network_get_status(&net);
        if (server == NULL && net.wifi_connected) {
            httpd_config_t config = HTTPD_DEFAULT_CONFIG();
            config.server_port = 80;
            config.ctrl_port = 32768;
            /* Full queue validation/publication is deeper than a single
             * screen update. Match USB's stack budget without consuming
             * the internal RAM needed by TLS and display DMA. */
            config.stack_size = 24576;
            config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
            config.max_open_sockets = 2;
            config.backlog_conn = 2;
            config.max_uri_handlers = 14;
            config.recv_wait_timeout = 15;
            config.send_wait_timeout = 15;
            config.lru_purge_enable = true;
            if (httpd_start(&server, &config) == ESP_OK) {
                register_handlers(server);
                ESP_LOGI(TAG, "HTTP push receiver listening at http://%s/api/status", net.ip);
            } else {
                ESP_LOGW(TAG, "Could not start HTTP push receiver");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_remote_start(void)
{
    app_tracker_start();
    app_launches_start();
    if (status_mutex == NULL) {
        status_mutex = xSemaphoreCreateMutex();
    }
    if (space_asset_mutex == NULL) {
        space_asset_mutex = xSemaphoreCreateMutex();
    }
    if (config_mutex == NULL) {
        config_mutex = xSemaphoreCreateMutex();
    }
    /* Config/state JSON assembly has a deeper call chain than streaming map
     * chunks. Keep this stack in PSRAM, but size it for a complete GET/SET so
     * opening USB cannot reset the instrument during synchronization. */
    BaseType_t usb_ok = xTaskCreateWithCaps(
        usb_config_task,
        "usb_config",
        24576,
        NULL,
        8,
        NULL,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (usb_ok != pdPASS) {
        ESP_LOGW(TAG, "Could not start USB config task");
    }
    BaseType_t ok = xTaskCreateWithCaps(remote_task, "airspace_remote", 16384, NULL, 4, NULL,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "Could not start remote task");
    }
}

void app_remote_get_status(app_remote_status_t *out)
{
    if (out == NULL) {
        return;
    }
    read_status_into(out);
}

void app_remote_publish_tracker(const app_remote_status_t *flight, bool owns_updates)
{
    if (!flight || ensure_status_storage() == NULL) return;
    xSemaphoreTake(status_mutex, portMAX_DELAY);
    app_remote_status_t next = *status;
    copy_tracker_fields(&next, flight);
    bool geometry_changed = next.tracked_flight_slot != status->tracked_flight_slot ||
        next.tracked_route_point_count != status->tracked_route_point_count ||
        memcmp(next.tracked_route_points, status->tracked_route_points, sizeof(next.tracked_route_points)) != 0;
    next.tracked_route_geometry_version = status->tracked_route_geometry_version + (geometry_changed ? 1 : 0);
    tracker_owns_display = owns_updates;
    if (memcmp(&next, status, sizeof(next)) != 0) {
        if (geometry_changed) {
            next.route_map_valid_mask = route_map_valid_mask_for_slot(next.tracked_flight_slot);
            next.route_flag_valid_mask = route_flag_valid_mask_for_slot(next.tracked_flight_slot);
            next.map_valid = next.route_map_valid_mask != 0;
            next.map_bytes = next.map_valid ? APP_REMOTE_MAP_BYTES : 0;
            next.map_revision++;
            next.route_flag_revision++;
            route_flag_loaded_revision[0] = UINT32_MAX;
            route_flag_loaded_revision[1] = UINT32_MAX;
        }
        next.revision++;
        *status = next;
    }
    xSemaphoreGive(status_mutex);
}

void app_remote_publish_launches(const app_remote_launch_t launches[3], uint8_t mask)
{
    if (!launches || !ensure_status_storage() || !status_mutex) return;
    xSemaphoreTake(status_mutex, portMAX_DELAY);
    launches_own_display = true;
    if (status->launch_option_mask != mask || memcmp(status->launch_options, launches, sizeof(status->launch_options))) {
        memcpy(status->launch_options, launches, sizeof(status->launch_options));
        status->launch_option_mask = mask;
        int first = -1;
        for (int a = 0; a < 3; a++) if ((mask & (1U << a)) &&
            (first < 0 || launches[a].net_unix < launches[first].net_unix)) first = a;
        if (first >= 0) copy_launch_option_to_legacy(&launches[first], status);
        else status->launch_valid = false;
        status->revision++;
    }
    xSemaphoreGive(status_mutex);
}

bool app_remote_get_launch_status(
    app_remote_launch_agency_t agency,
    app_remote_status_t *out)
{
    if (out == NULL ||
        agency < 0 ||
        agency >= APP_REMOTE_LAUNCH_AGENCY_COUNT) {
        return false;
    }
    read_status_into(out);
    const uint8_t bit =
        (uint8_t)(1U << (unsigned)agency);
    if ((out->launch_option_mask & bit) == 0 ||
        !out->launch_options[agency].valid) {
        return false;
    }
    copy_launch_option_to_legacy(
        &out->launch_options[agency],
        out);
    return true;
}

void app_remote_refresh_map_storage_index(void)
{
    refresh_map_storage_index(true);
}

void app_remote_refresh_config_file(void)
{
    cJSON *root = build_shared_config_json(NULL, false, false);
    if (root == NULL) {
        return;
    }
    if (!write_shared_config_file(root)) {
        ESP_LOGW(TAG, "Could not update %s", SHARED_CONFIG_PATH);
    }
    cJSON_Delete(root);
}

const uint8_t *app_remote_get_map(size_t *len, uint32_t *revision)
{
    return app_remote_get_map_for_view(0, len, revision);
}

const uint8_t *app_remote_get_map_for_view(int view_index, size_t *len, uint32_t *revision)
{
    app_remote_status_t remote = read_status();
    if (view_index < 0 || view_index >= APP_REMOTE_ROUTE_MAP_VIEW_COUNT) {
        view_index = 0;
    }
    if ((remote.route_map_valid_mask & (1UL << view_index)) == 0) {
        if (len != NULL) {
            *len = 0;
        }
        if (revision != NULL) {
            *revision = remote.map_revision;
        }
        return NULL;
    }

    char path[96];
    route_map_path_for_view(view_index, path, sizeof(path));
    char key[16];
    snprintf(key,
             sizeof(key),
             "S%u%s",
             (unsigned)remote.tracked_flight_slot,
             view_index == 1 ? "FRAME" : "GLOBE");
    release_radar_map_buffers_except(-1);
    if (!load_map_file_into_store(&route_map_store, path, key, remote.map_revision, "route")) {
        if (len != NULL) {
            *len = 0;
        }
        if (revision != NULL) {
            *revision = remote.map_revision;
        }
        return NULL;
    }

    if (len != NULL) {
        *len = remote.map_bytes;
    }
    if (revision != NULL) {
        *revision = remote.map_revision;
    }
    return route_map_store.buffers[route_map_store.active_index];
}

const uint8_t *app_remote_get_route_flag(int endpoint_index,
                                         size_t *len,
                                         uint32_t *revision)
{
    if (endpoint_index < 0 || endpoint_index > 1) {
        if (len != NULL) {
            *len = 0;
        }
        return NULL;
    }
    app_remote_status_t remote = read_status();
    if ((remote.route_flag_valid_mask & (1UL << endpoint_index)) == 0) {
        if (len != NULL) {
            *len = 0;
        }
        if (revision != NULL) {
            *revision = remote.route_flag_revision;
        }
        return NULL;
    }

    if (route_flag_buffers[endpoint_index] == NULL) {
        route_flag_buffers[endpoint_index] = (uint8_t *)heap_caps_malloc(
            APP_REMOTE_ROUTE_FLAG_BYTES,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (route_flag_buffers[endpoint_index] == NULL) {
            if (len != NULL) {
                *len = 0;
            }
            return NULL;
        }
    }
    if (route_flag_loaded_revision[endpoint_index] !=
        remote.route_flag_revision) {
        char path[96];
        route_flag_path_for_endpoint(endpoint_index, path, sizeof(path));
        FILE *file = fopen(path, "rb");
        if (file == NULL ||
            fread(route_flag_buffers[endpoint_index],
                  1,
                  APP_REMOTE_ROUTE_FLAG_BYTES,
                  file) != APP_REMOTE_ROUTE_FLAG_BYTES) {
            if (file != NULL) {
                fclose(file);
            }
            if (len != NULL) {
                *len = 0;
            }
            return NULL;
        }
        fclose(file);
        route_flag_loaded_revision[endpoint_index] =
            remote.route_flag_revision;
    }

    if (len != NULL) {
        *len = APP_REMOTE_ROUTE_FLAG_BYTES;
    }
    if (revision != NULL) {
        *revision = remote.route_flag_revision;
    }
    return route_flag_buffers[endpoint_index];
}

const uint8_t *app_remote_get_weather_city_icon(
    const char *location_key,
    size_t *len,
    uint32_t *revision)
{
    if (len != NULL) {
        *len = 0;
    }
    char key[MAP_LOCATION_KEY_MAX];
    location_key_for_storage(location_key, key, sizeof(key));
    char path[96];
    weather_city_icon_path_for_key(key, path, sizeof(path));
    if (!file_has_exact_size(
            path,
            APP_REMOTE_WEATHER_CITY_ICON_BYTES)) {
        return NULL;
    }
    if (weather_city_icon_buffer == NULL) {
        weather_city_icon_buffer =
            (uint8_t *)heap_caps_malloc(
                APP_REMOTE_WEATHER_CITY_ICON_BYTES,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (weather_city_icon_buffer == NULL) {
            return NULL;
        }
    }
    if (strcasecmp(weather_city_icon_key, key) != 0) {
        FILE *file = fopen(path, "rb");
        if (file == NULL ||
            fread(
                weather_city_icon_buffer,
                1,
                APP_REMOTE_WEATHER_CITY_ICON_BYTES,
                file) != APP_REMOTE_WEATHER_CITY_ICON_BYTES) {
            if (file != NULL) {
                fclose(file);
            }
            return NULL;
        }
        fclose(file);
        copy_string(
            weather_city_icon_key,
            sizeof(weather_city_icon_key),
            key);
    }
    if (len != NULL) {
        *len = APP_REMOTE_WEATHER_CITY_ICON_BYTES;
    }
    if (revision != NULL) {
        *revision = weather_city_icon_revision;
    }
    return weather_city_icon_buffer;
}

const uint8_t *app_remote_get_space_asset(
    const char *name,
    size_t *len,
    uint16_t *width,
    uint16_t *height,
    bool *has_alpha,
    uint32_t *revision)
{
    if (len != NULL) {
        *len = 0;
    }
    space_asset_t *asset = space_asset_for_name(name);
    if (asset == NULL) {
        return NULL;
    }
    const int asset_index = (int)(asset - space_assets);
    const size_t expected = space_asset_bytes(asset);
    char path[96];
    space_asset_path(asset, path, sizeof(path));
    if (space_asset_mutex != NULL) {
        xSemaphoreTake(space_asset_mutex, portMAX_DELAY);
    }
    if ((space_asset_valid_mask_cache &
         (1UL << asset_index)) == 0) {
        space_asset_load_failures++;
        ESP_LOGW(
            TAG,
            "Space artwork unavailable or corrupt: %s",
            asset->name);
        if (space_asset_mutex != NULL) {
            xSemaphoreGive(space_asset_mutex);
        }
        return NULL;
    }
    if (asset->buffer == NULL) {
        asset->buffer =
            (uint8_t *)heap_caps_malloc(
                expected,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (asset->buffer == NULL) {
            space_asset_load_failures++;
            ESP_LOGW(
                TAG,
                "No PSRAM for space asset %s: need=%u free=%u largest=%u",
                asset->name,
                (unsigned)expected,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
            if (space_asset_mutex != NULL) {
                xSemaphoreGive(space_asset_mutex);
            }
            return NULL;
        }
        FILE *file = fopen(path, "rb");
        if (file == NULL ||
            fread(asset->buffer, 1, expected, file) != expected) {
            if (file != NULL) {
                fclose(file);
            }
            heap_caps_free(asset->buffer);
            asset->buffer = NULL;
            space_asset_valid_mask_cache &=
                ~(1UL << asset_index);
            refresh_launch_asset_valid_mask();
            space_asset_load_failures++;
            ESP_LOGW(
                TAG,
                "Could not read space artwork: %s",
                asset->name);
            if (space_asset_mutex != NULL) {
                xSemaphoreGive(space_asset_mutex);
            }
            return NULL;
        }
        fclose(file);
        const uint32_t hash = fnv1a_update(
            2166136261UL,
            asset->buffer,
            expected);
        if (hash != asset->expected_fnv1a) {
            heap_caps_free(asset->buffer);
            asset->buffer = NULL;
            space_asset_valid_mask_cache &=
                ~(1UL << asset_index);
            refresh_launch_asset_valid_mask();
            space_asset_load_failures++;
            ESP_LOGW(
                TAG,
                "Space artwork checksum mismatch: %s",
                asset->name);
            if (space_asset_mutex != NULL) {
                xSemaphoreGive(space_asset_mutex);
            }
            return NULL;
        }
        ESP_LOGI(
            TAG,
            "Loaded space artwork %s (%u KB); free=%uKB largest=%uKB",
            asset->name,
            (unsigned)(expected / 1024U),
            (unsigned)(
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM) /
                1024U),
            (unsigned)(
                heap_caps_get_largest_free_block(
                    MALLOC_CAP_SPIRAM) /
                1024U));
    }
    if (len != NULL) {
        *len = expected;
    }
    if (width != NULL) {
        *width = asset->width;
    }
    if (height != NULL) {
        *height = asset->height;
    }
    if (has_alpha != NULL) {
        *has_alpha = asset->has_alpha;
    }
    if (revision != NULL) {
        *revision = asset->revision;
    }
    const uint8_t *result = asset->buffer;
    if (space_asset_mutex != NULL) {
        xSemaphoreGive(space_asset_mutex);
    }
    return result;
}

void app_remote_release_space_assets_except(
    const char *const *keep_names,
    size_t keep_count)
{
    size_t released = 0;
    if (space_asset_mutex != NULL) {
        xSemaphoreTake(space_asset_mutex, portMAX_DELAY);
    }
    for (int index = 0; index < SPACE_ASSET_COUNT; index++) {
        space_asset_t *asset = &space_assets[index];
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
        if (keep || asset->buffer == NULL) {
            continue;
        }
        released += space_asset_bytes(asset);
        heap_caps_free(asset->buffer);
        asset->buffer = NULL;
    }
    if (space_asset_mutex != NULL) {
        xSemaphoreGive(space_asset_mutex);
    }
    if (released > 0) {
        ESP_LOGI(
            TAG,
            "Released %u KB of inactive space artwork",
            (unsigned)(released / 1024U));
    }
}

const uint8_t *app_remote_get_radar_map(size_t *len, uint32_t *revision)
{
    return app_remote_get_radar_map_for_range(20.0f, len, revision, NULL);
}

const uint8_t *app_remote_get_radar_map_for_range(float miles, size_t *len, uint32_t *revision, int *range_index)
{
    char key[MAP_LOCATION_KEY_MAX];
    selected_location_key(key, sizeof(key));
    return app_remote_get_radar_map_for_location(
        key,
        miles,
        len,
        revision,
        range_index);
}

const uint8_t *app_remote_get_radar_map_for_location(
    const char *location_key,
    float miles,
    size_t *len,
    uint32_t *revision,
    int *range_index)
{
    app_remote_status_t remote = read_status();
    const int index = app_remote_radar_range_index_for_miles(miles);
    if (range_index != NULL) {
        *range_index = index;
    }

    map_store_t *store = &radar_map_stores[index];
    char key[MAP_LOCATION_KEY_MAX];
    location_key_for_storage(location_key, key, sizeof(key));
    char path[96];
    radar_map_path_for_key(key, index, path, sizeof(path));
    if (!file_has_exact_size(path, APP_REMOTE_MAP_BYTES)) {
        if (len != NULL) {
            *len = 0;
        }
        if (revision != NULL) {
            *revision = remote.radar_map_revision;
        }
        return NULL;
    }

    release_map_store_buffer(&route_map_store);
    release_radar_map_buffers_except(index);
    if (!load_map_file_into_store(store, path, key, remote.radar_map_revision, "radar")) {
        if (len != NULL) {
            *len = 0;
        }
        if (revision != NULL) {
            *revision = remote.radar_map_revision;
        }
        return NULL;
    }

    if (len != NULL) {
        *len = APP_REMOTE_MAP_BYTES;
    }
    if (revision != NULL) {
        *revision = remote.radar_map_revision;
    }
    return store->buffers[store->active_index];
}

const char *app_remote_style_text(app_remote_style_t style)
{
    switch (style) {
    case APP_REMOTE_STYLE_XDR_RADAR:
        return "xdrRadar";
    case APP_REMOTE_STYLE_CARTOGRAPHIC_GLASS:
        return "cartographicGlass";
    case APP_REMOTE_STYLE_GMT_CHRONOGRAPH:
        return "gmtChronograph";
    case APP_REMOTE_STYLE_WEATHER:
        return "weather";
    case APP_REMOTE_STYLE_WEATHER_RADAR:
        return "weatherRadar";
    case APP_REMOTE_STYLE_SPACE:
        return "space";
    case APP_REMOTE_STYLE_SPACE_ISS:
        return "spaceIss";
    case APP_REMOTE_STYLE_SPACE_MOON:
        return "spaceMoon";
    case APP_REMOTE_STYLE_LAUNCH:
        return "launch";
    case APP_REMOTE_STYLE_LAUNCH_MAP:
        return "launchMap";
    case APP_REMOTE_STYLE_LAUNCH_QR:
        return "launchQR";
    default:
        return "xdrRadar";
    }
}
