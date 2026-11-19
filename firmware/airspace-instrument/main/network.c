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
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "app_config.h"
#include "connectivity_policy.h"

static const char *TAG = "airspace_net";
static app_network_status_t status = {
    .wifi_state = APP_WIFI_IDLE,
    .rssi = 0,
};
static SemaphoreHandle_t status_mutex;
static SemaphoreHandle_t control_mutex;
static app_wifi_retry_t retry;
static bool station_configured; /* successfully installed in the driver */
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

static void initialise_static_status(bool reset_connection)
{
    app_config_t cfg;
    app_config_get(&cfg);
    const app_wifi_profile_t *wifi = app_config_active_wifi_profile(&cfg);

    xSemaphoreTake(status_mutex, portMAX_DELAY);
    status.wifi_configured = wifi && *wifi->ssid;
    if (reset_connection) {
        status.wifi_connected = false;
        status.wifi_state = status.wifi_configured ? APP_WIFI_IDLE : APP_WIFI_NO_CREDENTIALS;
        status.rssi = 0;
        station_configured = false;
        app_wifi_retry_reset(&retry, false, 0);
        copy_string(status.ip, sizeof(status.ip), "0.0.0.0");
    }
    copy_string(status.configured_ssid, sizeof(status.configured_ssid), wifi ? wifi->ssid : "");
    copy_string(status.location_key, sizeof(status.location_key), cfg.location_key);
    copy_string(status.location_label, sizeof(status.location_label), cfg.location_label);
    copy_string(status.latitude, sizeof(status.latitude), cfg.location_lat);
    copy_string(status.longitude, sizeof(status.longitude), cfg.location_lon);
    copy_string(status.timezone, sizeof(status.timezone), cfg.timezone);
    status.aircraft_labels_enabled = cfg.aircraft_labels_enabled;
    status.radar_wedge_enabled = cfg.radar_wedge_enabled;
    copy_string(status.accent_color, sizeof(status.accent_color), app_config_accent_color_name(cfg.accent_color));
    xSemaphoreGive(status_mutex);

    setenv("TZ", cfg.timezone, 1);
    tzset();
}

static void update_local_time(void)
{
    time_t now = 0;
    struct tm local = {0};
    time(&now);
    localtime_r(&now, &local);

    xSemaphoreTake(status_mutex, portMAX_DELAY);
    status.time_synced = local.tm_year >= (2024 - 1900);
    if (status.time_synced) {
        strftime(status.local_time, sizeof(status.local_time), "%a %H:%M:%S", &local);
    } else {
        copy_string(status.local_time, sizeof(status.local_time), "waiting");
    }
    xSemaphoreGive(status_mutex);
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
        xSemaphoreTake(status_mutex, portMAX_DELAY);
        status.wifi_connected = false;
        copy_string(status.ip, sizeof(status.ip), "0.0.0.0");
        status.rssi = 0;
        if (!wifi_suspended && station_configured && status.wifi_configured) {
            status.wifi_state = APP_WIFI_CONNECTING;
            app_wifi_retry_failed(&retry, esp_timer_get_time() / 1000);
        }
        xSemaphoreGive(status_mutex);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return;
        xSemaphoreTake(status_mutex, portMAX_DELAY);
        if (wifi_suspended || !station_configured ||
            strncmp((const char *)ap.ssid, status.configured_ssid, sizeof(ap.ssid)) != 0) {
            xSemaphoreGive(status_mutex);
            return;
        }
        status.wifi_connected = true;
        status.wifi_state = APP_WIFI_CONNECTED;
        snprintf(status.ip, sizeof(status.ip), IPSTR, IP2STR(&event->ip_info.ip));
        status.rssi = ap.rssi;
        app_wifi_retry_reset(&retry, false, 0);
        xSemaphoreGive(status_mutex);
        ESP_LOGI(TAG, "Wi-Fi connected");
        start_sntp_once();
    }
}

static void connect_if_configured(void)
{
    app_config_t cfg;
    app_config_get(&cfg);
    const app_wifi_profile_t *wifi = app_config_active_wifi_profile(&cfg);

    if (!wifi || !*wifi->ssid || wifi_suspended) return;

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, wifi->ssid, strnlen(wifi->ssid, sizeof(wifi_config.sta.ssid)));
    memcpy(wifi_config.sta.password, wifi->password, strnlen(wifi->password, sizeof(wifi_config.sta.password)));
    wifi_config.sta.threshold.authmode = strlen(wifi->password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    xSemaphoreTake(status_mutex, portMAX_DELAY);
    station_configured = result == ESP_OK;
    status.wifi_state = APP_WIFI_CONNECTING;
    if (station_configured) app_wifi_retry_reset(&retry, true, 0);
    else app_wifi_retry_failed(&retry, esp_timer_get_time()/1000);
    xSemaphoreGive(status_mutex);
}

