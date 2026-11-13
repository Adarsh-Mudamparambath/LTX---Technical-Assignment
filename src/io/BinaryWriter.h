#pragma once

#include "model/FlightRecord.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace binout {

/// The on-disk record laid out exactly as the 'outputstruct' sheet specifies.
///
///   uint8_t  hour          (24-hour form)
///   uint8_t  minute
///   uint8_t  sec
///   int32_t  latitude      MSB first, value / 8.381903e-8
///   int32_t  longitude     MSB first, value / 8.381903e-8
///   uint16_t speedKts      MSB first
///   uint16_t altitudeFeet  MSB first
///                          -> 15 bytes total
///
/// #pragma pack(1) is essential: with default alignment the int32 members are
/// padded to a 4-byte boundary and the struct becomes 16 bytes, which would
/// make each output file 3200 bytes instead of the required 3000.
#pragma pack(push, 1)
struct OutputRecord
{
    std::uint8_t  hour         = 0;
    std::uint8_t  minute       = 0;
    std::uint8_t  sec          = 0;
    std::int32_t  latitude     = 0; ///< stored big-endian
    std::int32_t  longitude    = 0; ///< stored big-endian
    std::uint16_t speedKts     = 0; ///< stored big-endian
    std::uint16_t altitudeFeet = 0; ///< stored big-endian
};
#pragma pack(pop)

static_assert(sizeof(OutputRecord) == 15, "outputstruct must be exactly 15 bytes");

/// Scale factor from the 'outputstruct' sheet: degrees / 8.381903e-8 gives the
/// 32-bit signed value. (This is the usual 180/2^31 semicircle encoding.)
inline constexpr double kDegreeScale = 8.381903e-8;

/// Records written per file (200 * 15 = 3000 bytes).
inline constexpr int kRecordsPerFile = 200;

struct WriteResult
{
    bool    ok = false;
    QString error;
    QString path;
    int     recordsWritten = 0;
    qint64  bytesWritten   = 0;
};

/// Task-4: dump the two filtered sets to binary.
///
/// File 1 keeps rows with `Course < 220`, file 2 keeps rows with
/// `37900 < AltitudeFeet <= 38000`. Each file is sorted by time and truncated
/// to the first 200 rows. The two pipelines (filter -> sort -> write) run
/// concurrently via std::async.
std::vector<WriteResult> writeFilteredPair(const QString& courseFilePath,
                                           const QString& altitudeFilePath,
                                           const std::vector<FlightRecord>& records);

} // namespace binout
