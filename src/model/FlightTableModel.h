#pragma once

#include "model/FlightRecord.h"

#include <QAbstractTableModel>

#include <vector>

/// Qt model that feeds the QTableView required by Task-1 and is re-sorted in
/// place by Task-2.
class FlightTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        Time = 0,
        Latitude,
        Longitude,
        Course,
        SpeedKts,
        SpeedMph,
        AltitudeFeet,
        ReportingFacility,
    };

    explicit FlightTableModel(QObject* parent = nullptr);

    // QAbstractTableModel
    int      rowCount(const QModelIndex& parent = {}) const override;
    int      columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /// Replace the whole data set (Task-1, on "Load CSV").
    void setRecords(std::vector<FlightRecord> records);

    /// Sort ascending by the Time column and refresh the view (Task-2).
    void sortByTime();

    const std::vector<FlightRecord>& records() const { return m_records; }
    bool                             isEmpty() const { return m_records.empty(); }

private:
    std::vector<FlightRecord> m_records;
};
