// AirESP32ace
// Created by VM, August 2026

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "esp_err.h"

#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_DMA 0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0

static inline void *heap_caps_malloc(size_t size, unsigned caps)
{
    (void)caps;
    return malloc(size);
}

static inline void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    (void)caps;
    return calloc(count, size);
}

static inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, unsigned caps)
{
    (void)caps;
    void *pointer = NULL;
    return posix_memalign(&pointer, alignment, size) == 0 ? pointer : NULL;
}

static inline void heap_caps_free(void *pointer)
{
    free(pointer);
}

static inline unsigned heap_caps_get_free_size(unsigned caps)
{
    (void)caps;
    return 512U * 1024U * 1024U;
}

static inline unsigned heap_caps_get_total_size(unsigned caps)
{
    return heap_caps_get_free_size(caps);
}

static inline unsigned heap_caps_get_largest_free_block(unsigned caps)
{
    return heap_caps_get_free_size(caps);
}

static inline int64_t esp_timer_get_time(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (int64_t)value.tv_sec * 1000000LL + value.tv_nsec / 1000LL;
}

typedef unsigned UBaseType_t;

static inline UBaseType_t uxTaskGetStackHighWaterMark(void *task)
{
    (void)task;
    return 1024U * 1024U;
}

typedef int gpio_num_t;
typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;

#define GPIO_NUM_0 0
#define GPIO_MODE_INPUT 0
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0

static inline esp_err_t gpio_config(const gpio_config_t *config)
{
    (void)config;
    return ESP_OK;
}

static inline int gpio_get_level(gpio_num_t pin)
{
    (void)pin;
    return 1;
}

#define ESP_ERROR_CHECK(expression) do { \
    if ((expression) != ESP_OK) abort(); \
} while (0)

#define ESP_LOGI(tag, format, ...) \
    fprintf(stderr, "[%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) \
    fprintf(stderr, "[%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, format, ...) \
    fprintf(stderr, "[%s] " format "\n", tag, ##__VA_ARGS__)

void airspace_desktop_set_asset_root(const char *asset_root);
