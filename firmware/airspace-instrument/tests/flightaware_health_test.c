// AirESP32ace
// Created by VM, August 2026

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "provider_policy.h"

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

static void check(int code, const char *body, app_api_health_state_t expected)
{
    response_code = code;
    response_body = body;
    unsigned before = requests;
    int result = app_tracker_flightaware_probe_status();
    assert(app_provider_health(result, result == 200) == expected);
    assert(requests == before + 1);
}

int main(void)
{
    assert(app_tracker_flightaware_probe_status() == -1);
    assert(requests == 0); /* Never query the provider without a key. */

    strcpy(saved.flightaware_api_key, "dummy-valid-personal-tier-key");
    check(200, "{\"total_cost\":0}", APP_API_HEALTH_VALID);
    strcpy(saved.flightaware_api_key, "deliberately-invalid-test-key");
    check(401, "{\"title\":\"Invalid API key\"}", APP_API_HEALTH_INVALID);
    check(403, "{\"title\":\"Forbidden\"}", APP_API_HEALTH_INVALID);
    check(429, "{\"title\":\"Rate limited\"}", APP_API_HEALTH_LIMITED);
    check(503, "{}", APP_API_HEALTH_UNAVAILABLE);
    check(0, NULL, APP_API_HEALTH_UNAVAILABLE);
    check(200, NULL, APP_API_HEALTH_UNAVAILABLE);
    check(200, "not json", APP_API_HEALTH_UNAVAILABLE);
    check(200, "null", APP_API_HEALTH_UNAVAILABLE);
    check(200, "[]", APP_API_HEALTH_UNAVAILABLE);
    check(200, "true", APP_API_HEALTH_UNAVAILABLE);

    strcpy(saved.flightaware_api_key, "dummy-valid-personal-tier-key");
    check(200, "{\"total_cost\":0}", APP_API_HEALTH_VALID); /* Invalid -> valid recovery. */
    puts("FlightAware health: valid, invalid, failure, and recovery tests passed");
    return 0;
}
