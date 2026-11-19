// AirESP32ace
// Created by VM, August 2026

#include "tracker.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define RAD (3.14159265358979323846 / 180.0)
static const cJSON *get(const cJSON *o, const char *k)
{
    return cJSON_GetObjectItemCaseSensitive(o, k);
}
static const char *str(const cJSON *o, const char *k)
{
    const cJSON *v = get(o, k);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static void copy(char *dst, size_t size, const char *s)
{
    if (s && *s)
        snprintf(dst, size, "%s", s);
}
static bool number(const cJSON *v)
{
    return cJSON_IsNumber(v) && isfinite(v->valuedouble);
}
static double clamp(double v, double lo, double hi)
{
    return fmax(lo, fmin(hi, v));
}

/* Caller has matched the same occurrence. Preserve live device-owned state,
 * but accept repaired static labels and newly prepared map geometry. */
void app_tracker_preserve_runtime(app_tracker_flight_t *next, const app_tracker_flight_t *old)
{
    uint8_t slot = next->display.tracked_flight_slot;
    int16_t points[APP_REMOTE_TRACKED_ROUTE_MAX_POINTS][2];
    memcpy(points, next->display.tracked_route_points, sizeof(points));
    uint8_t count = next->display.tracked_route_point_count;
    char origin[sizeof(next->display.tracked_flight_origin_city)];
    char destination[sizeof(next->display.tracked_flight_destination_city)];
    memcpy(origin, next->display.tracked_flight_origin_city, sizeof(origin));
    memcpy(destination, next->display.tracked_flight_destination_city, sizeof(destination));
    *next = *old;
    next->display.tracked_flight_slot = slot;
    memcpy(next->display.tracked_route_points, points, sizeof(points));
    next->display.tracked_route_point_count = count;
    copy(next->display.tracked_flight_origin_city, sizeof(origin), origin);
    copy(next->display.tracked_flight_destination_city, sizeof(destination), destination);
}

bool app_tracker_coordinates_valid(double lat, double lon)
{
    return isfinite(lat) && isfinite(lon) && fabs(lat) <= 90 && fabs(lon) <= 180;
}
static double angle(double a, double b, double c, double d)
{
    double h = pow(sin((c - a) * RAD / 2), 2) +
               cos(a * RAD) * cos(c * RAD) * pow(sin((d - b) * RAD / 2), 2);
    return 2 * asin(sqrt(clamp(h, 0, 1)));
}
static double bearing(double a, double b, double c, double d)
{
    return atan2(sin((d - b) * RAD) * cos(c * RAD),
                 cos(a * RAD) * sin(c * RAD) - sin(a * RAD) * cos(c * RAD) * cos((d - b) * RAD));
}
double app_tracker_progress(const app_tracker_flight_t *f, double lat, double lon)
{
    double total = angle(f->origin_lat, f->origin_lon, f->destination_lat, f->destination_lon);
    double dist = angle(f->origin_lat, f->origin_lon, lat, lon);
    double delta = bearing(f->origin_lat, f->origin_lon, lat, lon) -
                   bearing(f->origin_lat, f->origin_lon, f->destination_lat, f->destination_lon);
    return total > 1e-9 ? clamp(atan2(sin(dist) * cos(delta), cos(dist)) / total, 0, 1) : 0;
}
void app_tracker_route_coordinate(const app_tracker_flight_t *f, double p, double *lat, double *lon)
{
    double delta = angle(f->origin_lat, f->origin_lon, f->destination_lat, f->destination_lon);
    if (fabs(sin(delta)) < 1e-9) {
        *lat = f->origin_lat;
        *lon = f->origin_lon;
        return;
    }
    double a = sin((1 - p) * delta) / sin(delta), b = sin(p * delta) / sin(delta);
    double x = a * cos(f->origin_lat * RAD) * cos(f->origin_lon * RAD) +
               b * cos(f->destination_lat * RAD) * cos(f->destination_lon * RAD);
    double y = a * cos(f->origin_lat * RAD) * sin(f->origin_lon * RAD) +
               b * cos(f->destination_lat * RAD) * sin(f->destination_lon * RAD);
    double z = a * sin(f->origin_lat * RAD) + b * sin(f->destination_lat * RAD);
    *lat = atan2(z, hypot(x, y)) / RAD;
    *lon = atan2(y, x) / RAD;
}

/* Provider timestamps are UTC. Do not parse using the device's local TZ. */
int64_t app_tracker_timestamp(const cJSON *v)
{
    if (number(v))
        return v->valuedouble > 0 ? (int64_t)v->valuedouble : 0;
    if (!cJSON_IsString(v))
        return 0;
    int y, m, d, h, n;
    double seconds;
    if (sscanf(v->valuestring, "%d-%d-%dT%d:%d:%lfZ", &y, &m, &d, &h, &n, &seconds) != 6 ||
        !strchr(v->valuestring, 'Z') || y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 ||
        d > 31 || h < 0 || h > 23 || n < 0 || n > 59 || seconds < 0 || seconds >= 60)
        return 0;
    y -= m <= 2;
    int era = y / 400, yo = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t days = era * 146097 + yo * 365 + yo / 4 - yo / 100 + doy - 719468;
    return days * 86400 + h * 3600 + n * 60 + (int)seconds;
}
static int64_t first_time(const cJSON *o, const char *a, const char *b, const char *c)
{
    int64_t t = app_tracker_timestamp(get(o, a));
    if (!t && b)
        t = app_tracker_timestamp(get(o, b));
    if (!t && c)
        t = app_tracker_timestamp(get(o, c));
    return t;
}
static bool identity(const char *a, const char *b)
{
    char lhs[32] = {0}, rhs[32] = {0};
    unsigned i = 0;
    for (; *a && i < sizeof(lhs) - 1; a++)
        if (isalnum((unsigned char)*a))
            lhs[i++] = toupper((unsigned char)*a);
    i = 0;
    for (; *b && i < sizeof(rhs) - 1; b++)
        if (isalnum((unsigned char)*b))
            rhs[i++] = toupper((unsigned char)*b);
    return *lhs && !strcmp(lhs, rhs);
}
bool app_tracker_terminal(const app_tracker_flight_t *f)
{
    return f->display.tracked_flight_actual_arrival_unix > 0 ||
           !strcasecmp(f->display.tracked_flight_journey_phase, "arrived") ||
           !strcasecmp(f->display.tracked_flight_status, "Cancelled");
}
bool app_tracker_airborne(const app_tracker_flight_t *f)
{
    return !app_tracker_terminal(f) &&
           (f->display.tracked_flight_actual_departure_unix > 0 ||
            !strcasecmp(f->display.tracked_flight_journey_phase, "enroute"));
}
static bool fresh(int64_t stamp, int64_t now, int age)
{
    return stamp > 0 && stamp <= now + 30 && now - stamp <= age;
}
static bool set_position(app_tracker_flight_t *f, double lat, double lon, int64_t stamp,
                         int64_t now)
{
    if (!app_tracker_coordinates_valid(lat, lon) || !fresh(stamp, now, 180) ||
        stamp < f->position_unix)
        return false;
    /* Reject impossible jumps, including a reused callsign on another flight. */
    if (f->position_unix && stamp > f->position_unix &&
        angle(f->latitude, f->longitude, lat, lon) * 6371 > (stamp - f->position_unix) * 0.42 + 80)
        return false;
    double p = app_tracker_progress(f, lat, lon);
    double route_lat, route_lon;
    app_tracker_route_coordinate(f, p, &route_lat, &route_lon);
    if (angle(route_lat, route_lon, lat, lon) * 6371 > 1200)
        return false;
    f->latitude = lat;
    f->longitude = lon;
    f->position_unix = stamp;
    /* Only actual samples are monotonic; an estimate may be corrected. */
    f->position_progress = fmax(f->position_progress, p);
    f->display.route_progress = f->position_progress;
    f->estimated = false;
    return true;
}
bool app_tracker_parse_opensky(app_tracker_flight_t *f, const cJSON *root, int64_t now)
{
    const cJSON *row;
    cJSON_ArrayForEach(row, get(root, "states"))
    {
        const cJSON *hex = cJSON_GetArrayItem(row, 0), *call = cJSON_GetArrayItem(row, 1);
        if (!cJSON_IsString(hex) || !cJSON_IsString(call) ||
            !identity(call->valuestring, f->display.tracked_flight_callsign) ||
            (*f->icao24 && strcasecmp(hex->valuestring, f->icao24)))
            continue;
        int64_t contact = app_tracker_timestamp(cJSON_GetArrayItem(row, 4));
        if (!fresh(contact, now, 180) || contact < f->contact_unix)
            continue;
        const cJSON *lat = cJSON_GetArrayItem(row, 6), *lon = cJSON_GetArrayItem(row, 5);
        bool position = number(lat) && number(lon) &&
                        set_position(f, lat->valuedouble, lon->valuedouble,
                                     app_tracker_timestamp(cJSON_GetArrayItem(row, 3)), now);
        /* Bind an aircraft only after the occurrence/route-position checks. */
        if (!*f->icao24 && !position)
            continue;
        if (position)
            copy(f->icao24, sizeof(f->icao24), hex->valuestring);
        f->contact_unix = contact;
        const cJSON *alt = cJSON_GetArrayItem(row, 7), *speed = cJSON_GetArrayItem(row, 9),
                    *heading = cJSON_GetArrayItem(row, 10);
        if (number(alt))
            f->display.tracked_flight_altitude_m = (int)round(alt->valuedouble);
        if (number(speed) && speed->valuedouble >= 0)
            f->display.tracked_flight_speed_kph = (int)round(speed->valuedouble * 3.6);
        if (number(heading) && heading->valuedouble >= 0 && heading->valuedouble <= 360) {
            f->display.tracked_flight_heading_deg = (int)round(heading->valuedouble) % 360;
            f->display.tracked_flight_heading_valid = true;
        }
        const cJSON *vertical = cJSON_GetArrayItem(row, 11);
        if (number(vertical))
            copy(f->display.tracked_flight_phase, sizeof(f->display.tracked_flight_phase),
                 vertical->valuedouble > 1    ? "climbing"
                 : vertical->valuedouble < -1 ? "descending"
                                              : "cruising");
        if (position && cJSON_IsFalse(cJSON_GetArrayItem(row, 8)) && number(speed) &&
            speed->valuedouble > 40) {
            copy(f->display.tracked_flight_journey_phase,
                 sizeof(f->display.tracked_flight_journey_phase), "enroute");
        }
        if (position)
            copy(f->source, sizeof(f->source), "OpenSky");
        return position && number(alt) && number(speed) && number(heading);
    }
    return false;
}
bool app_tracker_parse_position(app_tracker_flight_t *f, const cJSON *root, int64_t now)
{
    const cJSON *p = get(root, "last_position");
    if (!cJSON_IsObject(p))
        p = get(root, "position");
    if (!cJSON_IsObject(p))
        p = root;
    const cJSON *lat = get(p, "latitude"), *lon = get(p, "longitude");
    if (!number(lat) || !number(lon) ||
        !set_position(f, lat->valuedouble, lon->valuedouble,
                      app_tracker_timestamp(get(p, "timestamp")), now))
        return false;
    const cJSON *v = get(p, "altitude");
    if (number(v))
        f->display.tracked_flight_altitude_m = (int)round(v->valuedouble * 30.48);
    v = get(p, "groundspeed");
    if (number(v))
        f->display.tracked_flight_speed_kph = (int)round(v->valuedouble * 1.852);
    v = get(p, "heading");
    if (number(v) && v->valuedouble >= 0 && v->valuedouble <= 360) {
        f->display.tracked_flight_heading_deg = v->valueint % 360;
        f->display.tracked_flight_heading_valid = true;
    }
    copy(f->source, sizeof(f->source), "FlightAware");
    return true;
}
static bool matches_schedule(const app_tracker_flight_t *f, const cJSON *r, bool fa)
{
    if (fa && *f->provider_id && !strcmp(str(r, "fa_flight_id"), f->provider_id))
        return true;
    if (fa && *f->provider_id)
        return false;
    const char *origin = fa ? str(get(r, "origin"), "code_iata") : str(r, "dep_iata");
    const char *dest = fa ? str(get(r, "destination"), "code_iata") : str(r, "arr_iata");
    int64_t dep = fa ? first_time(r, "scheduled_out", "scheduled_off", NULL)
                     : first_time(r, "dep_time_ts", NULL, NULL);
    bool call =
        identity(str(r, fa ? "ident_icao" : "flight_icao"), f->display.tracked_flight_callsign) ||
        identity(str(r, fa ? "ident_iata" : "flight_iata"), f->display.tracked_flight_number);
    return call && identity(origin, f->display.tracked_flight_origin) &&
           identity(dest, f->display.tracked_flight_destination) && dep > 0 &&
           f->scheduled_departure > 0 && llabs(dep - f->scheduled_departure) <= 3 * 3600;
}
bool app_tracker_parse_schedule(app_tracker_flight_t *f, const cJSON *root, bool fa, int64_t now)
{
    const cJSON *rows = get(root, fa ? "flights" : "response"), *r = NULL, *candidate;
    if (!rows && !fa)
        rows = get(root, "data");
    if (cJSON_IsObject(rows)) {
        if (matches_schedule(f, rows, fa))
            r = rows;
    } else
        cJSON_ArrayForEach(candidate, rows)
        {
            if (matches_schedule(f, candidate, fa)) {
                if (r)
                    return false;
                r = candidate;
            }
        }
    if (!r)
        return false;
    app_remote_status_t *d = &f->display;
    int64_t actual_dep = fa ? first_time(r, "actual_off", "actual_out", NULL)
                            : first_time(r, "dep_actual_ts", NULL, NULL);
    int64_t actual_arr = fa ? first_time(r, "actual_on", "actual_in", NULL)
                            : first_time(r, "arr_actual_ts", NULL, NULL);
    int64_t eta = fa ? first_time(r, "estimated_on", "estimated_in", "scheduled_in")
                     : first_time(r, "arr_estimated_ts", "arr_time_ts", NULL);
    int64_t etd = fa ? first_time(r, "estimated_out", "scheduled_out", NULL)
                     : first_time(r, "dep_estimated_ts", "dep_time_ts", NULL);
    if (actual_dep > 0 && actual_dep <= now + 60)
        d->tracked_flight_actual_departure_unix = actual_dep;
    if (etd > 0)
        d->tracked_flight_estimated_departure_unix = etd;
    if (eta > 0)
        d->tracked_flight_estimated_arrival_unix = eta;
    if (actual_arr > 0 && actual_arr <= now + 60)
        d->tracked_flight_actual_arrival_unix = actual_arr;
    copy(d->tracked_flight_origin_terminal, sizeof(d->tracked_flight_origin_terminal),
         str(r, fa ? "terminal_origin" : "dep_terminal"));
    copy(d->tracked_flight_origin_gate, sizeof(d->tracked_flight_origin_gate),
         str(r, fa ? "gate_origin" : "dep_gate"));
    copy(d->tracked_flight_destination_terminal, sizeof(d->tracked_flight_destination_terminal),
         str(r, fa ? "terminal_destination" : "arr_terminal"));
    copy(d->tracked_flight_destination_gate, sizeof(d->tracked_flight_destination_gate),
         str(r, fa ? "gate_destination" : "arr_gate"));
    const char *status = str(r, "status");
    d->tracked_flight_delayed =
        (f->scheduled_departure > 0 &&
         d->tracked_flight_estimated_departure_unix > f->scheduled_departure + 60) ||
        !strncasecmp(status, "delay", 5);
    if (cJSON_IsTrue(get(r, "cancelled")) || !strcasecmp(status, "cancelled") ||
        !strcasecmp(status, "canceled")) {
        copy(d->tracked_flight_status, sizeof(d->tracked_flight_status), "Cancelled");
        d->tracked_flight_valid = false;
    } else if (d->tracked_flight_actual_arrival_unix > 0 || !strcasecmp(status, "landed") ||
               !strcasecmp(status, "arrived")) {
        copy(d->tracked_flight_journey_phase, sizeof(d->tracked_flight_journey_phase), "arrived");
        if (!d->tracked_flight_actual_arrival_unix)
            d->tracked_flight_actual_arrival_unix = eta > 0 && eta < now ? eta : now;
    } else if (d->tracked_flight_actual_departure_unix > 0 || !strcasecmp(status, "en-route") ||
               !strcasecmp(status, "active")) {
        copy(d->tracked_flight_journey_phase, sizeof(d->tracked_flight_journey_phase), "enroute");
    } else if (!app_tracker_airborne(f)) {
        copy(d->tracked_flight_status, sizeof(d->tracked_flight_status),
             d->tracked_flight_delayed ? "Delayed" : "Scheduled");
        copy(f->source, sizeof(f->source), fa ? "FlightAware schedule" : "AirLabs schedule");
    }
    if (fa && !*f->provider_id)
        copy(f->provider_id, sizeof(f->provider_id), str(r, "fa_flight_id"));
    /* AirLabs may establish aircraft identity only on the matched occurrence. */
    if (!fa && !*f->icao24 && strlen(str(r, "hex")) == 6)
        copy(f->icao24, sizeof(f->icao24), str(r, "hex"));
    return true;
}
void app_tracker_reconcile(app_tracker_flight_t *f, int64_t now)
{
    app_remote_status_t *d = &f->display;
    int64_t eta = d->tracked_flight_estimated_arrival_unix;
    if (eta > 0 && now >= eta + 86400)
        d->tracked_flight_valid = false;
    if (app_tracker_terminal(f)) {
        f->estimated = false;
        d->route_progress = 1;
        d->tracked_flight_speed_kph = 0;
        d->tracked_flight_altitude_m = 0;
        copy(d->tracked_flight_phase, sizeof(d->tracked_flight_phase), "landed");
        if (strcasecmp(d->tracked_flight_status, "Cancelled"))
            copy(d->tracked_flight_status, sizeof(d->tracked_flight_status), "Arrived");
        int64_t arrival = d->tracked_flight_actual_arrival_unix;
        if (arrival > 0 && now >= arrival + 7200)
            d->tracked_flight_valid = false;
        return;
    }
    if (!app_tracker_airborne(f)) {
        f->estimated = false;
        d->route_progress = 0;
        // Seed snapshots can contain another occurrence's airborne telemetry.
        // Clear it on load/install too, before the preflight detail window opens.
        d->tracked_flight_altitude_m = 0;
        d->tracked_flight_speed_kph = 0;
        d->tracked_flight_heading_deg = 0;
        d->tracked_flight_heading_valid = false;
        copy(d->tracked_flight_phase, sizeof(d->tracked_flight_phase), "unknown");
        f->position_unix = f->contact_unix = 0;
        f->position_progress = 0;
        return;
    }
    double p = f->position_progress;
    int64_t age = f->position_unix > 0 ? now - f->position_unix : INT64_MAX;
    f->estimated = age > 180;
    if (age >= 0 && age <= 180 && f->position_unix > 0) {
        double km =
            angle(f->origin_lat, f->origin_lon, f->destination_lat, f->destination_lon) * 6371;
        if (km > 0)
            p += fmin(age, 120) * fmax(d->tracked_flight_speed_kph, 0) / 3600.0 / km;
    } else if (eta > 0) {
        int64_t start =
            f->position_unix > 0 ? f->position_unix : d->tracked_flight_actual_departure_unix;
        if (!start)
            start = d->tracked_flight_estimated_departure_unix > 0
                        ? d->tracked_flight_estimated_departure_unix
                        : f->scheduled_departure;
        if (start > 0 && eta > start)
            p += (1 - p) * clamp((double)(now - start) / (eta - start), 0, 1);
    }
    d->route_progress = clamp(p, 0, 0.99); // Estimated arrival never declares touchdown.
    copy(d->tracked_flight_status, sizeof(d->tracked_flight_status),
         f->estimated ? "En route (estimated)" : "En route");
}
int app_tracker_select(const app_tracker_flight_t *f, unsigned count, int current, int64_t now)
{
    if (current >= 0 && (unsigned)current < count && f[current].display.tracked_flight_valid &&
        app_tracker_airborne(&f[current]))
        return current;
    int first = -1, arrived = -1;
    for (unsigned i = 0; i < count; i++) {
        if (!f[i].display.tracked_flight_valid)
            continue;
        if (app_tracker_terminal(&f[i])) {
            if (arrived < 0)
                arrived = i;
            continue;
        }
        if (first < 0 || f[i].scheduled_departure < f[first].scheduled_departure)
            first = i;
    }
    if (first >= 0 && (arrived < 0 || f[first].scheduled_departure <= now))
        return first;
    return arrived >= 0 ? arrived : first;
}
bool app_tracker_schedule_due(const app_tracker_flight_t *f, int64_t now)
{
    return f->display.tracked_flight_valid && !app_tracker_terminal(f) && now >= f->next_schedule;
}

int app_tracker_schedule_interval(const app_tracker_flight_t *f, int64_t now)
{
    if (app_tracker_terminal(f))
        return 7200;
    if (app_tracker_airborne(f)) {
        if (f->display.tracked_flight_estimated_arrival_unix > 0 &&
            now >= f->display.tracked_flight_estimated_arrival_unix - 1800)
            return 300;
        int64_t duration =
            f->display.tracked_flight_estimated_arrival_unix - f->scheduled_departure;
        return duration > 0 && duration < 10800 ? 1800 : 3600;
    }
    int64_t until = f->scheduled_departure - now;
    return until > 86400 ? 21600 : until > 7200 ? 3600 : 600;
}
