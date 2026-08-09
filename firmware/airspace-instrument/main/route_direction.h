// AirESP32ace
// Created by VM, August 2026

#pragma once

#include <stdbool.h>

typedef enum {
    AIRSPACE_ROUTE_DIRECTION_UNCERTAIN = 0,
    AIRSPACE_ROUTE_DIRECTION_AS_REPORTED,
    AIRSPACE_ROUTE_DIRECTION_REVERSED,
} airspace_route_direction_t;

airspace_route_direction_t airspace_route_direction_resolve(
    double aircraft_latitude,
    double aircraft_longitude,
    int aircraft_track_degrees,
    int aircraft_speed_knots,
    bool aircraft_track_valid,
    double reported_origin_latitude,
    double reported_origin_longitude,
    double reported_destination_latitude,
    double reported_destination_longitude
);
