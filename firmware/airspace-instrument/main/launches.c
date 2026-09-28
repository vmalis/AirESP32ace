// AirESP32ace
// Created by VM, August 2026
#include "launches.h"
#include "app_config.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define LAUNCH_PATH "/spiffs/launches.bin"
#define LAUNCH_TEMP "/spiffs/launches.tmp"
#define LAUNCH_BACKUP "/spiffs/launches.bak"
#define LAUNCH_MAGIC 0x4c434831U
#define BODY_LIMIT (96 * 1024)
typedef struct {
    uint32_t magic, bytes;
    app_launch_entry_t entries[3];
    int64_t attempts[APP_LAUNCH_REQUESTS_PER_HOUR];
    int64_t blocked_until;
    uint32_t checksum;
} launch_store_t;
static launch_store_t *store;
static app_remote_launch_t *presentation;
static SemaphoreHandle_t mutex;
static bool ready, refresh_requested;

static uint32_t checksum(const launch_store_t *s)
{
    uint32_t h = 2166136261U;
    const uint8_t *p = (const uint8_t *)s;
    for (size_t i = 0; i < offsetof(launch_store_t, checksum); i++) h = (h ^ p[i]) * 16777619U;
    return h;
}
static bool save(void)
{
    store->magic = LAUNCH_MAGIC; store->bytes = sizeof(*store); store->checksum = checksum(store);
    FILE *f = fopen(LAUNCH_TEMP, "wb");
    if (!f) return false;
    bool ok = fwrite(store, 1, sizeof(*store), f) == sizeof(*store);
    ok = fclose(f) == 0 && ok;
    if (!ok) { unlink(LAUNCH_TEMP); return false; }
    unlink(LAUNCH_BACKUP);
    rename(LAUNCH_PATH, LAUNCH_BACKUP);
    if (rename(LAUNCH_TEMP, LAUNCH_PATH)) { rename(LAUNCH_BACKUP, LAUNCH_PATH); return false; }
    unlink(LAUNCH_BACKUP);
    return true;
}
static bool load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = fread(store, 1, sizeof(*store), f) == sizeof(*store);
    fclose(f);
    return ok && store->magic == LAUNCH_MAGIC && store->bytes == sizeof(*store) && store->checksum == checksum(store);
}
/* Called under our mutex; remote publication takes only the remote mutex.
 * No UI path calls back into this module while holding that mutex. */
