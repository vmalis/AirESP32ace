// AirESP32ace
// Created by VM, August 2026

#include <assert.h>

#include "route_direction.h"

int main(void)
{
    /* ADSBDB currently reports AAL2799 SFO>MIA. A westbound live track
       between the cities must correct that to MIA>SFO. */
    assert(airspace_route_direction_resolve(
               33.0,
               -101.0,
               295,
               440,
               true,
               37.618999,
               -122.375,
               25.793200,
               -80.290604) == AIRSPACE_ROUTE_DIRECTION_REVERSED);

    assert(airspace_route_direction_resolve(
               33.0,
               -101.0,
               112,
               440,
               true,
               37.618999,
               -122.375,
               25.793200,
               -80.290604) == AIRSPACE_ROUTE_DIRECTION_AS_REPORTED);

    assert(airspace_route_direction_resolve(
               33.0,
               -101.0,
               295,
               35,
               true,
               37.618999,
               -122.375,
               25.793200,
               -80.290604) == AIRSPACE_ROUTE_DIRECTION_UNCERTAIN);

    assert(airspace_route_direction_resolve(
               33.0,
               -101.0,
               295,
               440,
               false,
               37.618999,
               -122.375,
               25.793200,
               -80.290604) == AIRSPACE_ROUTE_DIRECTION_UNCERTAIN);
    return 0;
}
