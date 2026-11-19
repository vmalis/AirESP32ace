// AirESP32ace
// Created by VM, November 2026
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The only commit pointer is inside the durable flight queue. Inactive files
 * can be incomplete or absent without changing the pointers used to render. */
static inline bool app_route_package_next(uint32_t previous, uint32_t required,
                                         uint32_t checked, uint32_t replaced,
                                         uint32_t *next) {
    if (!next || (required & ~0xffffU) || (checked & required) != required ||
        (replaced & ~checked)) return false;
    *next = (previous ^ replaced) & required;
    return true;
}
