#pragma once

#include <QString>
#include <QTime>

#include <optional>

/// One row of NZ5_Flightdata.csv.
///
/// Task-1 asks for "a vector of objects/struct having same member variables as
/// the columns in the CSV file" and for std::optional to carry NULL entries.
/// Time / Latitude / Longitude / Reporting Facility are always populated in the
/// source data; the speed columns are frequently blank, and the remaining
/// numeric columns are optional too so a partially-filled row never aborts the
/// load.
struct FlightRecord
{
    QTime                time;              ///< Column "Time"     (source is 12-hour with AM/PM)
    double               latitude  = 0.0;   ///< Column "Latitude"  (degrees, +N)
    double               longitude = 0.0;   ///< Column "Longitude" (degrees, +E)
    std::optional<int>   course;            ///< Column "Course"       (degrees true)
    std::optional<int>   speedKts;          ///< Column "SpeedKts"     (often NULL)
    std::optional<int>   speedMph;          ///< Column "SpeedMph"     (often NULL)
    std::optional<int>   altitudeFeet;      ///< Column "AltitudeFeet"
    QString              reportingFacility; ///< Column "Reporting Facility"

    /// Number of columns in the CSV, i.e. the column count of the table view.
    static constexpr int ColumnCount = 8;
};
