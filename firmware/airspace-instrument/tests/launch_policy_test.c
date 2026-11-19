// AirESP32ace
// Created by VM, August 2026
#include "launches.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reduced from LL2's Flight 14 response on September 28, 2026. */
static const char *flight14 =
    "{\"id\":\"7d1afb26-6f9c-429b-9ccf-29012fd1e519\","
    "\"name\":\"Starship | Starlink Group 31-1 (Starship Flight 14)\","
    "\"net\":\"2026-09-28T12:48:59Z\",\"last_updated\":\"2026-09-28T14:08:06Z\","
    "\"status\":{\"id\":9,\"name\":\"Payload Deployed\",\"abbrev\":\"Deployed\"},"
    "\"net_precision\":{\"name\":\"Second\",\"abbrev\":\"SEC\"},"
    "\"pad\":{\"name\":\"Orbital Launch Pad 2\",\"location\":{\"name\":\"SpaceX Starbase, TX, USA\"}},"
    "\"rocket\":{\"configuration\":{\"full_name\":\"Starship V3\"}}}";

int main(int argc, char **argv)
{
    int64_t now = app_launch_timestamp("2026-09-28T14:09:00Z");
    assert(now > 0);
    assert(app_launch_utc_offset("starbase", now) == -18000);
    assert(app_launch_utc_offset("florida", now) == -14400);
    assert(app_launch_utc_offset("baikonur", now) == 18000);
    assert(app_launch_utc_offset("starbase", app_launch_timestamp("2026-12-01T00:00:00Z")) == -21600);
    assert(app_launch_utc_offset("florida", app_launch_timestamp("2026-03-08T06:59:59Z")) == -18000);
    assert(app_launch_utc_offset("florida", app_launch_timestamp("2026-03-08T07:00:00Z")) == -14400);
    assert(app_launch_timestamp("2026-09-28T14:09:00.123456Z") == now);
    assert(app_launch_timestamp("2026-09-28T14:09:00+00:00") == now);
    assert(!app_launch_timestamp("2026-02-30T14:09:00Z"));
    assert(!app_launch_timestamp("2026-09-28T14:09:00-05:00"));
    assert(!app_launch_timestamp("2026-09-28T14:09:00"));
    assert(!app_launch_timestamp(NULL));
    assert(app_launch_failure_interval(1) == 60);
    assert(app_launch_failure_interval(2) == 300);
    assert(app_launch_failure_interval(6) == 21600);
    assert(app_launch_failure_interval(99) == 21600);
    assert(app_launch_valid_id("7d1afb26-6f9c-429b-9ccf-29012fd1e519"));
    assert(!app_launch_valid_id(NULL));
    assert(!app_launch_valid_id("7d1afb26/6f9c/429b/9ccf/29012fd1e519"));
    cJSON *j = cJSON_Parse(flight14);
    app_launch_entry_t e = {0};
    assert(app_launch_accept(&e, j, APP_REMOTE_LAUNCH_SPACEX, now, false));
    assert(!strcmp(e.launch.mission, "STARLINK GROUP 31-1 (STARSHIP FLIGHT 14)"));
    assert(!strcmp(e.launch.site, "starbase"));
    assert(!strcmp(e.launch.vehicle, "starship"));
    assert(!strcmp(e.launch.precision, "Second"));
    assert(e.status_id == 9 && !e.completed); /* Deployment is not Starship splashdown. */
    assert(!app_launch_stale(&e, now + 600));
    assert(app_launch_stale(&e, now + 601));
    assert(!app_launch_expired(&e, now + 7200));
    assert(app_launch_expired(&e, now + 3 * 86400));
    assert(!app_launch_accept(&e, j, APP_REMOTE_LAUNCH_NASA, now, false));
    app_launch_entry_t before = e;
    cJSON_ReplaceItemInObjectCaseSensitive(j, "last_updated", cJSON_CreateString("2026-07-30T14:54:54Z"));
    assert(!app_launch_accept(&e, j, 2, now, true));
    assert(memcmp(&e, &before, sizeof(e)) == 0);
    cJSON_ReplaceItemInObjectCaseSensitive(j, "last_updated", cJSON_CreateString("2026-09-28T14:09:00Z"));
    cJSON_ReplaceItemInObjectCaseSensitive(j, "id", cJSON_CreateString("0f867f0c-5422-4d44-93b8-d7faaae7019b"));
    assert(!app_launch_accept(&e, j, 2, now, true));
    cJSON_ReplaceItemInObjectCaseSensitive(j, "id", cJSON_CreateString(e.launch.id));
    cJSON *status = cJSON_GetObjectItemCaseSensitive(j, "status");
    cJSON_ReplaceItemInObjectCaseSensitive(status, "id", cJSON_CreateNumber(3));
    assert(app_launch_accept(&e, j, 2, now, true));
    assert(e.completed == now);
    assert(app_launch_accept(&e, j, 2, now + 60, true));
    assert(e.completed == now); /* Repeated refreshes cannot extend retention. */
    assert(!app_launch_expired(&e, now + 7199));
    assert(app_launch_expired(&e, now + 7200));
    cJSON_Delete(j);
    assert(!app_launch_accept(&e, NULL, 2, now, true));
    j = cJSON_Parse("{}");
    assert(!app_launch_accept(&e, j, 2, now, true));
    cJSON_Delete(j);
    int64_t attempts[APP_LAUNCH_REQUESTS_PER_HOUR] = {0};
    assert(app_launch_budget_due(attempts, now) == now);
    for (unsigned i = 0; i < APP_LAUNCH_REQUESTS_PER_HOUR; i++) attempts[i] = now - 1200 + i * 60;
    assert(app_launch_budget_due(attempts, now) == now + 2401);
    int64_t restored[APP_LAUNCH_REQUESTS_PER_HOUR]; memcpy(restored, attempts, sizeof(attempts));
    assert(app_launch_budget_due(restored, now) == app_launch_budget_due(attempts, now));
    assert(app_launch_budget_due(restored, now + 3601) <= now + 3601);
    app_launch_entry_t boundary = {.launch = {.valid=true,.net_unix=now+25*3600},.checked=now};
    assert(app_launch_next_check(&boundary,now)==now+3600);
    assert(!app_launch_stale_for_mask(&boundary,now+3600,7));
    boundary.launch.net_unix=now+6*3600+600;
    assert(app_launch_next_check(&boundary,now)==now+600);
    assert(!app_launch_stale_for_mask(&boundary,now+601,7));
    /* Three simultaneous imminent programs share twelve calls per hour:
     * production pacing, selection and stale thresholds across two hours. */
    app_launch_entry_t near[3]={0};
    memset(attempts,0,sizeof(attempts));
    unsigned taken=0, counts[3]={0};
    for (unsigned a=0; a<3; a++) { near[a].launch.valid=true; near[a].launch.net_unix=now+3600; }
    for (int second=0; second<7200; second+=5) {
        int64_t t=now+second;
        if (app_launch_budget_due(attempts,t)<=t) {
            int selected=-1;
            for (int a=0; a<3; a++) if (near[a].next_check <= t &&
                (selected<0 || near[a].next_check<near[selected].next_check)) selected=a;
            if (selected>=0) {
                attempts[taken++%APP_LAUNCH_REQUESTS_PER_HOUR]=t;
                counts[selected]++;
                near[selected].checked=t;
                near[selected].next_check=app_launch_next_check(&near[selected],t);
            }
        }
        for (unsigned a=0; a<3; a++)
            if (near[a].checked) assert(!app_launch_stale_for_mask(&near[a],t,7));
    }
    assert(taken<=24 && counts[0]>=7 && counts[1]>=7 && counts[2]>=7);
    uint8_t mask = 0;
    j = cJSON_Parse("[\"spacex\",\"nasa\"]"); assert(app_launch_agencies(j, &mask) && mask == 5); cJSON_Delete(j);
    j = cJSON_Parse("[\"unknown\"]"); assert(!app_launch_agencies(j, &mask)); cJSON_Delete(j);
    j = cJSON_Parse("[]"); assert(!app_launch_agencies(j, &mask)); cJSON_Delete(j);
    /* Optional full provider capture: exercise the production parser against
     * the complete normal-mode response, not only our reduced fixture. */
    if (argc > 1) {
        FILE *capture = fopen(argv[1], "rb"); assert(capture);
        assert(!fseek(capture, 0, SEEK_END));
        long size = ftell(capture); assert(size > 0 && size < 96 * 1024);
        rewind(capture);
        char *body = calloc((size_t)size + 1, 1); assert(body);
        assert(fread(body, 1, (size_t)size, capture) == (size_t)size);
        fclose(capture);
        j = cJSON_Parse(body); assert(j); free(body);
        cJSON *results = cJSON_GetObjectItemCaseSensitive(j, "results");
        cJSON *mission = results ? cJSON_GetArrayItem(results, 0) : j;
        memset(&e, 0, sizeof(e));
        assert(app_launch_accept(&e, mission, 2, app_launch_timestamp("2026-09-28T23:59:59Z"), false));
        assert(!strcmp(e.launch.id, "7d1afb26-6f9c-429b-9ccf-29012fd1e519"));
        assert(!strcmp(e.launch.site, "starbase"));
        assert(e.launch.net_unix == app_launch_timestamp("2026-09-28T12:48:59Z"));
        cJSON_Delete(j);
        puts("Full Flight 14 provider capture parsed successfully.");
    }
    puts("Launch policy tests passed: Flight 14, identity, UTC, stale/expiry, completion, quota, preferences.");
}
