#include "io/BinaryWriter.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <functional>
#include <future>
#include <limits>

namespace binout {
namespace {

/// Degrees -> the int32 the spec asks for, MSB first.
std::int32_t encodeDegrees(double degrees)
{
    const double scaled = degrees / kDegreeScale;

    // Clamp before the cast so an out-of-range value cannot wrap around.
    constexpr double kMin = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    constexpr double kMax = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    const double     safe = std::clamp(std::llround(scaled) * 1.0, kMin, kMax);

    return qToBigEndian(static_cast<std::int32_t>(safe));
}

std::uint16_t encodeU16(const std::optional<int>& v)
{
    const int clamped = v ? std::clamp(*v, 0, 65535) : 0;
    return qToBigEndian(static_cast<std::uint16_t>(clamped));
}

OutputRecord toOutputRecord(const FlightRecord& r)
{
    OutputRecord o;
    // The sheet notes "(in 24 hr form)" - QTime::hour() is already 0-23, which
    // is what the 12-hour AM/PM source string was parsed into on load.
    o.hour   = static_cast<std::uint8_t>(r.time.hour());
    o.minute = static_cast<std::uint8_t>(r.time.minute());
    o.sec    = static_cast<std::uint8_t>(r.time.second());

    o.latitude     = encodeDegrees(r.latitude);
    o.longitude    = encodeDegrees(r.longitude);
    o.speedKts     = encodeU16(r.speedKts);
    o.altitudeFeet = encodeU16(r.altitudeFeet);
    return o;
}

using Predicate = std::function<bool(const FlightRecord&)>;

/// filter -> sort by time -> first 200 -> serialise -> write.
/// Runs on its own thread; two of these execute concurrently.
WriteResult runPipeline(QString path, const std::vector<FlightRecord>& source, Predicate keep)
{
    WriteResult result;
    result.path = path;

    std::vector<FlightRecord> selected;
    selected.reserve(source.size());
    std::copy_if(source.begin(), source.end(), std::back_inserter(selected), keep);

    std::stable_sort(selected.begin(), selected.end(),
                     [](const FlightRecord& a, const FlightRecord& b) {
                         return a.time < b.time;
                     });

    if (static_cast<int>(selected.size()) < kRecordsPerFile) {
        result.error = QStringLiteral(
                           "Filter matched only %1 rows; %2 are needed for a %3-byte file")
                           .arg(selected.size())
                           .arg(kRecordsPerFile)
                           .arg(kRecordsPerFile * int(sizeof(OutputRecord)));
        return result;
    }
    selected.resize(kRecordsPerFile);

    std::vector<OutputRecord> packed;
    packed.reserve(selected.size());
    for (const FlightRecord& r : selected)
        packed.push_back(toOutputRecord(r));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return result;
    }

    const qint64 expected = static_cast<qint64>(packed.size()) * qint64(sizeof(OutputRecord));
    const qint64 written  = file.write(reinterpret_cast<const char*>(packed.data()), expected);
    file.close();

    if (written != expected) {
        result.error = QStringLiteral("Short write to %1 (%2 of %3 bytes)")
                           .arg(path).arg(written).arg(expected);
        return result;
    }

    result.ok             = true;
    result.recordsWritten = static_cast<int>(packed.size());
    result.bytesWritten   = written;
    return result;
}

} // namespace

std::vector<WriteResult> writeFilteredPair(const QString& courseFilePath,
                                           const QString& altitudeFilePath,
                                           const std::vector<FlightRecord>& records)
{
    // Task-4 asks for std::async so the sorting and the file writing happen
    // concurrently; launch::async forces a real thread for each pipeline
    // rather than deferred evaluation on this one.
    auto courseFuture = std::async(std::launch::async, runPipeline,
                                   courseFilePath, std::cref(records),
                                   Predicate([](const FlightRecord& r) {
                                       return r.course && *r.course < 220;
                                   }));

    auto altitudeFuture = std::async(std::launch::async, runPipeline,
                                     altitudeFilePath, std::cref(records),
                                     Predicate([](const FlightRecord& r) {
                                         return r.altitudeFeet
                                                && *r.altitudeFeet > 37900
                                                && *r.altitudeFeet <= 38000;
                                     }));

    return {courseFuture.get(), altitudeFuture.get()};
}

} // namespace binout
