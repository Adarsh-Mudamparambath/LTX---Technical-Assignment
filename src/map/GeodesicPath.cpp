#include "map/GeodesicPath.h"

#include <GeographicLib/Geodesic.hpp>
#include <GeographicLib/GeodesicLine.hpp>

#include <algorithm>

namespace geodesic {

std::vector<GeoPoint> between(const GeoPoint& from, const GeoPoint& to, int samples)
{
    std::vector<GeoPoint> path;

    const int steps = std::max(1, samples);
    path.reserve(size_t(steps) + 1);

    const GeographicLib::Geodesic& wgs84 = GeographicLib::Geodesic::WGS84();

    // InverseLine solves the inverse problem once, then the line can be
    // sampled cheaply along its arc.
    const GeographicLib::GeodesicLine line =
        wgs84.InverseLine(from.lat, from.lon, to.lat, to.lon);

    const double arcStep = line.Arc() / steps;
    for (int i = 0; i <= steps; ++i) {
        double lat = 0.0;
        double lon = 0.0;
        line.ArcPosition(i * arcStep, lat, lon);
        path.push_back(GeoPoint{lat, lon});
    }

    return path;
}

double distanceMetres(const GeoPoint& from, const GeoPoint& to)
{
    double metres = 0.0;
    GeographicLib::Geodesic::WGS84().Inverse(from.lat, from.lon, to.lat, to.lon, metres);
    return metres;
}

} // namespace geodesic
