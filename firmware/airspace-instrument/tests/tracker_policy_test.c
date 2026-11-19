// AirESP32ace
// Created by VM, August 2026

#include "tracker.h"
#include "tracker_storage.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static app_tracker_flight_t flight(void)
{
    app_tracker_flight_t f = {0};
    f.origin_lat = 37.6213;
    f.origin_lon = -122.3790;
    f.destination_lat = 37.4602;
    f.destination_lon = 126.4407;
    f.scheduled_departure = 1789252560;
    f.display.tracked_flight_valid = true;
    f.display.tracked_flight_actual_departure_unix = f.scheduled_departure;
    f.display.tracked_flight_estimated_arrival_unix = f.scheduled_departure + 12 * 3600 + 33 * 60;
    strcpy(f.display.tracked_flight_number, "YP114");
    strcpy(f.display.tracked_flight_callsign, "APZ114");
    strcpy(f.display.tracked_flight_origin, "SFO");
    strcpy(f.display.tracked_flight_destination, "ICN");
    strcpy(f.display.tracked_flight_journey_phase, "enroute");
    return f;
}
static cJSON *state(double lat, double lon, int64_t position, int64_t contact, const char *call)
{
    char json[512];
    snprintf(json, sizeof(json),
             "{\"states\":[[\"71aa11\",\"%s\",null,%lld,%lld,%.9f,%.9f,10973,false,281.94,264,0]]}",
             call, (long long)position, (long long)contact, lon, lat);
    cJSON *r = cJSON_Parse(json);
    assert(r);
    return r;
}
static void durable_queue(void)
{
    /* Synthetic binary fixture at the v0.1.4 Xtensa offsets (host size_t
     * differs); includes an inactive slot and persisted paid-call cooldown. */
    unsigned char legacy[13464] = {0};
    uint32_t header[] = {0x54524b31, sizeof(legacy), 1, 0};
    memcpy(legacy, header, sizeof(header));
    uint8_t *row = legacy + 16;
    row[8] = 1;
    strcpy((char *)row + 10, "XX101");
    strcpy((char *)row + 3024, "fixture-plan");
    strcpy((char *)row + 2985, "A12");
    row[3017] = 2;
    int64_t cooldown = 1789000000;
    memcpy(row + 3288, &cooldown, 8);
    uint32_t h = 2166136261U;
    for (unsigned i=0; i<13456; i++) h = (h ^ legacy[i]) * 16777619U;
    memcpy(legacy+13456,&h,4);
    FILE *file = tmpfile(); assert(file);
    assert(fwrite(legacy,1,sizeof(legacy),file) == sizeof(legacy)); rewind(file);
    tracker_store_t s, roundtrip;
    assert(app_tracker_store_read(file,&s)); fclose(file);
    assert(s.count == 1 && s.active == 0 && s.flights[0].last_fallback == cooldown);
    assert(s.flights[0].display.tracked_flight_slot == 2 &&
           !strcmp(s.flights[0].display.tracked_flight_origin_gate,"A12"));
    s.flights[0].origin_zone = (app_tracker_zone_t){.count=2,.at={1, 1000},.offset={3600,7200}};
    s.asset_generation = UINT32_MAX;
    file = tmpfile(); assert(file && app_tracker_store_write(file,&s)); rewind(file);
    assert(app_tracker_store_read(file,&roundtrip)); assert(!memcmp(&s,&roundtrip,sizeof(s)));
    assert(fseek(file, 42, SEEK_SET)==0); fputc(0x10,file); rewind(file);
    assert(!app_tracker_store_read(file,&roundtrip)); fclose(file);
    file = tmpfile(); assert(file); assert(fwrite(legacy,1,100,file)==100); rewind(file);
    assert(!app_tracker_store_read(file,&roundtrip)); fclose(file);
}
int main(void)
{
    durable_queue();
    app_tracker_flight_t f = flight();
    app_tracker_flight_t repair = flight();
    f.display.route_progress = 0.6;
    f.next_schedule = 12345;
    strcpy(repair.display.tracked_flight_origin_city, "San Francisco");
    strcpy(repair.display.tracked_flight_destination_city, "Seoul");
    repair.display.tracked_flight_slot = 2;
    repair.display.tracked_route_point_count = 2;
    repair.display.tracked_route_points[0][0] = 42;
    app_tracker_preserve_runtime(&repair, &f);
    assert(!strcmp(repair.display.tracked_flight_origin_city, "San Francisco"));
    assert(!strcmp(repair.display.tracked_flight_destination_city, "Seoul"));
    assert(repair.display.route_progress == 0.6f);
    assert(repair.next_schedule == 12345);
    assert(repair.display.tracked_flight_slot == 2);
    assert(repair.display.tracked_route_points[0][0] == 42);
    f = flight(); // An incomplete re-sync must not erase already-known cities.
    app_tracker_preserve_runtime(&f, &repair);
    assert(!strcmp(f.display.tracked_flight_destination_city, "Seoul"));
    f = flight();
    // Bidirectional Pacific, short hops, and Southern Hemisphere geometry.
    const double routes[][4] = {{37.62, -122.38, 37.46, 126.44},
                                {37.46, 126.44, 37.62, -122.38},
                                {37.36, -121.92, 32.73, -117.19},
                                {-33.94, 151.18, -33.39, -70.79},
                                {40.64, -73.77, -33.97, 18.60}};
    for (unsigned i = 0; i < sizeof(routes) / sizeof(*routes); i++) {
        f.origin_lat = routes[i][0];
        f.origin_lon = routes[i][1];
        f.destination_lat = routes[i][2];
        f.destination_lon = routes[i][3];
        for (int step = 0; step <= 10; step++) {
            double lat, lon, p = step / 10.0;
            app_tracker_route_coordinate(&f, p, &lat, &lon);
            assert(app_tracker_coordinates_valid(lat, lon));
            assert(fabs(app_tracker_progress(&f, lat, lon) - p) < 0.00001);
        }
    }
    f = flight();
    int64_t now = f.scheduled_departure + 6 * 3600 + 16 * 60;
    app_tracker_reconcile(&f, now);
    assert(f.display.route_progress > .49 && f.display.route_progress < .51 && f.estimated);
    assert(!app_tracker_terminal(&f));
    double lat, lon;
    app_tracker_route_coordinate(&f, .45, &lat, &lon);
    cJSON *r = state(lat, lon, now, now, "APZ114 ");
    assert(app_tracker_parse_opensky(&f, r, now));
    cJSON_Delete(r);
    assert(!f.estimated && f.display.route_progress > .44 && f.display.route_progress < .46);
    assert(f.display.tracked_flight_altitude_m == 10973 &&
           f.display.tracked_flight_speed_kph == 1015);
    // Fresh radio contact must not refresh a stale position timestamp.
    r = state(lat, lon, now - 600, now + 60, "APZ114");
    assert(!app_tracker_parse_opensky(&f, r, now + 60));
    cJSON_Delete(r);
    assert(f.position_unix == now);
    r = state(lat, lon, now + 60, now + 60, "APZ115");
    assert(!app_tracker_parse_opensky(&f, r, now + 60));
    cJSON_Delete(r);
    r = state(lat, lon, now + 60, now + 60, "APZ114");
    cJSON *row = cJSON_GetArrayItem(cJSON_GetObjectItem(r, "states"), 0);
    cJSON_ReplaceItemInArray(row, 7, cJSON_CreateNull());
    assert(!app_tracker_parse_opensky(&f, r, now + 60));
    cJSON_Delete(r);
    assert(f.display.tracked_flight_altitude_m == 10973 && f.position_unix == now + 60);
    app_tracker_reconcile(&f, now + 3600);
    assert(f.display.route_progress > .45 && f.display.route_progress < 1 && f.estimated);
    app_tracker_reconcile(&f, f.display.tracked_flight_estimated_arrival_unix + 600);
    assert(!app_tracker_terminal(&f) && f.display.route_progress < 1);
    app_tracker_reconcile(&f, f.display.tracked_flight_estimated_arrival_unix + 86400);
    assert(!f.display.tracked_flight_valid);
    // A scheduled flight never departs just because its countdown reached zero.
    f = flight();
    f.display.tracked_flight_actual_departure_unix = 0;
    strcpy(f.display.tracked_flight_journey_phase, "preflight");
    f.display.tracked_flight_altitude_m = 5193;
    f.display.tracked_flight_speed_kph = 750;
    f.display.tracked_flight_heading_valid = true;
    strcpy(f.display.tracked_flight_phase, "climbing");
    f.position_unix = now - 10;
    f.position_progress = 0.4f;
    app_tracker_reconcile(&f, now);
    assert(f.display.route_progress == 0 && !app_tracker_airborne(&f));
    assert(f.display.tracked_flight_altitude_m == 0);
    assert(f.display.tracked_flight_speed_kph == 0);
    assert(!f.display.tracked_flight_heading_valid);
    assert(!strcmp(f.display.tracked_flight_phase, "unknown"));
    assert(f.position_unix == 0 && f.position_progress == 0);
    // Schedule checks continue during the days-ahead countdown, but stay sparse.
    assert(app_tracker_schedule_due(&f, f.scheduled_departure - 48 * 3600));
    assert(app_tracker_schedule_interval(&f, f.scheduled_departure - 48 * 3600) == 21600);
    assert(app_tracker_schedule_interval(&f, f.scheduled_departure - 12 * 3600) == 3600);
    assert(app_tracker_schedule_interval(&f, f.scheduled_departure - 3600) == 600);
    f.next_schedule = now + 600;
    assert(!app_tracker_schedule_due(&f, now));
    assert(app_tracker_schedule_due(&f, now + 600));
    // A matched delay changes ETD and the countdown flag; recovery clears both.
    strcpy(f.provider_id, "APZ114-delay-test");
    r = cJSON_Parse("{\"flights\":[{\"fa_flight_id\":\"APZ114-delay-test\","
                    "\"estimated_out\":\"2026-09-12T23:36:00Z\",\"status\":\"Scheduled\"}]}");
    assert(app_tracker_parse_schedule(&f, r, true, f.scheduled_departure - 7200));
    cJSON_Delete(r);
    assert(f.display.tracked_flight_delayed);
    assert(!strcmp(f.display.tracked_flight_status, "Delayed"));
    assert(f.display.tracked_flight_estimated_departure_unix == f.scheduled_departure + 3600);
    r = cJSON_Parse("{\"flights\":[{\"fa_flight_id\":\"APZ114-delay-test\","
                    "\"estimated_out\":\"2026-09-12T22:36:00Z\",\"status\":\"Scheduled\"}]}");
    assert(app_tracker_parse_schedule(&f, r, true, f.scheduled_departure - 7200));
    cJSON_Delete(r);
    assert(!f.display.tracked_flight_delayed);
    assert(!strcmp(f.display.tracked_flight_status, "Scheduled"));
    app_tracker_reconcile(&f, f.display.tracked_flight_estimated_arrival_unix + 86400);
    assert(!f.display.tracked_flight_valid); // A missed departure cannot block the queue forever.
    f = flight();
    strcpy(f.provider_id, "APZ114-exact-occurrence");
    r = cJSON_Parse("{\"flights\":[{\"fa_flight_id\":\"APZ114-yesterday\",\"actual_in\":\"2026-09-"
                    "13T11:09:00Z\"}]}");
    assert(!app_tracker_parse_schedule(&f, r, true, now));
    cJSON_Delete(r);
    r = cJSON_Parse("{\"response\":[]}");
    assert(!app_tracker_parse_schedule(&f, r, false, now));
    cJSON_Delete(r);
    r = cJSON_Parse("{\"response\":{\"flight_icao\":\"APZ114\",\"dep_iata\":\"SFO\",\"arr_iata\":"
                    "\"ICN\",\"dep_time_ts\":1788647760,\"status\":\"landed\"}}");
    assert(!app_tracker_parse_schedule(&f, r, false, now));
    cJSON_Delete(r);
    r = cJSON_Parse("\"2026-09-12T22:36:00Z\"");
    assert(app_tracker_timestamp(r) == f.scheduled_departure);
    cJSON_Delete(r);
    r = cJSON_Parse("{\"flights\":[{\"fa_flight_id\":\"APZ114-exact-occurrence\",\"actual_in\":"
                    "\"2026-09-13T11:09:00Z\"}]}");
    int64_t arrival = f.display.tracked_flight_estimated_arrival_unix;
    assert(app_tracker_parse_schedule(&f, r, true, arrival + 60));
    cJSON_Delete(r);
    app_tracker_reconcile(&f, arrival + 60);
    assert(app_tracker_terminal(&f) && f.display.route_progress == 1);
    assert(f.display.tracked_flight_speed_kph == 0);
    app_tracker_flight_t queue[2] = {f, flight()};
    queue[1].scheduled_departure = arrival + 3600;
    queue[1].display.tracked_flight_actual_departure_unix = 0;
    strcpy(queue[1].display.tracked_flight_journey_phase, "preflight");
    assert(app_tracker_select(queue, 2, 0, arrival - 1) == 0);
    assert(app_tracker_select(queue, 2, 0, arrival + 60) == 1);
    assert(app_tracker_select(queue, 2, 0, arrival + 3600) == 1);
    queue[0] = flight(); // An active flight cannot be displaced by the next timetable entry.
    assert(app_tracker_select(queue, 2, 0, arrival + 3600) == 0);
    f = flight();
    f.display.tracked_flight_actual_arrival_unix = arrival;
    strcpy(f.display.tracked_flight_journey_phase, "arrived");
    app_tracker_reconcile(&f, arrival + 7199);
    assert(f.display.tracked_flight_valid);
    app_tracker_reconcile(&f, arrival + 7200);
    assert(!f.display.tracked_flight_valid);
    // Four upcoming trips must select the earliest departure immediately,
    // even when the instrument has been off for days and none is airborne.
    app_tracker_flight_t upcoming[4];
    for (unsigned i = 0; i < 4; i++) {
        upcoming[i] = flight();
        upcoming[i].scheduled_departure = now + (7 + 2 * i) * 86400;
        upcoming[i].display.tracked_flight_estimated_departure_unix =
            upcoming[i].scheduled_departure;
        upcoming[i].display.tracked_flight_estimated_arrival_unix =
            upcoming[i].scheduled_departure + 12 * 3600;
        upcoming[i].display.tracked_flight_actual_departure_unix = 0;
        strcpy(upcoming[i].display.tracked_flight_journey_phase, "preflight");
        app_tracker_reconcile(&upcoming[i], now);
        assert(upcoming[i].display.tracked_flight_valid);
        assert(!app_tracker_airborne(&upcoming[i]));
        assert(upcoming[i].display.route_progress == 0);
    }
    assert(app_tracker_select(upcoming, 4, -1, now) == 0);
    assert(upcoming[0].display.tracked_flight_estimated_departure_unix - now == 7 * 86400);
    assert(app_tracker_select(upcoming, 4, 3, now) == 0);
    f = upcoming[0];
    strcpy(f.provider_id, "matched");
    strcpy(f.display.tracked_flight_origin_gate, "B7");
    strcpy(f.display.tracked_flight_destination_gate, "B8");
    unsigned fields = 0;
    r = cJSON_Parse("{\"flights\":[{\"fa_flight_id\":\"matched\",\"gate_origin\":null}]}");
    assert(app_tracker_parse_schedule_fields(&f,r,true,now,&fields)); cJSON_Delete(r);
    assert(fields == 1 && !*f.display.tracked_flight_origin_gate && !strcmp(f.display.tracked_flight_destination_gate,"B8"));
    r = cJSON_Parse("{\"flights\":[{\"fa_flight_id\":\"matched\",\"cancelled\":true}]}");
    assert(app_tracker_parse_schedule(&f,r,true,now)); cJSON_Delete(r);
    app_tracker_reconcile(&f,now+7199);
    assert(f.display.tracked_flight_valid && app_tracker_terminal(&f) && !app_tracker_airborne(&f));
    assert(!strcmp(f.display.tracked_flight_status,"Cancelled") && f.display.route_progress==0);
    assert(!f.display.tracked_flight_actual_arrival_unix && !app_tracker_schedule_due(&f,now+60));
    assert(app_tracker_select(&f,1,-1,now)==0);
    app_tracker_reconcile(&f,now+7200); assert(!f.display.tracked_flight_valid);
    f = flight(); strcpy(f.provider_id,"diversion-test");
    r = cJSON_Parse("{\"flights\":[{\"fa_flight_id\":\"diversion-test\",\"diverted\":true}]}");
    assert(app_tracker_parse_schedule(&f,r,true,now)); cJSON_Delete(r);
    app_tracker_reconcile(&f,now); assert(!f.display.route_progress && !app_tracker_terminal(&f) && app_tracker_diverted(&f));
    assert(!strcmp(f.display.tracked_flight_status,"Diverted"));
    r = cJSON_Parse("{\"flights\":[{\"fa_flight_id\":\"diversion-test\",\"actual_on\":1789252700}]}");
    assert(app_tracker_parse_schedule(&f,r,true,now)); cJSON_Delete(r);
    app_tracker_reconcile(&f,now); assert(!f.display.route_progress && app_tracker_terminal(&f));
    assert(!strcmp(f.display.tracked_flight_status,"Landed after diversion"));
    app_tracker_flight_t fresh_plan = flight(); assert(!app_tracker_diverted(&fresh_plan));
    f = upcoming[0];
    r = cJSON_Parse("[[1789000000,-18000],[1793512800,-21600]]");
    assert(app_tracker_decode_zone(r,&f.origin_zone)); cJSON_Delete(r);
    f.display.tracked_flight_estimated_departure_unix=1793512799;
    app_tracker_reconcile(&f,1793512800);
    assert(f.display.tracked_flight_origin_current_utc_offset_s==-21600);
    assert(f.display.tracked_flight_departure_utc_offset_s==-18000);
    f.display.tracked_flight_estimated_departure_unix++;
    app_tracker_reconcile(&f,1793512800);
    assert(f.display.tracked_flight_departure_utc_offset_s==-21600);
    puts("Tracker policy tests passed (geometry, stale samples, identity, estimation, lifecycle).");
}
