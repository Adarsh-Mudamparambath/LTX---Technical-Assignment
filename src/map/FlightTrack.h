#pragma once

#include "model/FlightRecord.h"

#include <vector>

/// A position on the globe, in degrees.
struct GeoPoint
{
    double lat = 0.0;
    double lon = 0.0;
};

namespace track {

/// Task-7: the flight path, ordered by time.
///
/// The table can be in file order or sorted, but a trajectory only means
/// anything in time order, so the track is always built from a time-sorted
/// copy rather than from the model's current row order.
std::vector<GeoPoint> fromRecords(const std::vector<FlightRecord>& records);

/// Longitudes unwrapped into a continuous run, so a path crossing the
/// antimeridian keeps going instead of jumping the width of the map.
///
/// The returned x values are Web Mercator world coordinates that may fall
/// outside 0..1; the renderer draws shifted copies to cover whichever world
/// repetition is on screen.
std::vector<float> toUnwrappedWorldXY(const std::vector<GeoPoint>& points);

/// Unit-sphere positions (+Y north) scaled by `radius`, matching the vertex
/// layout Globe3DWidget uses for the sphere itself.
std::vector<float> toSpherePositions(const std::vector<GeoPoint>& points, double radius);

} // namespace track
