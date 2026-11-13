#include "map/FlightTrack.h"

#include "map/Mercator.h"

#include <algorithm>
#include <cmath>

namespace track {

std::vector<GeoPoint> fromRecords(const std::vector<FlightRecord>& records)
{
    std::vector<FlightRecord> ordered(records);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const FlightRecord& a, const FlightRecord& b) {
                         return a.time < b.time;
                     });

    std::vector<GeoPoint> points;
    points.reserve(ordered.size());
    for (const FlightRecord& r : ordered)
        points.push_back(GeoPoint{r.latitude, r.longitude});

    return points;
}

std::vector<float> toUnwrappedWorldXY(const std::vector<GeoPoint>& points)
{
    std::vector<float> xy;
    if (points.empty())
        return xy;

    xy.reserve(points.size() * 2);

    double previousX = mercator::lonToX(points.front().lon);
    xy.push_back(float(previousX));
    xy.push_back(float(mercator::latToY(points.front().lat)));

    for (size_t i = 1; i < points.size(); ++i) {
        // Step to the nearest image of the next longitude. NZ5 runs from
        // ~175E to ~118W, so without this every antimeridian crossing would
        // draw a spurious line back across the entire map.
        double delta = mercator::lonToX(points[i].lon) - std::fmod(previousX, 1.0);
        if (delta > 0.5)
            delta -= 1.0;
        else if (delta < -0.5)
            delta += 1.0;

        previousX += delta;
        xy.push_back(float(previousX));
        xy.push_back(float(mercator::latToY(points[i].lat)));
    }

    return xy;
}

std::vector<float> toSpherePositions(const std::vector<GeoPoint>& points, double radius)
{
    std::vector<float> xyz;
    xyz.reserve(points.size() * 3);

    for (const GeoPoint& p : points) {
        const double latR = mercator::degToRad(p.lat);
        const double lonR = mercator::degToRad(p.lon);

        xyz.push_back(float(radius * std::cos(latR) * std::sin(lonR)));
        xyz.push_back(float(radius * std::sin(latR)));
        xyz.push_back(float(radius * std::cos(latR) * std::cos(lonR)));
    }

    return xyz;
}

} // namespace track
