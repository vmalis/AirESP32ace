// AirESP32ace
// Created by VM, August 2026

#include "network.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "app_config.h"

static const char *TAG = "airspace_net";
static app_network_status_t status = {
    .wifi_state = APP_WIFI_IDLE,
    .rssi = 0,
};
static SemaphoreHandle_t status_mutex;
static int retry_count;
static bool sntp_started;
static bool wifi_started;
static volatile bool wifi_suspended;

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) {
        return;
    }
    const char *value = src ? src : "";
    size_t len = strlen(value);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, value, len);
    dst[len] = '\0';
}

static void update_status_locked(const app_network_status_t *patch)
{
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    status = *patch;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
}

static app_network_status_t read_status(void)
{
    app_network_status_t copy;
    if (status_mutex != NULL) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
    }
    copy = status;
    if (status_mutex != NULL) {
        xSemaphoreGive(status_mutex);
    }
    return copy;
}

static void log_heap(const char *stage)
{
    ESP_LOGI(TAG, "%s heap: internal=%u KB largest=%u KB dma=%u KB largest_dma=%u KB psram=%u KB",
             stage,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024,
             heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024,
             heap_caps_get_largest_free_block(MALLOC_CAP_DMA) / 1024,
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
}

static void set_wifi_state(app_wifi_state_t state)
{
    app_network_status_t s = read_status();
    s.wifi_state = state;
    update_status_locked(&s);
}

static void initialise_static_status(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    const app_wifi_profile_t *wifi = app_config_active_wifi_profile(&cfg);

    app_network_status_t s = read_status();
    s.wifi_configured = wifi != NULL && strlen(wifi->ssid) > 0;
    s.wifi_connected = false;
    copy_string(s.configured_ssid, sizeof(s.configured_ssid), wifi != NULL ? wifi->ssid : "");
    copy_string(s.ip, sizeof(s.ip), "0.0.0.0");
    copy_string(s.local_time, sizeof(s.local_time), "waiting");
    copy_string(s.location_key, sizeof(s.location_key), cfg.location_key);
    copy_string(s.location_label, sizeof(s.location_label), cfg.location_label);
    copy_string(s.latitude, sizeof(s.latitude), cfg.location_lat);
    copy_string(s.longitude, sizeof(s.longitude), cfg.location_lon);
    copy_string(s.timezone, sizeof(s.timezone), cfg.timezone);
    s.aircraft_labels_enabled = cfg.aircraft_labels_enabled;
    s.radar_wedge_enabled = cfg.radar_wedge_enabled;
    copy_string(s.accent_color, sizeof(s.accent_color), app_config_accent_color_name(cfg.accent_color));
    update_status_locked(&s);

    setenv("TZ", cfg.timezone, 1);
    tzset();
}

static void update_local_time(void)
{
    time_t now = 0;
    struct tm local = {0};
    time(&now);
    localtime_r(&now, &local);

    app_network_status_t s = read_status();
    s.time_synced = local.tm_year >= (2024 - 1900);
    if (s.time_synced) {
        strftime(s.local_time, sizeof(s.local_time), "%a %H:%M:%S", &local);
    } else {
        copy_string(s.local_time, sizeof(s.local_time), "waiting");
    }
    update_status_locked(&s);
}

static void time_sync_cb(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "SNTP time synchronized");
    update_local_time();
}

static void start_sntp_once(void)
{
    if (sntp_started) {
        return;
    }
    sntp_started = true;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        app_network_status_t s = read_status();
        s.wifi_connected = false;
        copy_string(s.ip, sizeof(s.ip), "0.0.0.0");
        s.rssi = 0;
        if (wifi_suspended) {
            s.wifi_state = APP_WIFI_IDLE;
            update_status_locked(&s);
            return;
        }
        s.wifi_state = retry_count < 5 ? APP_WIFI_CONNECTING : APP_WIFI_FAILED;
        update_status_locked(&s);

        if (retry_count < 5 && s.wifi_configured) {
            retry_count++;
            ESP_LOGW(TAG, "Wi-Fi disconnected; retry %d", retry_count);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "Wi-Fi connection failed");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        app_network_status_t s = read_status();
        s.wifi_connected = true;
        s.wifi_state = APP_WIFI_CONNECTED;
        snprintf(s.ip, sizeof(s.ip), IPSTR, IP2STR(&event->ip_info.ip));
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            s.rssi = ap.rssi;
        }
        retry_count = 0;
        update_status_locked(&s);
        ESP_LOGI(TAG, "Wi-Fi connected, IP %s", s.ip);
        start_sntp_once();
    }
}

