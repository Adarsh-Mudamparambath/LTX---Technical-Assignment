#pragma once

#include <algorithm>
#include <cmath>

/// Web Mercator (EPSG:3857) helpers shared by the 2D map and the 3D globe.
///
/// "World coordinates" here are the normalised square used by every slippy-map
/// scheme: x and y both run 0..1, x = 0 at 180 W, y = 0 at the north edge
/// (+85.051129 deg). Multiply by 256 * 2^z to get pixels at zoom z.
namespace mercator {

/// The latitude where the Mercator projection is cut off to keep the world
/// square: atan(sinh(pi)) in degrees.
inline constexpr double kMaxLatitude = 85.05112878;

inline constexpr double kPi = 3.14159265358979323846;

inline double degToRad(double d) { return d * kPi / 180.0; }
inline double radToDeg(double r) { return r * 180.0 / kPi; }

/// Longitude (degrees) -> world x in 0..1.
inline double lonToX(double lon)
{
    return (lon + 180.0) / 360.0;
}

/// Latitude (degrees) -> world y in 0..1, measured from the north edge down.
inline double latToY(double lat)
{
    const double clamped = std::clamp(lat, -kMaxLatitude, kMaxLatitude);
    const double phi     = degToRad(clamped);
    return 0.5 - std::log(std::tan(kPi / 4.0 + phi / 2.0)) / (2.0 * kPi);
}

/// World x in 0..1 -> longitude in degrees.
inline double xToLon(double x)
{
    return x * 360.0 - 180.0;
}

/// World y in 0..1 -> latitude in degrees.
inline double yToLat(double y)
{
    const double n = kPi * (1.0 - 2.0 * y);
    return radToDeg(std::atan(std::sinh(n)));
}

} // namespace mercator
