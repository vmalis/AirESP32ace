// AirESP32ace
// Created by VM, August 2026

#pragma once

#include <stdbool.h>

typedef enum {
    APP_WIFI_IDLE = 0,
    APP_WIFI_NO_CREDENTIALS,
    APP_WIFI_CONNECTING,
    APP_WIFI_CONNECTED,
    APP_WIFI_FAILED,
} app_wifi_state_t;

typedef struct {
    app_wifi_state_t wifi_state;
    bool wifi_configured;
    bool wifi_connected;
    char configured_ssid[33];
    char ip[16];
    int rssi;
    bool time_synced;
    char local_time[32];
    char location_key[16];
    char location_label[48];
    char latitude[20];
    char longitude[20];
    char timezone[64];
    bool aircraft_labels_enabled;
    bool radar_wedge_enabled;
    char accent_color[12];
} app_network_status_t;

void app_network_start(void);
void app_network_get_status(app_network_status_t *status);
void app_network_apply_config(bool reconnect_wifi);
bool app_network_suspend(void);
bool app_network_resume(void);
const char *app_wifi_state_text(app_wifi_state_t state);
