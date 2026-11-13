#pragma once

#include "model/FlightRecord.h"

#include <QString>

#include <vector>

namespace geojson {

struct WriteResult
{
    bool    ok = false;
    QString error;
    int     featureCount = 0;
};

/// Task-3: write the (sorted) records as a GeoJSON FeatureCollection, with the
/// Time property in ISO 8601 form.
///
/// Each record becomes a Point feature; a LineString feature holding the whole
/// trajectory is appended so the file drops straight into any GeoJSON viewer.
WriteResult write(const QString& path, const std::vector<FlightRecord>& records);

} // namespace geojson
