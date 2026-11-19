// AirESP32ace
// Created by VM, August 2026
#include "tracker_storage.h"
#include <string.h>

#define V1_MAGIC 0x54524b31U
#define V2_MAGIC 0x54524b32U
#define V1_BYTES 13464U
#define V1_STRIDE 3360U
#define NONE 65535U

/* V2 writes these fields consecutively in this fixed order. The V1 offsets
 * and widths come from the v0.1.4 ESP32 ELF, not the host compiler. Adding a
 * persisted field or changing a width requires a new codec version. */
#define FIELDS(F) \
 F(display.style,0,4,N) F(display.route_progress,4,4,N) \
 F(display.tracked_flight_valid,8,1,B) F(display.tracked_flight_delayed,9,1,B) \
 F(display.tracked_flight_number,10,16,S) F(display.tracked_flight_callsign,26,16,S) \
 F(display.tracked_flight_airline_iata,42,4,S) F(display.tracked_flight_airline_name,46,40,S) \
 F(display.tracked_flight_aircraft_type,86,16,S) F(display.tracked_flight_origin,102,8,S) \
 F(display.tracked_flight_origin_city,110,28,S) F(display.tracked_flight_origin_country,138,3,S) \
 F(display.tracked_flight_destination,141,8,S) F(display.tracked_flight_destination_city,149,28,S) \
 F(display.tracked_flight_destination_country,177,3,S) F(display.tracked_flight_status,180,56,S) \
 F(display.tracked_flight_duration,236,24,S) F(display.tracked_flight_distance,260,24,S) \
 F(display.tracked_flight_eta,284,16,S) F(display.tracked_flight_elapsed,300,16,S) \
 F(display.tracked_flight_remaining,316,16,S) F(display.tracked_flight_phase,332,16,S) \
 F(display.tracked_flight_altitude_m,348,4,N) F(display.tracked_flight_speed_kph,352,4,N) \
 F(display.tracked_flight_heading_deg,356,4,N) F(display.tracked_flight_heading_valid,360,1,B) \
 F(display.tracked_route_geometry_version,500,4,N) F(display.tracked_route_point_count,504,1,N) \
 F(display.tracked_route_points,506,96,N) F(display.tracked_flight_journey_phase,602,12,S) \
 F(display.tracked_flight_route_distance_km,616,4,N) F(display.tracked_flight_origin_current_utc_offset_s,620,4,N) \
 F(display.tracked_flight_destination_current_utc_offset_s,624,4,N) F(display.tracked_flight_departure_utc_offset_s,628,4,N) \
 F(display.tracked_flight_arrival_utc_offset_s,632,4,N) F(display.tracked_flight_estimated_departure_unix,640,8,N) \
 F(display.tracked_flight_actual_departure_unix,648,8,N) F(display.tracked_flight_estimated_arrival_unix,656,8,N) \
 F(display.tracked_flight_actual_arrival_unix,664,8,N) F(display.tracked_flight_origin_terminal,2977,8,S) \
 F(display.tracked_flight_origin_gate,2985,12,S) F(display.tracked_flight_destination_terminal,2997,8,S) \
 F(display.tracked_flight_destination_gate,3005,12,S) F(display.tracked_flight_slot,3017,1,N) \
 F(plan_id,3024,40,S) F(provider_id,3064,128,S) F(icao24,3192,8,S) \
 F(origin_lat,3200,8,N) F(origin_lon,3208,8,N) F(destination_lat,3216,8,N) \
 F(destination_lon,3224,8,N) F(latitude,3232,8,N) F(longitude,3240,8,N) \
 F(scheduled_departure,3248,8,N) F(position_unix,3256,8,N) F(contact_unix,3264,8,N) \
 F(next_motion,3272,8,N) F(next_schedule,3280,8,N) F(last_fallback,3288,8,N) \
 F(missing_since,3296,8,N) F(checked_unix,3304,8,N) F(position_progress,3312,4,N) \
 F(estimated,3316,1,B) F(source,3317,32,S) F(http_status,3352,4,N) \
 F(origin_zone.at,NONE,96,N) F(origin_zone.offset,NONE,48,N) F(origin_zone.count,NONE,1,N) \
 F(destination_zone.at,NONE,96,N) F(destination_zone.offset,NONE,48,N) F(destination_zone.count,NONE,1,N) \
 F(terminal_notice_unix,NONE,8,N)