static void service_connection(void)
{
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    xSemaphoreTake(status_mutex, portMAX_DELAY);
    bool due = wifi_started && !wifi_suspended && status.wifi_configured &&
               !status.wifi_connected && app_wifi_retry_begin(&retry, esp_timer_get_time()/1000);
    bool configured = station_configured;
    xSemaphoreGive(status_mutex);
    if (due && !configured) connect_if_configured();
    if (due && configured && esp_wifi_connect() != ESP_OK) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
        if (!status.wifi_connected) app_wifi_retry_failed(&retry, esp_timer_get_time()/1000);
        xSemaphoreGive(status_mutex);
    }
    xSemaphoreGive(control_mutex);
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
    initialise_static_status(true);
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
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    wifi_started = true;
    log_heap("after Wi-Fi start");

    connect_if_configured();
    xSemaphoreGive(control_mutex);

    while (true) {
        service_connection();
        update_local_time();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_network_start(void)
{
    if (status_mutex == NULL) {
        status_mutex = xSemaphoreCreateMutex();
    }
    if (!control_mutex) control_mutex = xSemaphoreCreateMutex();
    if (!status_mutex || !control_mutex) { ESP_LOGE(TAG, "No network mutex"); return; }
    app_config_init_defaults();
    initialise_static_status(true);
    xTaskCreate(network_task, "airspace_net", 8192, NULL, 5, NULL);
}

void app_network_apply_config(bool reconnect_wifi)
{
    if (!control_mutex || !status_mutex) return;
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    initialise_static_status(reconnect_wifi);
    if (reconnect_wifi && wifi_started && !wifi_suspended) {
        esp_wifi_disconnect();
        connect_if_configured();
    }
    xSemaphoreGive(control_mutex);
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
    if (!control_mutex || !status_mutex) return true;
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    if (!wifi_started || wifi_suspended) {
        xSemaphoreGive(control_mutex);
        return true;
    }

    /* Set the guard before stopping the driver. WIFI_EVENT_STA_DISCONNECTED
     * can be delivered as part of esp_wifi_stop(), and must not start the
     * normal reconnect loop while the instrument is asleep. */
    xSemaphoreTake(status_mutex, portMAX_DELAY);
    wifi_suspended = true;
    app_wifi_retry_reset(&retry, false, 0);
    xSemaphoreGive(status_mutex);
    const esp_err_t result = esp_wifi_stop();
    if (result != ESP_OK) {
        xSemaphoreTake(status_mutex, portMAX_DELAY);
        wifi_suspended = false;
        app_wifi_retry_reset(&retry, !status.wifi_connected, 0);
        xSemaphoreGive(status_mutex);
        xSemaphoreGive(control_mutex);
        ESP_LOGW(TAG, "Could not suspend Wi-Fi: %s", esp_err_to_name(result));
        return false;
    }

    xSemaphoreTake(status_mutex, portMAX_DELAY);
    status.wifi_connected = false;
    status.wifi_state = APP_WIFI_IDLE;
    status.rssi = 0;
    copy_string(status.ip, sizeof(status.ip), "0.0.0.0");
    xSemaphoreGive(status_mutex);
    xSemaphoreGive(control_mutex);
    ESP_LOGI(TAG, "Wi-Fi suspended for display sleep");
    return true;
}

bool app_network_resume(void)
{
    if (!control_mutex || !status_mutex) return true;
    xSemaphoreTake(control_mutex, portMAX_DELAY);
    if (!wifi_started || !wifi_suspended) {
        xSemaphoreGive(control_mutex);
        return true;
    }

    const esp_err_t result = esp_wifi_start();
    if (result != ESP_OK) {
        xSemaphoreGive(control_mutex);
        ESP_LOGW(TAG, "Could not resume Wi-Fi: %s", esp_err_to_name(result));
        return false;
    }

    xSemaphoreTake(status_mutex, portMAX_DELAY);
    wifi_suspended = false;
    station_configured = false;
    xSemaphoreGive(status_mutex);
    connect_if_configured();
    xSemaphoreGive(control_mutex);
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