static void connect_if_configured(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    const app_wifi_profile_t *wifi = app_config_active_wifi_profile(&cfg);

    app_network_status_t s = read_status();
    s.wifi_configured = wifi != NULL && strlen(wifi->ssid) > 0;
    copy_string(s.configured_ssid, sizeof(s.configured_ssid), wifi != NULL ? wifi->ssid : "");
    copy_string(s.location_key, sizeof(s.location_key), cfg.location_key);
    copy_string(s.location_label, sizeof(s.location_label), cfg.location_label);
    copy_string(s.latitude, sizeof(s.latitude), cfg.location_lat);
    copy_string(s.longitude, sizeof(s.longitude), cfg.location_lon);
    copy_string(s.timezone, sizeof(s.timezone), cfg.timezone);
    s.aircraft_labels_enabled = cfg.aircraft_labels_enabled;
    s.radar_wedge_enabled = cfg.radar_wedge_enabled;
    copy_string(s.accent_color, sizeof(s.accent_color), app_config_accent_color_name(cfg.accent_color));
    update_status_locked(&s);

    setenv("TZ", cfg.timezone, 1);
    tzset();

    if (!s.wifi_configured) {
        s.wifi_state = APP_WIFI_NO_CREDENTIALS;
        update_status_locked(&s);
        ESP_LOGW(TAG, "No Wi-Fi SSID configured");
        return;
    }

    wifi_config_t wifi_config = {0};
    copy_string((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), wifi->ssid);
    copy_string((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), wifi->password);
    wifi_config.sta.threshold.authmode = strlen(wifi->password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    retry_count = 0;
    set_wifi_state(APP_WIFI_CONNECTING);
    ESP_ERROR_CHECK(esp_wifi_connect());
    ESP_LOGI(TAG, "Connecting to Wi-Fi profile '%s' SSID '%s'",
             app_config_wifi_profile_name(cfg.active_wifi_profile),
             wifi->ssid);
}

static void network_task(void *arg)
{
    (void)arg;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    app_config_init_defaults();
    ESP_ERROR_CHECK(app_config_load());
    initialise_static_status();
    log_heap("before Wi-Fi");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.static_rx_buf_num = 2;
    cfg.dynamic_rx_buf_num = 8;
    cfg.static_tx_buf_num = 4;
    cfg.cache_tx_buf_num = 4;
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_started = true;
    log_heap("after Wi-Fi start");

    connect_if_configured();

    while (true) {
        update_local_time();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_network_start(void)
{
    if (status_mutex == NULL) {
        status_mutex = xSemaphoreCreateMutex();
    }
    app_config_init_defaults();
    initialise_static_status();
    xTaskCreate(network_task, "airspace_net", 8192, NULL, 5, NULL);
}

void app_network_apply_config(bool reconnect_wifi)
{
    if (!reconnect_wifi) {
        app_config_t cfg;
        app_config_get(&cfg);
        const app_wifi_profile_t *wifi = app_config_active_wifi_profile(&cfg);

        app_network_status_t s = read_status();
        s.wifi_configured = wifi != NULL && strlen(wifi->ssid) > 0;
        copy_string(s.configured_ssid, sizeof(s.configured_ssid), wifi != NULL ? wifi->ssid : "");
        copy_string(s.location_key, sizeof(s.location_key), cfg.location_key);
        copy_string(s.location_label, sizeof(s.location_label), cfg.location_label);
        copy_string(s.latitude, sizeof(s.latitude), cfg.location_lat);
        copy_string(s.longitude, sizeof(s.longitude), cfg.location_lon);
        copy_string(s.timezone, sizeof(s.timezone), cfg.timezone);
        s.aircraft_labels_enabled = cfg.aircraft_labels_enabled;
        s.radar_wedge_enabled = cfg.radar_wedge_enabled;
        copy_string(s.accent_color, sizeof(s.accent_color), app_config_accent_color_name(cfg.accent_color));
        update_status_locked(&s);

        setenv("TZ", cfg.timezone, 1);
        tzset();
        return;
    }

    initialise_static_status();
    if (!wifi_started) {
        return;
    }

    esp_wifi_disconnect();
    connect_if_configured();
}

void app_network_get_status(app_network_status_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = read_status();
}

bool app_network_suspend(void)
{
    if (!wifi_started || wifi_suspended) {
        return true;
    }

    /* Set the guard before stopping the driver. WIFI_EVENT_STA_DISCONNECTED
     * can be delivered as part of esp_wifi_stop(), and must not start the
     * normal reconnect loop while the instrument is asleep. */
    wifi_suspended = true;
    retry_count = 0;
    const esp_err_t result = esp_wifi_stop();
    if (result != ESP_OK) {
        wifi_suspended = false;
        ESP_LOGW(TAG, "Could not suspend Wi-Fi: %s", esp_err_to_name(result));
        return false;
    }

    app_network_status_t s = read_status();
    s.wifi_connected = false;
    s.wifi_state = APP_WIFI_IDLE;
    s.rssi = 0;
    copy_string(s.ip, sizeof(s.ip), "0.0.0.0");
    update_status_locked(&s);
    ESP_LOGI(TAG, "Wi-Fi suspended for display sleep");
    return true;
}

bool app_network_resume(void)
{
    if (!wifi_started || !wifi_suspended) {
        return true;
    }

    wifi_suspended = false;
    const esp_err_t result = esp_wifi_start();
    if (result != ESP_OK) {
        wifi_suspended = true;
        ESP_LOGW(TAG, "Could not resume Wi-Fi: %s", esp_err_to_name(result));
        return false;
    }

    connect_if_configured();
    ESP_LOGI(TAG, "Wi-Fi resumed after display sleep");
    return true;
}

const char *app_wifi_state_text(app_wifi_state_t state)
{
    switch (state) {
    case APP_WIFI_IDLE:
        return "idle";
    case APP_WIFI_NO_CREDENTIALS:
        return "no credentials";
    case APP_WIFI_CONNECTING:
        return "connecting";
    case APP_WIFI_CONNECTED:
        return "connected";
    case APP_WIFI_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}
