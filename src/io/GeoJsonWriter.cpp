#include "io/GeoJsonWriter.h"

#include <nlohmann/json.hpp>

#include <QFile>

using nlohmann::json;

namespace geojson {
namespace {

std::string toStd(const QString& s)
{
    return s.toStdString();
}

/// ISO 8601 time-of-day, e.g. "15:44:57".
///
/// The source data carries a clock time with no date, so the feature property
/// is an ISO 8601 local time rather than a full timestamp. QTime::ISODate is
/// exactly that format.
std::string isoTime(const QTime& t)
{
    return toStd(t.toString(Qt::ISODate));
}

json propertiesOf(const FlightRecord& r)
{
    json p;
    p["time"] = isoTime(r.time);

    // std::nullopt is written as JSON null, which is what "NULL entry" means
    // once the data leaves C++.
    p["course"]       = r.course       ? json(*r.course)       : json(nullptr);
    p["speedKts"]     = r.speedKts     ? json(*r.speedKts)     : json(nullptr);
    p["speedMph"]     = r.speedMph     ? json(*r.speedMph)     : json(nullptr);
    p["altitudeFeet"] = r.altitudeFeet ? json(*r.altitudeFeet) : json(nullptr);

    p["reportingFacility"] = toStd(r.reportingFacility);
    return p;
}

} // namespace

WriteResult write(const QString& path, const std::vector<FlightRecord>& records)
{
    WriteResult result;

    json features = json::array();
    json track    = json::array();
    features.get_ref<json::array_t&>().reserve(records.size() + 1);

    for (const FlightRecord& r : records) {
        // GeoJSON coordinate order is [longitude, latitude].
        json coords = json::array({r.longitude, r.latitude});
        track.push_back(coords);

        features.push_back(json{
            {"type", "Feature"},
            {"geometry", json{{"type", "Point"}, {"coordinates", std::move(coords)}}},
            {"properties", propertiesOf(r)},
        });
    }

    if (!track.empty()) {
        features.push_back(json{
            {"type", "Feature"},
            {"geometry", json{{"type", "LineString"}, {"coordinates", std::move(track)}}},
            {"properties", json{{"name", "NZ5 trajectory"}, {"pointCount", records.size()}}},
        });
    }

    const json doc{
        {"type", "FeatureCollection"},
        {"features", std::move(features)},
    };

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        result.error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return result;
    }

    const std::string text = doc.dump(2);
    if (file.write(text.data(), static_cast<qint64>(text.size())) < 0) {
        result.error = QStringLiteral("Write failed: %1").arg(file.errorString());
        return result;
    }
    file.close();

    result.ok           = true;
    result.featureCount = static_cast<int>(records.size());
    return result;
}

} // namespace geojson
