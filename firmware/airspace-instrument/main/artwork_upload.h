// AirESP32ace
// Created by VM, November 2026
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ART_OK, ART_BAD_CONTENT, ART_NO_SPACE, ART_VOLUME_UNAVAILABLE,
    ART_MEDIA_IO, ART_CONFLICT, ART_TIMEOUT, ART_DISCONNECTED
} app_art_result_t;
typedef struct {
    uint64_t free_bytes, needed_bytes;
    size_t received;
    int sys_errno;
} app_art_report_t;
typedef struct {
    void *ctx;
    /* Return zero only with fresh, validated volume geometry. */
    int (*capacity)(void *ctx, uint64_t *available, uint32_t *cluster);
    /* Return positive bytes, 0 for a closed peer, -ETIMEDOUT for timeout. */
    int (*receive)(void *ctx, uint8_t *buf, size_t max);
    /* Optional, brief renderer lock: never held over the socket or staging. */
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
} app_art_io_t;
bool app_artwork_required(size_t size, uint32_t cluster, uint64_t *needed);
app_art_result_t app_artwork_recover(const char *path);
app_art_result_t app_artwork_install(const char *path, size_t size, bool known_hash,
    uint32_t expected_hash, const app_art_io_t *io, app_art_report_t *report);
