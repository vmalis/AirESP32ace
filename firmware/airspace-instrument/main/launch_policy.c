// AirESP32ace
// Created by VM, August 2026
#include "launches.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char *text(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : "";
}
static bool contains(const char *value, const char *needle)
{
    for (; *value; value++) if (strncasecmp(value, needle, strlen(needle)) == 0) return true;
    return false;
}
static void copy(char *to, size_t size, const char *from) { snprintf(to, size, "%s", from); }

int64_t app_launch_timestamp(const char *s)
{
    int y, m, d, h, min, sec, used = 0;
    if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d%n", &y, &m, &d, &h, &min, &sec, &used) != 6 ||
        y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 || h < 0 || h > 23 ||
        min < 0 || min > 59 || sec < 0 || sec > 59) return 0;
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (d > days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) return 0;
    const char *tail = s + used;
    if (*tail == '.') { tail++; if (!isdigit((unsigned char)*tail)) return 0;
        while (isdigit((unsigned char)*tail)) tail++; }
    if (strcmp(tail, "Z") && strcmp(tail, "+00:00")) return 0;
    /* Civil date to Unix days, independent of the device's selected timezone. */
    y -= m <= 2;
    int era = y / 400, yo = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t day = era * 146097 + yo * 365 + yo / 4 - yo / 100 + doy - 719468;
    return day * 86400 + h * 3600 + min * 60 + sec;
}

