// AirESP32ace
// Created by VM, August 2026

#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    AIRSPACE_AIRCRAFT_SHAPE_UNKNOWN = 0,
    AIRSPACE_AIRCRAFT_SHAPE_LIGHT_PISTON,
    AIRSPACE_AIRCRAFT_SHAPE_SINGLE_PROP,
    AIRSPACE_AIRCRAFT_SHAPE_TURBOPROP,
    AIRSPACE_AIRCRAFT_SHAPE_REGIONAL_JET,
    AIRSPACE_AIRCRAFT_SHAPE_BUSINESS_JET,
    AIRSPACE_AIRCRAFT_SHAPE_NARROWBODY_TWINJET,
    AIRSPACE_AIRCRAFT_SHAPE_WIDEBODY_TWINJET,
    AIRSPACE_AIRCRAFT_SHAPE_FOUR_ENGINE_WIDEBODY,
    AIRSPACE_AIRCRAFT_SHAPE_HELICOPTER,
} airspace_aircraft_shape_t;

typedef struct {
    const char *code;
    const char *slug;
    const char *label;
    airspace_aircraft_shape_t shape;
    bool exact_code;
} airspace_aircraft_type_match_t;

bool airspace_aircraft_type_lookup(
    const char *typecode,
    const char *model,
    const char *manufacturer,
    airspace_aircraft_type_match_t *match);
bool airspace_aircraft_is_rotorcraft(
    const char *typecode,
    const char *model,
    const char *manufacturer,
    const char *engine_type,
    int opensky_category);
const char *airspace_aircraft_canonical_label(
    const char *typecode,
    const char *model,
    const char *manufacturer);
const char *airspace_aircraft_type_source_text(int source);
