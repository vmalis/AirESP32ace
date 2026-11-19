// AirESP32ace
// Created by VM, August 2026

#include "tracker.h"
#include "tracker_storage.h"
#include "app_config.h"
#include "data.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define QUEUE_PATH "/spiffs/tracker.bin"
#define QUEUE_TEMP "/spiffs/tracker.tmp"
#define QUEUE_BACKUP "/spiffs/tracker.bak"
static tracker_store_t *store;
static SemaphoreHandle_t mutex;
static uint32_t generation;
static _Atomic uint32_t asset_generation;
static int64_t last_save;
static int64_t next_tick;
static bool ready, primary_trusted;

static bool save(tracker_store_t *s)
{
    FILE *file = fopen(QUEUE_TEMP, "wb");
    if (!file)
        return false;
    bool ok = app_tracker_store_write(file, s);
    ok = fclose(file) == 0 && ok;
    if (!ok) {
        unlink(QUEUE_TEMP);
        return false;
    }
    /* On recovery the backup can be the *only* valid queue. Never replace
     * it with the damaged primary before the new queue is committed. */
    if (primary_trusted) {
        unlink(QUEUE_BACKUP);
        if (rename(QUEUE_PATH, QUEUE_BACKUP) != 0) return false;
        primary_trusted = false;
    } else {
        unlink(QUEUE_PATH);
    }
    if (rename(QUEUE_TEMP, QUEUE_PATH) != 0) {
        primary_trusted = rename(QUEUE_BACKUP, QUEUE_PATH) == 0;
        return false;
    }
    primary_trusted = true;
    unlink(QUEUE_BACKUP);
    last_save = time(NULL);
    return true;
}
static bool load(const char *path, tracker_store_t *s)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    bool ok = app_tracker_store_read(file, s);
    fclose(file);
    return ok;
}
void app_tracker_start(void)
{
    mutex = xSemaphoreCreateMutex();
    store = heap_caps_calloc(1, sizeof(*store), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (store)
        store->active = -1;
}
static void publish(void)
{
    atomic_store(&asset_generation, store->asset_generation);
    app_remote_status_t empty = {0};
    app_remote_publish_tracker(store->active >= 0 ? &store->flights[store->active].display : &empty,
                               store->count > 0);
}
uint32_t app_tracker_asset_generation(void)
{
    /* Rendering/remote reads must not invert the tracker -> remote lock order. */
    return atomic_load(&asset_generation);
}
unsigned app_tracker_prepared_count(void)
{
    if (!store || !mutex) return 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    unsigned count = store->count;
    xSemaphoreGive(mutex);
    return count;
}
void app_tracker_load(void)
{
    if (!store || !mutex)
        return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    primary_trusted = load(QUEUE_PATH, store);
    bool loaded = primary_trusted || load(QUEUE_BACKUP, store);
    if (!loaded) {
        memset(store, 0, sizeof(*store));
        store->active = -1;
    }
    for (unsigned i = 0; i < store->count; i++) {
        store->flights[i].next_motion = 0;
        app_tracker_reconcile(&store->flights[i], time(NULL));
    }
    ready = true;
    if (loaded) {
        store->active = app_tracker_select(store->flights, store->count, store->active, time(NULL));
        publish();
    }
    xSemaphoreGive(mutex);
}
bool app_tracker_owns_updates(void)
{
    if (!store || !mutex)
        return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool owned = store->count > 0;
    xSemaphoreGive(mutex);
    return owned;
}
bool app_tracker_install(const cJSON *root)
{
    if (!store || !ready)
        return false;
    const cJSON *queue = cJSON_GetObjectItemCaseSensitive(root, "trackerQueue");
    if (!cJSON_IsArray(queue) || cJSON_GetArraySize(queue) > APP_TRACKER_SLOTS)
        return false;
    tracker_store_t *next = heap_caps_calloc(1, sizeof(*next), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!next)
        return false;
    next->active = -1;
    const cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assetGeneration");
    if (assets) {
        if (!cJSON_IsNumber(assets) || !isfinite(assets->valuedouble) ||
            assets->valuedouble < 0 || assets->valuedouble > UINT32_MAX ||
            assets->valuedouble != floor(assets->valuedouble)) goto invalid;
        next->asset_generation = (uint32_t)assets->valuedouble;
    }
    unsigned slots = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, queue)
    {
        app_tracker_flight_t *f = &next->flights[next->count];
        if (!app_remote_decode_state(item, &f->display))
            goto invalid;
        const cJSON *meta = cJSON_GetObjectItemCaseSensitive(item, "tracking");
        if (!app_tracker_decode_zone(cJSON_GetObjectItemCaseSensitive(meta, "originOffsetSteps"), &f->origin_zone) ||
            !app_tracker_decode_zone(cJSON_GetObjectItemCaseSensitive(meta, "destinationOffsetSteps"), &f->destination_zone))
            goto invalid;
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(meta, "planId");
        const cJSON *provider = cJSON_GetObjectItemCaseSensitive(meta, "providerId");
        const cJSON *dep = cJSON_GetObjectItemCaseSensitive(meta, "scheduledDepartureUnix");
        const cJSON *a = cJSON_GetObjectItemCaseSensitive(meta, "originLatitude");
        const cJSON *b = cJSON_GetObjectItemCaseSensitive(meta, "originLongitude");
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(meta, "destinationLatitude");
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(meta, "destinationLongitude");
        if (!cJSON_IsString(id) || !*id->valuestring ||
            strlen(id->valuestring) >= sizeof(f->plan_id) || !cJSON_IsNumber(a) ||
            !cJSON_IsNumber(b) || !cJSON_IsNumber(c) || !cJSON_IsNumber(d) ||
            !app_tracker_coordinates_valid(a->valuedouble, b->valuedouble) ||
            !app_tracker_coordinates_valid(c->valuedouble, d->valuedouble) ||
            !(f->scheduled_departure = app_tracker_timestamp(dep)) ||
            !f->display.tracked_flight_valid || !*f->display.tracked_flight_callsign)
            goto invalid;
        const cJSON *slot_value = cJSON_GetObjectItemCaseSensitive(item, "deviceSlot");
        if (!cJSON_IsNumber(slot_value) || slot_value->valuedouble != slot_value->valueint ||
            slot_value->valueint < 0 || slot_value->valueint >= APP_TRACKER_SLOTS ||
            f->display.tracked_route_point_count < 2)
            goto invalid;
        unsigned slot = f->display.tracked_flight_slot;
        if (slots & (1U << slot))
            goto invalid;
        for (unsigned j = 0; j < next->count; j++)
            if (!strcmp(next->flights[j].plan_id, id->valuestring))
                goto invalid;
        slots |= 1U << slot;
        snprintf(f->plan_id, sizeof(f->plan_id), "%s", id->valuestring);
        if (cJSON_IsString(provider)) {
            if (strlen(provider->valuestring) >= sizeof(f->provider_id))
                goto invalid;
            snprintf(f->provider_id, sizeof(f->provider_id), "%s", provider->valuestring);
        }
        f->origin_lat = a->valuedouble;
        f->origin_lon = b->valuedouble;
        f->destination_lat = c->valuedouble;
        f->destination_lon = d->valuedouble;
        /* Seed values are retained, but never labeled as a fresh ADS-B fix. */
        f->estimated = true;
        snprintf(f->source, sizeof(f->source), "Awaiting live position");
        next->count++;
    }
    /* The queue is a prescription; reject ambiguous overlapping windows. */
    for (unsigned i = 0; i < next->count; i++)
        for (unsigned j = i + 1; j < next->count; j++) {
            const app_tracker_flight_t *a = &next->flights[i], *b = &next->flights[j];
            if (a->scheduled_departure < b->display.tracked_flight_estimated_arrival_unix &&
                b->scheduled_departure < a->display.tracked_flight_estimated_arrival_unix)
                goto invalid;
        }
    const cJSON *credentials = cJSON_GetObjectItemCaseSensitive(root, "trackerCredentials");
    if (cJSON_IsObject(credentials)) {
        app_config_t cfg;
        app_config_get(&cfg);
        const char *providers[] = {"openSky", "openSky", "airLabs", "flightAware"};
        const char *keys[] = {"clientId", "clientSecret", "apiKey", "apiKey"};
        char *dest[] = {cfg.opensky_client_id, cfg.opensky_client_secret, cfg.airlabs_api_key,
                        cfg.flightaware_api_key};
        const size_t sizes[] = {sizeof(cfg.opensky_client_id), sizeof(cfg.opensky_client_secret),
                                sizeof(cfg.airlabs_api_key), sizeof(cfg.flightaware_api_key)};
        for (unsigned i = 0; i < 4; i++) {
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(
                cJSON_GetObjectItemCaseSensitive(credentials, providers[i]), keys[i]);
            if (cJSON_IsString(v)) {
                if (strlen(v->valuestring) >= sizes[i])
                    goto invalid;
                snprintf(dest[i], sizes[i], "%s", v->valuestring);
            }
        }
        cfg.opensky_enabled = true;
        cfg.flightaware_available = *cfg.flightaware_api_key != '\0';
        if (app_config_save(&cfg) != ESP_OK)
            goto invalid;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    for (unsigned i = 0; i < next->count; i++)
        for (unsigned j = 0; j < store->count; j++) {
            app_tracker_flight_t *n = &next->flights[i], *old = &store->flights[j];
            if (!strcmp(n->plan_id, old->plan_id) &&
                n->scheduled_departure == old->scheduled_departure &&
                !strcmp(n->display.tracked_flight_origin, old->display.tracked_flight_origin) &&
                !strcmp(n->display.tracked_flight_destination,
                        old->display.tracked_flight_destination) &&
                (!*n->provider_id || !strcmp(n->provider_id, old->provider_id))) {
                app_tracker_preserve_runtime(n, old);
            }
        }
    int prior = -1;
    for (unsigned i = 0; i < next->count; i++) {
        app_tracker_reconcile(&next->flights[i], time(NULL));
        if (store->active >= 0 &&
            !strcmp(next->flights[i].plan_id, store->flights[store->active].plan_id))
            prior = i;
    }
    next->active = app_tracker_select(next->flights, next->count, prior, time(NULL));
    if (!save(next)) {
        xSemaphoreGive(mutex);
        goto invalid;
    }
    *store = *next;
    generation++;
    next_tick = 0;
    publish();
    xSemaphoreGive(mutex);
    heap_caps_free(next);
    return true;
invalid:
    heap_caps_free(next);
    return false;
}
bool app_tracker_clear(void)
{
    if (!store || !ready)
        return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool cleared = false;
    tracker_store_t *empty =
        heap_caps_calloc(1, sizeof(*empty), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (empty) {
        empty->active = -1;
        if (save(empty)) {
            *store = *empty;
            generation++;
            next_tick = 0;
            publish();
            cleared = true;
        }
        heap_caps_free(empty);
    }
    xSemaphoreGive(mutex);
    return cleared;
}
static bool encode(const char *s, char *out, size_t size)
{
    size_t i = 0;
    for (; *s; s++) {
        unsigned char c = *s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.') {
            if (i + 1 >= size)
                return false;
            out[i++] = c;
        } else {
            if (i + 3 >= size)
                return false;
            snprintf(out + i, size - i, "%%%02X", c);
            i += 3;
        }
    }
    out[i] = 0;
    return true;
}
static cJSON *request_form(const char *url, const char *key, const char *form, int *code)
{
    return app_data_request_provider_json(url, form, key, 64 * 1024, code, NULL);
}
static cJSON *request(const char *url, const char *key, int *code)
{
    return request_form(url, key, NULL, code);
}
int app_tracker_flightaware_probe_status(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    if (!*cfg.flightaware_api_key)
        return -1;
    int code = 0;
    /* Alerts are a Standard/Premium feature and return 401 even for a valid
     * Personal-tier key. Match the companion's no-cost account health probe;
     * only a successful, parsed response validates the credential. */
    cJSON *root =
        request("https://aeroapi.flightaware.com/aeroapi/account/usage",
                cfg.flightaware_api_key, &code);
    bool valid = code == 200 && cJSON_IsObject(root);
    cJSON_Delete(root);
    return valid ? 200 : code == 200 ? 0 : code;
}

static bool schedule(app_tracker_flight_t *f, const app_config_t *cfg, int64_t now)
{
    char id[400], url[768];
    bool matched = false;
    unsigned reported = 0;
    cJSON *root = NULL;
    if (*cfg->flightaware_api_key &&
        encode(*f->provider_id ? f->provider_id : f->display.tracked_flight_callsign, id,
               sizeof(id))) {
        snprintf(url, sizeof(url), "https://aeroapi.flightaware.com/aeroapi/flights/%s", id);
        root = request(url, cfg->flightaware_api_key, &f->http_status);
        matched = app_tracker_parse_schedule_fields(f, root, true, now, &reported);
        cJSON_Delete(root);
    }
    bool after_eta = f->display.tracked_flight_estimated_arrival_unix > 0 &&
                     now >= f->display.tracked_flight_estimated_arrival_unix;
    bool missing_gates = (!(reported & 1) && !*f->display.tracked_flight_origin_gate) ||
                         (!(reported & 2) && !*f->display.tracked_flight_destination_gate) ||
                         (!(reported & 4) && !*f->display.tracked_flight_origin_terminal) ||
                         (!(reported & 8) && !*f->display.tracked_flight_destination_terminal);
    if ((!matched || missing_gates || (after_eta && !app_tracker_terminal(f))) &&
        *cfg->airlabs_api_key) {
        char key[300];
        if (encode(f->display.tracked_flight_number, id, sizeof(id)) &&
            encode(cfg->airlabs_api_key, key, sizeof(key))) {
            snprintf(url, sizeof(url), "flight_iata=%s&api_key=%s", id, key);
            root = request_form("https://airlabs.co/api/v9/flight", NULL, url, &f->http_status);
            if (matched) {
                app_tracker_flight_t *supplement = heap_caps_malloc(sizeof(*supplement), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (supplement) *supplement = *f;
                if (supplement && app_tracker_parse_schedule(supplement, root, false, now)) {
                    /* FlightAware still owns schedule metadata on its exact occurrence. */
                    if (!(reported & 1) && !*f->display.tracked_flight_origin_gate)
                        memcpy(f->display.tracked_flight_origin_gate,
                               supplement->display.tracked_flight_origin_gate,
                               sizeof(f->display.tracked_flight_origin_gate));
                    if (!(reported & 2) && !*f->display.tracked_flight_destination_gate)
                        memcpy(f->display.tracked_flight_destination_gate,
                               supplement->display.tracked_flight_destination_gate,
                               sizeof(f->display.tracked_flight_destination_gate));
                    if (!(reported & 4) && !*f->display.tracked_flight_origin_terminal)
                        memcpy(f->display.tracked_flight_origin_terminal,
                               supplement->display.tracked_flight_origin_terminal,
                               sizeof(f->display.tracked_flight_origin_terminal));
                    if (!(reported & 8) && !*f->display.tracked_flight_destination_terminal)
                        memcpy(f->display.tracked_flight_destination_terminal,
                               supplement->display.tracked_flight_destination_terminal,
                               sizeof(f->display.tracked_flight_destination_terminal));
                    if (!*f->icao24)
                        memcpy(f->icao24, supplement->icao24, sizeof(f->icao24));
                    if (after_eta && supplement->display.tracked_flight_actual_arrival_unix > 0) {
                        f->display.tracked_flight_actual_arrival_unix =
                            supplement->display.tracked_flight_actual_arrival_unix;
                        memcpy(f->display.tracked_flight_journey_phase,
                               supplement->display.tracked_flight_journey_phase,
                               sizeof(f->display.tracked_flight_journey_phase));
                        if (app_tracker_diverted(supplement))
                            snprintf(f->display.tracked_flight_status,
                                     sizeof(f->display.tracked_flight_status), "Landed after diversion");
                    }
                }
                heap_caps_free(supplement);
            } else
                matched = app_tracker_parse_schedule(f, root, false, now);
            cJSON_Delete(root);
        }
    }
    return matched;
}
static bool motion(app_tracker_flight_t *f, int64_t now)
{
    char query[240];
    if (*f->icao24) {
        char id[32];
        if (!encode(f->icao24, id, sizeof(id)))
            return false;
        snprintf(query, sizeof(query), "icao24=%s&extended=1", id);
        cJSON *root = app_data_tracker_opensky(query, &f->http_status);
        bool ok = app_tracker_parse_opensky(f, root, now);
        cJSON_Delete(root);
        return ok;
    }
    /* Search around today's expected progress, never the Radar's home city.
     * Split the Pacific dateline into two legal bounding boxes. */
    double lat, lon;
    app_tracker_route_coordinate(f, f->display.route_progress, &lat, &lon);
    double low = lon - 14, high = lon + 14;
    for (int i = 0; i < 2; i++) {
        double a = low, b = high;
        if (i == 0) {
            a = fmax(-180, a);
            b = fmin(180, b);
        } else if (low < -180) {
            a = low + 360;
            b = 180;
        } else if (high > 180) {
            a = -180;
            b = high - 360;
        } else
            break;
        snprintf(query, sizeof(query), "lamin=%.4f&lamax=%.4f&lomin=%.4f&lomax=%.4f&extended=1",
                 fmax(-90, lat - 8), fmin(90, lat + 8), a, b);
        cJSON *root = app_data_tracker_opensky(query, &f->http_status);
        if (f->http_status == APP_OPENSKY_DEFERRED) {
            /* An unqueried dateline half is not evidence of lost coverage. */
            cJSON_Delete(root);
            break;
        }
        bool ok = app_tracker_parse_opensky(f, root, now);
        cJSON_Delete(root);
        if (ok)
            return true;
    }
    return false;
}
void app_tracker_tick(void)
{
    if (!store || !ready)
        return;
    int64_t now = time(NULL);
    if (now < 1700000000)
        return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    /* The renderer rebuilds tracked panels on a remote revision. Publish at
     * the telemetry cadence, not on every background-worker wakeup. Local
     * countdown/route animation continues independently between samples. */
    if (now < next_tick) {
        xSemaphoreGive(mutex);
        return;
    }
    next_tick = now + 60;
    if (!store->count) {
        xSemaphoreGive(mutex);
        return;
    }
    for (unsigned i = 0; i < store->count; i++)
        app_tracker_reconcile(&store->flights[i], now);
    store->active = app_tracker_select(store->flights, store->count, store->active, now);
    publish();
    int index = store->active;
    if (index < 0) {
        xSemaphoreGive(mutex);
        return;
    }
    /* A retained arrival/cancellation is presentation only; continue polling
     * the *next* prescription at its normal sparse schedule cadence. */
    if (app_tracker_terminal(&store->flights[index])) {
        int next = -1;
        for (unsigned i = 0; i < store->count; i++) {
            if (!store->flights[i].display.tracked_flight_valid || app_tracker_terminal(&store->flights[i])) continue;
            if (next < 0 || store->flights[i].scheduled_departure < store->flights[next].scheduled_departure)
                next = (int)i;
        }
        if (next < 0) { xSemaphoreGive(mutex); return; }
        index = next;
    }
    app_tracker_flight_t f = store->flights[index];
    uint32_t started_generation = generation;
    xSemaphoreGive(mutex);
    app_config_t cfg;
    app_config_get(&cfg);
    bool changed = false;
    if (app_tracker_schedule_due(&f, now)) {
        f.next_schedule = now + app_tracker_schedule_interval(&f, now);
        if (schedule(&f, &cfg, now))
            f.checked_unix = time(NULL);
        changed = true;
    }
    int64_t effective_departure = f.display.tracked_flight_estimated_departure_unix;
    if (!effective_departure) effective_departure = f.scheduled_departure;
    bool near_departure = app_tracker_airborne(&f) || now >= effective_departure - 600;
    if (!app_tracker_terminal(&f) && !app_tracker_diverted(&f) && near_departure && now >= f.next_motion) {
        f.next_motion = now + 60;
        int64_t prior_contact = f.contact_unix, prior_position = f.position_unix;
        bool ok = cfg.opensky_enabled && motion(&f, now);
        bool deferred = cfg.opensky_enabled && f.http_status == APP_OPENSKY_DEFERRED;
        if (ok)
            f.missing_since = 0;
        else if (!deferred && !f.missing_since)
            f.missing_since = now;
        if (!ok && !deferred && app_tracker_airborne(&f) &&
            (f.position_unix == 0 || now - f.missing_since >= 300) &&
            (!f.last_fallback || now - f.last_fallback >= 300) && *cfg.flightaware_api_key &&
            *f.provider_id) {
            /* Attempt time is preserved across successful OpenSky samples and reboot. */
            f.last_fallback = now;
            xSemaphoreTake(mutex, portMAX_DELAY);
            bool reserved = false;
            if (generation == started_generation) {
                store->flights[index].last_fallback = now;
                reserved = save(store);
            }
            xSemaphoreGive(mutex);
            char id[400], url[512];
            if (reserved && encode(f.provider_id, id, sizeof(id))) {
                snprintf(url, sizeof(url),
                         "https://aeroapi.flightaware.com/aeroapi/flights/%s/position", id);
                cJSON *root = request(url, cfg.flightaware_api_key, &f.http_status);
                ok = app_tracker_parse_position(&f, root, time(NULL));
                cJSON_Delete(root);
            }
        }
        if (ok || f.position_unix > prior_position || f.contact_unix > prior_contact)
            f.checked_unix = time(NULL);
        changed = true;
    }
    app_tracker_reconcile(&f, time(NULL));
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (generation == started_generation) {
        store->flights[index] = f;
        store->active = app_tracker_select(store->flights, store->count, index, time(NULL));
        publish();
        if (changed && (now - last_save >= 300 || app_tracker_terminal(&f)))
            save(store);
    }
    xSemaphoreGive(mutex);
}
cJSON *app_tracker_snapshot(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "deviceTrackingSupported", true);
    cJSON_AddNumberToObject(root, "deviceUnix", (double)time(NULL));
    if (!store || !mutex)
        return root;
    xSemaphoreTake(mutex, portMAX_DELAY);
    cJSON_AddNumberToObject(root, "preparedFlightCount", store->count);
    if (store->active >= 0) {
        app_tracker_flight_t *f = &store->flights[store->active];
        app_remote_status_t *d = &f->display;
        cJSON_AddStringToObject(root, "planId", f->plan_id);
        cJSON_AddStringToObject(root, "flightNumber", d->tracked_flight_number);
        cJSON_AddStringToObject(root, "origin", d->tracked_flight_origin);
        cJSON_AddStringToObject(root, "originCity", d->tracked_flight_origin_city);
        cJSON_AddStringToObject(root, "destination", d->tracked_flight_destination);
        cJSON_AddStringToObject(root, "destinationCity", d->tracked_flight_destination_city);
        cJSON_AddStringToObject(root, "status", d->tracked_flight_status);
        cJSON_AddStringToObject(root, "journeyPhase", d->tracked_flight_journey_phase);
        cJSON_AddStringToObject(root, "source", f->source);
        cJSON_AddBoolToObject(root, "estimatedPosition", f->estimated);
        cJSON_AddNumberToObject(root, "routeProgress", d->route_progress);
        cJSON_AddNumberToObject(root, "positionUnix", f->position_unix);
        cJSON_AddNumberToObject(root, "checkedUnix", f->checked_unix);
        cJSON_AddNumberToObject(root, "httpStatus", f->http_status);
        cJSON_AddNumberToObject(root, "altitudeMeters", d->tracked_flight_altitude_m);
        cJSON_AddNumberToObject(root, "speedKPH", d->tracked_flight_speed_kph);
        if (d->tracked_flight_heading_valid)
            cJSON_AddNumberToObject(root, "headingDegrees", d->tracked_flight_heading_deg);
        cJSON_AddNumberToObject(root, "actualDepartureUnix",
                                d->tracked_flight_actual_departure_unix);
        cJSON_AddNumberToObject(root, "estimatedDepartureUnix",
                                d->tracked_flight_estimated_departure_unix);
        cJSON_AddNumberToObject(root, "estimatedArrivalUnix",
                                d->tracked_flight_estimated_arrival_unix);
        cJSON_AddNumberToObject(root, "actualArrivalUnix", d->tracked_flight_actual_arrival_unix);
        cJSON_AddStringToObject(root, "originTerminal", d->tracked_flight_origin_terminal);
        cJSON_AddStringToObject(root, "originGate", d->tracked_flight_origin_gate);
        cJSON_AddStringToObject(root, "destinationTerminal",
                                d->tracked_flight_destination_terminal);
        cJSON_AddStringToObject(root, "destinationGate", d->tracked_flight_destination_gate);
    }
    xSemaphoreGive(mutex);
    return root;
}