bool app_launch_terminal(int status) { return status == 3 || status == 4 || status == 7; }
bool app_launch_valid_id(const char *id)
{
    if (!id || strlen(id) != 36) return false;
    for (unsigned i = 0; i < 36; i++)
        if ((i == 8 || i == 13 || i == 18 || i == 23) ? id[i] != '-' : !isxdigit((unsigned char)id[i])) return false;
    return true;
}
int app_launch_utc_offset(const char *site, int64_t now)
{
    if (!strcmp(site, "baikonur")) return 18000;
    int standard = !strcmp(site, "starbase") ? -21600 : -18000;
    time_t timestamp = now; struct tm date; gmtime_r(&timestamp, &date);
    char first[32];
    snprintf(first, sizeof(first), "%04d-03-01T00:00:00Z", date.tm_year + 1900);
    int64_t march = app_launch_timestamp(first);
    snprintf(first, sizeof(first), "%04d-11-01T00:00:00Z", date.tm_year + 1900);
    int64_t november = app_launch_timestamp(first);
    int march_sunday = (7 - (march / 86400 + 4) % 7) % 7;
    int november_sunday = (7 - (november / 86400 + 4) % 7) % 7;
    int64_t start = march + (march_sunday + 7) * 86400 + 7200 - standard;
    int64_t end = november + november_sunday * 86400 + 7200 - (standard + 3600);
    return standard + (now >= start && now < end ? 3600 : 0);
}
bool app_launch_expired(const app_launch_entry_t *e, int64_t now)
{
    return e->launch.valid && ((e->completed && now >= e->completed + 7200) ||
           (e->launch.net_unix > 0 && now > e->launch.net_unix + 2 * 86400));
}
int64_t app_launch_interval(const app_launch_entry_t *e, int64_t now)
{
    if (!e->launch.valid) return 6 * 3600;
    if (app_launch_terminal(e->status_id)) return 1800;
    int64_t until = e->launch.net_unix - now;
    return until <= 6 * 3600 ? 300 : until <= 86400 ? 1800 : 6 * 3600;
}
int64_t app_launch_next_check(const app_launch_entry_t *e, int64_t now)
{
    int64_t due = now + app_launch_interval(e, now);
    if (e->launch.valid && !app_launch_terminal(e->status_id)) {
        const int thresholds[] = {86400, 21600};
        for (unsigned i = 0; i < 2; i++) {
            int64_t boundary = e->launch.net_unix - thresholds[i];
            if (boundary > now && due > boundary) due = boundary;
        }
    }
    return due;
}
bool app_launch_stale_for_mask(const app_launch_entry_t *e, int64_t now, uint8_t enabled)
{
    if (!e->checked || now < e->checked) return true;
    /* A confirmed final result is historical, not a missed live sample. */
    if (app_launch_terminal(e->status_id)) return false;
    unsigned programs = 0;
    for (unsigned i = 0; i < 3; i++) programs += (enabled >> i) & 1;
    int64_t fair_interval = (programs ? programs : 1) * (3600 / APP_LAUNCH_REQUESTS_PER_HOUR);
    int64_t grace = app_launch_interval(e, now);
    if (grace < fair_interval) grace = fair_interval;
    int64_t due = app_launch_next_check(e, e->checked);
    if (due < e->checked + fair_interval) due = e->checked + fair_interval;
    return now > due + grace;
}
bool app_launch_stale(const app_launch_entry_t *e, int64_t now)
{
    return app_launch_stale_for_mask(e, now, 1);
}
int64_t app_launch_failure_interval(unsigned failures)
{
    const int delays[] = {60, 300, 900, 1800, 3600, 6 * 3600};
    return delays[failures > 6 ? 5 : failures ? failures - 1 : 0];
}
int64_t app_launch_budget_due(const int64_t attempts[APP_LAUNCH_REQUESTS_PER_HOUR], int64_t now)
{
    int64_t oldest = INT64_MAX, latest = 0;
    unsigned count = 0;
    for (unsigned i = 0; i < APP_LAUNCH_REQUESTS_PER_HOUR; i++) {
        if (attempts[i] > latest) latest = attempts[i];
        if (attempts[i] > now - 3600) { count++; if (attempts[i] < oldest) oldest = attempts[i]; }
    }
    /* Pace reservations rather than exhausting the full hour in a burst. */
    int64_t due = latest ? latest + 3600 / APP_LAUNCH_REQUESTS_PER_HOUR : now;
    if (count == APP_LAUNCH_REQUESTS_PER_HOUR && due < oldest + 3601) due = oldest + 3601;
    return due;
}
bool app_launch_agencies(const cJSON *array, uint8_t *mask)
{
    if (!cJSON_IsArray(array) || !mask) return false;
    uint8_t value = 0;
    const cJSON *item;
    const char *names[] = {"nasa", "roscosmos", "spacex"};
    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsString(item)) return false;
        bool found = false;
        for (unsigned a = 0; a < 3; a++) if (!strcmp(item->valuestring, names[a])) {
            value |= 1U << a; found = true;
        }
        if (!found) return false;
    }
    if (!value) return false;
    *mask = value;
    return true;
}
bool app_launch_parse(const cJSON *j, unsigned a, app_remote_launch_t *out, int *status_id)
{
    if (!cJSON_IsObject(j) || a >= 3 || !out || !status_id) return false;
    const char *id = text(j, "id"), *name = text(j, "name");
    const char *match[] = {"Artemis", "Soyuz MS", "Starship"};
    const cJSON *rocket = cJSON_GetObjectItemCaseSensitive(j, "rocket");
    const cJSON *cfg = cJSON_GetObjectItemCaseSensitive(rocket, "configuration");
    if (!contains(name, match[a]) && !(a == 2 && contains(text(cfg, "full_name"), "Starship"))) return false;
    if (!app_launch_valid_id(id)) return false;
    int64_t net = app_launch_timestamp(text(j, "net"));
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(j, "status");
    const cJSON *status_number = cJSON_GetObjectItemCaseSensitive(status, "id");
    if (!net || !cJSON_IsNumber(status_number) || !*text(status, "name")) return false;
    memset(out, 0, sizeof(*out));
    out->valid = true;
    copy(out->id, sizeof(out->id), id);
    copy(out->full_name, sizeof(out->full_name), name);
    const char *mission = strchr(name, '|'); mission = mission ? mission + 1 : name;
    while (*mission == ' ') mission++;
    copy(out->mission, sizeof(out->mission), mission);
    for (char *p = out->mission; *p; p++) *p = toupper((unsigned char)*p);
    const char *agencies[] = {"nasa", "roscosmos", "spacex"};
    const char *vehicles[] = {"sls", "soyuz", "starship"};
    copy(out->agency, sizeof(out->agency), agencies[a]);
    copy(out->vehicle, sizeof(out->vehicle), vehicles[a]);
    const cJSON *pad = cJSON_GetObjectItemCaseSensitive(j, "pad");
    const cJSON *location = cJSON_GetObjectItemCaseSensitive(pad, "location");
    copy(out->pad, sizeof(out->pad), text(pad, "name"));
    copy(out->location, sizeof(out->location), text(location, "name"));
    copy(out->site, sizeof(out->site), a == 1 ? "baikonur" :
         a == 2 && (contains(out->location, "starbase") || contains(out->location, "boca chica")) ? "starbase" : "florida");
    const cJSON *precision = cJSON_GetObjectItemCaseSensitive(j, "net_precision");
    copy(out->precision, sizeof(out->precision), text(precision, "name"));
    copy(out->precision_abbrev, sizeof(out->precision_abbrev), text(precision, "abbrev"));
    copy(out->status, sizeof(out->status), text(status, "name"));
    copy(out->status_abbrev, sizeof(out->status_abbrev), text(status, "abbrev"));
    out->net_unix = net;
    out->updated_unix = app_launch_timestamp(text(j, "last_updated"));
    if (!out->updated_unix) return false;
    out->weather_probability_percent = -1;
    const cJSON *probability = cJSON_GetObjectItemCaseSensitive(j, "probability");
    if (cJSON_IsNumber(probability) && probability->valueint >= 0 && probability->valueint <= 100)
        out->weather_probability_percent = probability->valueint;
    const cJSON *video;
    cJSON_ArrayForEach(video, cJSON_GetObjectItemCaseSensitive(j, "vid_urls")) {
        const char *url = text(video, "url");
        if (!strncmp(url, "https://", 8) && strlen(url) < sizeof(out->watch_url)) {
            copy(out->watch_url, sizeof(out->watch_url), url); break;
        }
    }
    *status_id = status_number->valueint;
    return true;
}
bool app_launch_accept(app_launch_entry_t *e, const cJSON *json, unsigned a, int64_t now, bool exact)
{
    app_remote_launch_t parsed;
    int status;
    if (!app_launch_parse(json, a, &parsed, &status) ||
        (exact && strcmp(e->launch.id, parsed.id)) ||
        parsed.net_unix < now - 2 * 86400 || parsed.updated_unix > now + 300) return false;
    bool same = !strcmp(e->launch.id, parsed.id);
    if (same && parsed.updated_unix && e->launch.updated_unix > parsed.updated_unix) return false;
    if (same && !*parsed.watch_url) copy(parsed.watch_url, sizeof(parsed.watch_url), e->launch.watch_url);
    if (same && !*parsed.location) {
        copy(parsed.location, sizeof(parsed.location), e->launch.location);
        copy(parsed.site, sizeof(parsed.site), e->launch.site);
    }
    if (same && !*parsed.pad) copy(parsed.pad, sizeof(parsed.pad), e->launch.pad);
    if (!*parsed.watch_url) copy(parsed.watch_url, sizeof(parsed.watch_url),
        a == 2 ? "https://www.spacex.com/launches/" : a == 1 ? "https://www.roscosmos.ru/" : "https://www.nasa.gov/nasatv/");
    e->launch = parsed;
    e->launch.current_utc_offset_s = app_launch_utc_offset(parsed.site, now);
    e->status_id = status;
    if (!same || !app_launch_terminal(status)) e->completed = 0;
    if (app_launch_terminal(status) && !e->completed) e->completed = now;
    e->checked = now;
    e->failures = 0;
    e->next_check = app_launch_next_check(e, now);
    return true;
}