enum { N, B, S };
#define CHECK_WIDTH(member,offset,width,kind) \
    _Static_assert(sizeof(((app_tracker_flight_t *)0)->member) == (width), "migrate " #member);
FIELDS(CHECK_WIDTH)
typedef struct { uint16_t ram, old; uint8_t width, kind; } field_t;
#define FIELD(member,offset,width,kind) {offsetof(app_tracker_flight_t, member),offset,width,kind},
static const field_t fields[] = { FIELDS(FIELD) };

static uint32_t digest(uint32_t h, const uint8_t *p, size_t n)
{
    while (n--) h = (h ^ *p++) * 16777619U;
    return h;
}
static bool read_h(FILE *f, void *p, size_t n, uint32_t *h)
{
    if (fread(p, 1, n, f) != n) return false;
    *h = digest(*h, p, n); return true;
}
static bool skip(FILE *f, size_t n, uint32_t *h)
{
    uint8_t buf[192];
    while (n) {
        size_t count = n < sizeof(buf) ? n : sizeof(buf);
        if (!read_h(f, buf, count, h)) return false;
        n -= count;
    }
    return true;
}
static bool write_h(FILE *f, const void *p, size_t n, uint32_t *h)
{
    if (fwrite(p, 1, n, f) != n) return false;
    *h = digest(*h, p, n); return true;
}
bool app_tracker_store_write(FILE *f, const tracker_store_t *s)
{
    if (s->count > APP_TRACKER_SLOTS || s->active < -1 || s->active >= (int)s->count) return false;
    uint32_t header[] = {V2_MAGIC, 2, s->count, (uint32_t)s->active};
    uint32_t h = 2166136261U;
    if (!write_h(f, header, sizeof(header), &h)) return false;
    if (!write_h(f, &s->asset_generation, sizeof(s->asset_generation), &h)) return false;
    for (unsigned j = 0; j < s->count; j++) {
        const uint8_t *row = (const uint8_t *)&s->flights[j];
        for (unsigned i = 0; i < sizeof(fields)/sizeof(*fields); i++)
            if (!write_h(f, row + fields[i].ram, fields[i].width, &h)) return false;
    }
    return fwrite(&h, 1, sizeof(h), f) == sizeof(h);
}
bool app_tracker_store_read(FILE *f, tracker_store_t *s)
{
    uint32_t header[4], h = 2166136261U, stored;
    if (!read_h(f, header, sizeof(header), &h)) return false;
    bool old = header[0] == V1_MAGIC && header[1] == V1_BYTES;
    if ((!old && (header[0] != V2_MAGIC || header[1] != 2)) ||
        header[2] > APP_TRACKER_SLOTS || (header[3] != UINT32_MAX && header[3] >= header[2])) return false;
    memset(s, 0, sizeof(*s));
    s->count = header[2]; s->active = (int32_t)header[3];
    if (!old && !read_h(f, &s->asset_generation, sizeof(s->asset_generation), &h)) return false;
    for (unsigned j = 0; j < s->count; j++) {
        app_tracker_flight_t *flight = &s->flights[j];
        unsigned cursor = 0;
        for (unsigned i = 0; i < sizeof(fields)/sizeof(*fields); i++) {
            const field_t *d = &fields[i];
            if (old && d->old == NONE) continue;
            if (old && !skip(f, d->old - cursor, &h)) return false;
            uint8_t *dest = (uint8_t *)flight + d->ram;
            if (!read_h(f, dest, d->width, &h) || (d->kind == B && *dest > 1) ||
                (d->kind == S && !memchr(dest, 0, d->width))) return false;
            cursor = d->old + d->width;
        }
        if (old && !skip(f, V1_STRIDE - cursor, &h)) return false;
        if (flight->origin_zone.count > APP_TRACKER_ZONE_STEPS ||
            flight->destination_zone.count > APP_TRACKER_ZONE_STEPS ||
            flight->display.tracked_flight_slot >= APP_TRACKER_SLOTS ||
            flight->display.tracked_route_point_count > APP_REMOTE_TRACKED_ROUTE_MAX_POINTS) return false;
        /* Legacy checkedUnix included unsuccessful attempts. A v0.1.4 queue
         * can prove its sample time, but not its last schedule success. */
        if (old) flight->checked_unix = flight->position_unix;
    }
    if (old && !skip(f, (APP_TRACKER_SLOTS - s->count) * V1_STRIDE, &h)) return false;
    if (fread(&stored, 1, sizeof(stored), f) != sizeof(stored) || stored != h) return false;
    if (old && fread(&stored, 1, sizeof(stored), f) != sizeof(stored)) return false; /* tail padding */
    return fgetc(f) == EOF && !ferror(f);
}
