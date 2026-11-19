// AirESP32ace
// Created by VM, August 2026

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

typedef struct {
    char flightaware_api_key[128];
} app_config_t;

static app_config_t saved;
static int response_code;
static const char *response_body;
static unsigned requests;

static void app_config_get(app_config_t *cfg)
{
    *cfg = saved;
}

static cJSON *request(const char *url, const char *key, int *code)
{
    /* Exercise the actual firmware function, including endpoint and key use. */
    assert(strcmp(url, "https://aeroapi.flightaware.com/aeroapi/account/usage") == 0);
    assert(strcmp(key, saved.flightaware_api_key) == 0);
    requests++;
    *code = response_code;
    return response_body ? cJSON_Parse(response_body) : NULL;
}

#include "flightaware_health_under_test.inc"

static void check(int code, const char *body, bool expected)
{
    response_code = code;
    response_body = body;
    unsigned before = requests;
    assert(app_tracker_validate_flightaware() == expected);
    assert(requests == before + 1);
}

int main(void)
{
    assert(!app_tracker_validate_flightaware());
    assert(requests == 0); /* Never query the provider without a key. */

    strcpy(saved.flightaware_api_key, "dummy-valid-personal-tier-key");
    check(200, "{\"total_cost\":0}", true);
    strcpy(saved.flightaware_api_key, "deliberately-invalid-test-key");
    check(401, "{\"title\":\"Invalid API key\"}", false);
    check(403, "{\"title\":\"Forbidden\"}", false);
    check(429, "{\"title\":\"Rate limited\"}", false);
    check(503, "{}", false);
    check(0, NULL, false);
    check(200, NULL, false);
    check(200, "not json", false);
    check(200, "null", false);
    check(200, "[]", false);
    check(200, "true", false);

    strcpy(saved.flightaware_api_key, "dummy-valid-personal-tier-key");
    check(200, "{\"total_cost\":0}", true); /* Invalid -> valid recovery. */
    puts("FlightAware health: valid, invalid, failure, and recovery tests passed");
    return 0;
}
