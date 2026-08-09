// AirESP32ace
// Created by VM, August 2026

#include <assert.h>
#include <string.h>

#include "aircraft_type_registry.h"

static void expect_slug(const char *typecode,
                        const char *model,
                        const char *expected_slug)
{
    airspace_aircraft_type_match_t match;
    assert(airspace_aircraft_type_lookup(typecode, model, "", &match));
    assert(strcmp(match.slug, expected_slug) == 0);
}

static void expect_label(const char *typecode,
                         const char *expected_label)
{
    airspace_aircraft_type_match_t match;
    assert(airspace_aircraft_type_lookup(typecode, "", "", &match));
    assert(strcmp(match.label, expected_label) == 0);
}

int main(void)
{
    expect_slug("T204", "", "tupolev-tu-204");
    expect_slug("T214", "", "tupolev-tu-204");
    expect_slug("", "Tupolev Tu-214", "tupolev-tu-204");
    expect_slug("B738", "", "boeing-737");
    expect_slug("B789", "", "boeing-787");
    expect_slug("A20N", "", "airbus-a320");
    expect_label("A359", "Airbus A350-900");
    expect_label("A35K", "Airbus A350-1000");
    expect_slug("BCS3", "", "airbus-a220");
    expect_slug("E75L", "", "embraer-e-jet");
    expect_slug("CRJ9", "", "bombardier-crj");
    expect_slug("DH8D", "", "dash-8");
    expect_slug("AT76", "", "atr-72");
    expect_slug("PC12", "", "pilatus-pc-12");
    expect_slug("GLF6", "", "gulfstream-g650");
    expect_slug("A139", "", "leonardo-aw139");
    expect_slug("MI8", "", "mil-mi-8");

    assert(airspace_aircraft_is_rotorcraft("AS65", "", "", "", 0));
    assert(airspace_aircraft_is_rotorcraft(
        "", "Eurocopter MH-65D Dolphin", "", "", 0));
    assert(airspace_aircraft_is_rotorcraft(
        "", "", "", "Turboshaft", 0));
    assert(airspace_aircraft_is_rotorcraft(
        "", "", "", "", 8));
    assert(!airspace_aircraft_is_rotorcraft(
        "B738", "Boeing 737-800", "Boeing", "Jet", 5));

    airspace_aircraft_type_match_t match;
    assert(!airspace_aircraft_type_lookup("ZZZZ", "", "", &match));
    return 0;
}
