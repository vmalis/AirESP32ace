// AirESP32ace
// Created by VM, August 2026

#include "route_direction.h"

#include <math.h>
#include <stdlib.h>

#define ROUTE_DIRECTION_MIN_SPEED_KNOTS 90
#define ROUTE_DIRECTION_MIN_CONFIDENCE_DEGREES 38
#define ROUTE_DIRECTION_MAX_FORWARD_ERROR_DEGREES 78

static double degrees_to_radians(double degrees)
{
    return degrees * 3.14159265358979323846 / 180.0;
}

static double radians_to_degrees(double radians)
{
    return radians * 180.0 / 3.14159265358979323846;
}

static int bearing_degrees(double latitude_1,
                           double longitude_1,
                           double latitude_2,
                           double longitude_2)
{
    const double phi_1 = degrees_to_radians(latitude_1);
    const double phi_2 = degrees_to_radians(latitude_2);
    const double delta_longitude =
        degrees_to_radians(longitude_2 - longitude_1);
    const double y = sin(delta_longitude) * cos(phi_2);
    const double x =
        cos(phi_1) * sin(phi_2) -
        sin(phi_1) * cos(phi_2) * cos(delta_longitude);
    int bearing = (int)lround(radians_to_degrees(atan2(y, x)));
    return (bearing + 360) % 360;
}

static int angular_difference_degrees(int left, int right)
{
    int difference = abs(left - right) % 360;
    return difference > 180 ? 360 - difference : difference;
}

airspace_route_direction_t airspace_route_direction_resolve(
    double aircraft_latitude,
    double aircraft_longitude,
    int aircraft_track_degrees,
    int aircraft_speed_knots,
    bool aircraft_track_valid,
    double reported_origin_latitude,
    double reported_origin_longitude,
    double reported_destination_latitude,
    double reported_destination_longitude)
{
    if (!aircraft_track_valid ||
        aircraft_speed_knots < ROUTE_DIRECTION_MIN_SPEED_KNOTS ||
        !isfinite(aircraft_latitude) ||
        !isfinite(aircraft_longitude) ||
        !isfinite(reported_origin_latitude) ||
        !isfinite(reported_origin_longitude) ||
        !isfinite(reported_destination_latitude) ||
        !isfinite(reported_destination_longitude)) {
        return AIRSPACE_ROUTE_DIRECTION_UNCERTAIN;
    }

    const int normalized_track =
        ((aircraft_track_degrees % 360) + 360) % 360;
    const int bearing_to_reported_origin = bearing_degrees(
        aircraft_latitude,
        aircraft_longitude,
        reported_origin_latitude,
        reported_origin_longitude
    );
    const int bearing_to_reported_destination = bearing_degrees(
        aircraft_latitude,
        aircraft_longitude,
        reported_destination_latitude,
        reported_destination_longitude
    );
    const int origin_error = angular_difference_degrees(
        normalized_track,
        bearing_to_reported_origin
    );
    const int destination_error = angular_difference_degrees(
        normalized_track,
        bearing_to_reported_destination
    );

    if (origin_error <= ROUTE_DIRECTION_MAX_FORWARD_ERROR_DEGREES &&
        destination_error - origin_error >=
            ROUTE_DIRECTION_MIN_CONFIDENCE_DEGREES) {
        return AIRSPACE_ROUTE_DIRECTION_REVERSED;
    }
    if (destination_error <= ROUTE_DIRECTION_MAX_FORWARD_ERROR_DEGREES &&
        origin_error - destination_error >=
            ROUTE_DIRECTION_MIN_CONFIDENCE_DEGREES) {
        return AIRSPACE_ROUTE_DIRECTION_AS_REPORTED;
    }
    return AIRSPACE_ROUTE_DIRECTION_UNCERTAIN;
}
