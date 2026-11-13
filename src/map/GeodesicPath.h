#pragma once

#include "map/FlightTrack.h"

#include <vector>

namespace geodesic {

/// Task-8: the shortest path between two points on the WGS84 ellipsoid,
/// sampled into `samples + 1` positions.
///
/// This is a true geodesic (Karney's algorithm, via GeographicLib), not a
/// spherical great circle - on this route the two differ by several
/// kilometres. The path is interpolated at equal arc-length steps so the line
/// stays smooth when it is drawn on the curved globe or on a Mercator map,
/// where a geodesic is not a straight line.
std::vector<GeoPoint> between(const GeoPoint& from, const GeoPoint& to, int samples = 256);

/// Length of that geodesic in metres.
double distanceMetres(const GeoPoint& from, const GeoPoint& to);

} // namespace geodesic
