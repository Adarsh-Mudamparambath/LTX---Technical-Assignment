#include "model/FlightTableModel.h"

#include <QBrush>
#include <QColor>

#include <algorithm>

namespace {

/// Placeholder shown for a std::nullopt cell, so a NULL reads differently from
/// a genuine zero.
const QString kNullText = QStringLiteral("—"); // em dash

QVariant optionalToDisplay(const std::optional<int>& v)
{
    return v ? QVariant(*v) : QVariant(kNullText);
}

} // namespace

FlightTableModel::FlightTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int FlightTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_records.size());
}

int FlightTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : FlightRecord::ColumnCount;
}

QVariant FlightTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    const FlightRecord& r = m_records[static_cast<size_t>(index.row())];

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case Time:              return r.time.toString(QStringLiteral("hh:mm:ss"));
        case Latitude:          return QString::number(r.latitude, 'f', 4);
        case Longitude:         return QString::number(r.longitude, 'f', 4);
        case Course:            return optionalToDisplay(r.course);
        case SpeedKts:          return optionalToDisplay(r.speedKts);
        case SpeedMph:          return optionalToDisplay(r.speedMph);
        case AltitudeFeet:      return optionalToDisplay(r.altitudeFeet);
        case ReportingFacility: return r.reportingFacility;
        default:                return {};
        }

    case Qt::TextAlignmentRole:
        return index.column() == ReportingFacility
                   ? QVariant(int(Qt::AlignLeft | Qt::AlignVCenter))
                   : QVariant(int(Qt::AlignRight | Qt::AlignVCenter));

    case Qt::ForegroundRole: {
        // Grey out the NULL placeholders.
        const bool isNull =
            (index.column() == Course       && !r.course) ||
            (index.column() == SpeedKts     && !r.speedKts) ||
            (index.column() == SpeedMph     && !r.speedMph) ||
            (index.column() == AltitudeFeet && !r.altitudeFeet);
        if (isNull)
            return QBrush(QColor(150, 150, 150));
        return {};
    }

    default:
        return {};
    }
}

QVariant FlightTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};

    if (orientation == Qt::Vertical)
        return section + 1;

    switch (section) {
    case Time:              return QStringLiteral("Time");
    case Latitude:          return QStringLiteral("Latitude");
    case Longitude:         return QStringLiteral("Longitude");
    case Course:            return QStringLiteral("Course");
    case SpeedKts:          return QStringLiteral("SpeedKts");
    case SpeedMph:          return QStringLiteral("SpeedMph");
    case AltitudeFeet:      return QStringLiteral("AltitudeFeet");
    case ReportingFacility: return QStringLiteral("Reporting Facility");
    default:                return {};
    }
}

void FlightTableModel::setRecords(std::vector<FlightRecord> records)
{
    beginResetModel();
    m_records = std::move(records);
    endResetModel();
}

void FlightTableModel::sortByTime()
{
    if (m_records.empty())
        return;

    beginResetModel();
    std::stable_sort(m_records.begin(), m_records.end(),
                     [](const FlightRecord& a, const FlightRecord& b) {
                         return a.time < b.time;
                     });
    endResetModel();
}
