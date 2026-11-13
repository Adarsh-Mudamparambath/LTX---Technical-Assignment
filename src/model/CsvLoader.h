#pragma once

#include "model/FlightRecord.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace csv {

/// Outcome of a load attempt. `ok` is false only when the file could not be
/// opened or carried no usable header; malformed individual rows are reported
/// through `skippedRows` but do not fail the load.
struct LoadResult
{
    bool                      ok = false;
    QString                   error;
    std::vector<FlightRecord> records;
    int                       skippedRows = 0;
};

/// Split one CSV line into fields, honouring RFC 4180 quoting
/// ("a,b" stays one field, "" is an escaped quote).
QStringList splitLine(const QString& line);

/// Read `path` into a vector<FlightRecord> (Task-1).
LoadResult load(const QString& path);

} // namespace csv