static void publish(uint8_t enabled, int64_t now)
{
    uint8_t mask = 0;
    for (unsigned a = 0; a < 3; a++) {
        app_launch_entry_t *e = &store->entries[a];
        presentation[a] = e->launch;
        presentation[a].current_utc_offset_s = app_launch_utc_offset(e->launch.site, now);
        if (!(enabled & (1U << a)) || app_launch_expired(e, now)) presentation[a].valid = false;
        if (presentation[a].valid) {
            mask |= 1U << a;
            if (app_launch_stale(e, now)) {
                snprintf(presentation[a].status, sizeof(presentation[a].status), "STALE - awaiting update");
                snprintf(presentation[a].status_abbrev, sizeof(presentation[a].status_abbrev), "STALE");
                snprintf(presentation[a].precision, sizeof(presentation[a].precision), "Unknown");
            }
        }
    }
    app_remote_publish_launches(presentation, mask);
}
void app_launches_start(void)
{
    mutex = xSemaphoreCreateMutex();
    store = heap_caps_calloc(1, sizeof(*store), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    presentation = heap_caps_calloc(3, sizeof(*presentation), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void app_launches_load(void)
{
    if (!store || !presentation || !mutex) return;
    app_config_t cfg; app_config_get(&cfg);
    app_remote_status_t *legacy = heap_caps_calloc(1, sizeof(*legacy), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (legacy) app_remote_get_status(legacy);
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!load(LAUNCH_PATH) && !load(LAUNCH_BACKUP) && !load(LAUNCH_TEMP)) {
        memset(store, 0, sizeof(*store));
        if (legacy) for (unsigned a = 0; a < 3; a++) {
            if (legacy->launch_option_mask & (1U << a)) store->entries[a].launch = legacy->launch_options[a];
        }
    }
    /* Migrate legacy provider preferences once. The old snapshot is only a
     * seed; checked stays zero, so it cannot masquerade as fresh data. */
    if (!cfg.launch_agency_mask) {
        cfg.launch_agency_mask = legacy && legacy->launch_option_mask ? legacy->launch_option_mask : 7;
        app_config_save(&cfg);
    }
    heap_caps_free(legacy);
    for (unsigned a = 0; a < 3; a++) store->entries[a].next_check = 0;
    ready = true;
    publish(cfg.launch_agency_mask, time(NULL));
    xSemaphoreGive(mutex);
}
void app_launches_request_refresh(void)
{
    if (!mutex) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    refresh_requested = true;
    xSemaphoreGive(mutex);
}
static esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        !strcasecmp(event->header_key, "Retry-After")) {
        char *end;
        long seconds = strtol(event->header_value, &end, 10);
        if (*end == 0 && seconds > 0 && seconds <= 86400) *(int *)event->user_data = seconds;
    }
    return ESP_OK;
}
static cJSON *request(const char *url, int *code, int *retry)
{
    *code = 0; *retry = 3600;
    esp_http_client_config_t config = {.url = url, .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach, .disable_auto_redirect = true,
        .buffer_size = 2048, .buffer_size_tx = 1024, .event_handler = http_event, .user_data = retry};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return NULL;
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "User-Agent", "AirESP32ace/0.1.2 personal-display");
    char *body = heap_caps_malloc(BODY_LIMIT + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    cJSON *root = NULL;
    int used = 0, n = 0;
    bool complete = false;
    if (body && esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        *code = esp_http_client_get_status_code(client);
        if (*code == 200) {
            while (used < BODY_LIMIT && (n = esp_http_client_read(client, body + used, BODY_LIMIT - used)) > 0) used += n;
            body[used] = 0;
            complete = n >= 0 && esp_http_client_is_complete_data_received(client);
        }
    }
    esp_http_client_close(client); esp_http_client_cleanup(client);
    /* Release TLS allocations before constructing the JSON tree. */
    if (complete) root = cJSON_Parse(body);
    if (!root) ESP_LOGW("airspace_launch", "Response rejected: HTTP=%d bytes=%d complete=%d", *code, used, complete);
    heap_caps_free(body);
    return root;
}
void app_launches_tick(bool online)
{
    if (!store || !presentation || !mutex) return;
    app_config_t cfg; app_config_get(&cfg);
    int64_t now = time(NULL);
    if (now < 1700000000) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!ready) { xSemaphoreGive(mutex); return; }
    if (refresh_requested) {
        for (unsigned a = 0; a < 3; a++) {
            app_launch_entry_t *e = &store->entries[a];
            if (!e->failures && e->next_check > now) e->next_check = now;
        }
        refresh_requested = false;
    }
    publish(cfg.launch_agency_mask, now); /* Also marks stale data while offline. */
    if (!online || now < store->blocked_until || now < app_launch_budget_due(store->attempts, now)) {
        xSemaphoreGive(mutex); return;
    }
    int selected = -1;
    /* Start with SpaceX when no cached NET exists, otherwise earliest due. */
    const unsigned order[] = {2, 0, 1};
    for (unsigned i = 0; i < 3; i++) {
        unsigned a = order[i];
        if (!(cfg.launch_agency_mask & (1U << a)) || now < store->entries[a].next_check) continue;
        if (selected < 0 || store->entries[a].next_check < store->entries[selected].next_check) selected = a;
    }
    if (selected < 0) { xSemaphoreGive(mutex); return; }
    app_launch_entry_t *work = heap_caps_malloc(sizeof(*work), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!work) { xSemaphoreGive(mutex); return; }
    *work = store->entries[selected];
    unsigned oldest = 0;
    for (unsigned i = 1; i < APP_LAUNCH_REQUESTS_PER_HOUR; i++) if (store->attempts[i] < store->attempts[oldest]) oldest = i;
    store->attempts[oldest] = now;
    /* Persist the reservation before HTTP so restarting cannot reset quota. */
    if (!save()) { heap_caps_free(work); xSemaphoreGive(mutex); return; }
    xSemaphoreGive(mutex);

    bool exact = work->launch.valid && !app_launch_expired(work, now) && app_launch_valid_id(work->launch.id);
    char url[384];
    if (exact) snprintf(url, sizeof(url), "https://ll.thespacedevs.com/2.3.0/launches/%s/?format=json&mode=normal", work->launch.id);
    else {
        const char *filters[] = {"search=Artemis", "search=Soyuz%20MS", "rocket__configuration__full_name__icontains=Starship"};
        time_t since = now - 86400; struct tm date; gmtime_r(&since, &date);
        char utc[32]; strftime(utc, sizeof(utc), "%Y-%m-%dT%H:%M:%SZ", &date);
        /* Recent + upcoming, not just upcoming: don't lose today's mission
         * when the provider moves it out of the upcoming list at liftoff. */
        snprintf(url, sizeof(url), "https://ll.thespacedevs.com/2.3.0/launches/?format=json&mode=normal&limit=3&ordering=net&net__gte=%s&%s", utc, filters[selected]);
    }
    int code, retry;
    cJSON *root = request(url, &code, &retry);
    now = time(NULL);
    bool accepted = false;
    if (exact) accepted = app_launch_accept(work, root, selected, now, true);
    else {
        const cJSON *item;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "results")) {
            if (work->completed && !strcmp(work->launch.id, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "id")) ?: "")) continue;
            if (app_launch_accept(work, item, selected, now, false)) { accepted = true; break; }
        }
    }
    cJSON_Delete(root);
    work->http_status = code;
    if (!accepted) {
        if (work->failures < 6) work->failures++;
        work->next_check = now + app_launch_failure_interval(work->failures);
    }
    ESP_LOGI("airspace_launch", "Program=%d HTTP=%d accepted=%d retry=%lld", selected, code, accepted, work->next_check);
    xSemaphoreTake(mutex, portMAX_DELAY);
    store->entries[selected] = *work;
    heap_caps_free(work);
    if (code == 429) store->blocked_until = now + retry;
    save();
    app_config_get(&cfg);
    publish(cfg.launch_agency_mask, now);
    xSemaphoreGive(mutex);
}
cJSON *app_launches_json(void)
{
    cJSON *root = cJSON_CreateObject(), *items = cJSON_AddArrayToObject(root, "launches");
    cJSON_AddBoolToObject(root, "deviceLaunchesSupported", true);
    if (!mutex || !store) return root;
    app_config_t cfg; app_config_get(&cfg);
    int64_t now = time(NULL);
    xSemaphoreTake(mutex, portMAX_DELAY);
    cJSON_AddNumberToObject(root, "enabledMask", cfg.launch_agency_mask);
    cJSON_AddNumberToObject(root, "retryAfterUnix", store->blocked_until);
    cJSON_AddNumberToObject(root, "nextRequestUnix", app_launch_budget_due(store->attempts, now));
    for (unsigned a = 0; a < 3; a++) {
        const app_launch_entry_t *e = &store->entries[a];
        if (!(cfg.launch_agency_mask & (1U << a))) continue;
        const app_remote_launch_t *l = &e->launch;
        cJSON *j = cJSON_CreateObject(); cJSON_AddItemToArray(items, j);
        cJSON_AddBoolToObject(j, "available", l->valid && !app_launch_expired(e, now));
        cJSON_AddBoolToObject(j, "stale", app_launch_stale(e, now));
        cJSON_AddNumberToObject(j, "checkedUnix", e->checked);
        cJSON_AddNumberToObject(j, "httpStatus", e->http_status);
        cJSON_AddNumberToObject(j, "nextCheckUnix", e->next_check);
        cJSON_AddNumberToObject(j, "failureCount", e->failures);
        const char *agencies[] = {"nasa", "roscosmos", "spacex"};
        cJSON_AddStringToObject(j, "agency", agencies[a]);
#define FIELD(key, field) cJSON_AddStringToObject(j, key, l->field)
        FIELD("id", id); FIELD("missionName", mission); FIELD("fullName", full_name);
        FIELD("vehicle", vehicle); FIELD("site", site); FIELD("padName", pad); FIELD("locationName", location);
        FIELD("precisionName", precision); FIELD("precisionAbbreviation", precision_abbrev);
        FIELD("statusName", status); FIELD("statusAbbreviation", status_abbrev); FIELD("watchURL", watch_url);
#undef FIELD
        cJSON_AddNumberToObject(j, "netUnix", l->net_unix);
        cJSON_AddNumberToObject(j, "updatedUnix", l->updated_unix);
        cJSON_AddNumberToObject(j, "weatherProbabilityPercent", l->weather_probability_percent);
    }
    xSemaphoreGive(mutex);
    return root;
}
