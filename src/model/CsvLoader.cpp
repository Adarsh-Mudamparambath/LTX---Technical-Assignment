#include "model/CsvLoader.h"

#include <QFile>
#include <QTextStream>

namespace csv {
namespace {

/// Parse "3:44:57 PM" / "10:47:51 AM". The source sheet renders every time in
/// 12-hour form with an AM/PM suffix, but accept a 24-hour value too so a CSV
/// exported with different locale settings still loads.
QTime parseTime(const QString& raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty())
        return {};

    for (const char* fmt : {"h:mm:ss AP", "hh:mm:ss AP", "h:mm:ss ap", "H:mm:ss", "HH:mm:ss"}) {
        const QTime t = QTime::fromString(s, QString::fromLatin1(fmt));
        if (t.isValid())
            return t;
    }
    return {};
}

/// Empty / whitespace-only cells become std::nullopt - this is the NULL
/// handling Task-1 calls for.
std::optional<int> parseOptionalInt(const QString& raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty())
        return std::nullopt;

    bool      ok    = false;
    const int value = s.toInt(&ok);
    if (ok)
        return value;

    // Tolerate "34000.0" style values.
    const double d = s.toDouble(&ok);
    return ok ? std::optional<int>(static_cast<int>(d)) : std::nullopt;
}

} // namespace

QStringList splitLine(const QString& line)
{
    QStringList fields;
    QString     field;
    bool        inQuotes = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);

        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                // Doubled quote inside a quoted field is a literal quote.
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    field.append(QLatin1Char('"'));
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field.append(c);
            }
        } else if (c == QLatin1Char('"')) {
            inQuotes = true;
        } else if (c == QLatin1Char(',')) {
            fields.append(field);
            field.clear();
        } else {
            field.append(c);
        }
    }
    fields.append(field);
    return fields;
}

LoadResult load(const QString& path)
{
    LoadResult result;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = QStringLiteral("Cannot open %1: %2").arg(path, file.errorString());
        return result;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    // Header row - consumed and discarded, but its presence is required.
    const QString header = in.readLine();
    if (header.isNull()) {
        result.error = QStringLiteral("%1 is empty").arg(path);
        return result;
    }

    result.records.reserve(512);

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty())
            continue;

        const QStringList f = splitLine(line);
        if (f.size() < FlightRecord::ColumnCount) {
            ++result.skippedRows;
            continue;
        }

        FlightRecord r;
        r.time = parseTime(f.at(0));

        bool latOk = false;
        bool lonOk = false;
        r.latitude  = f.at(1).trimmed().toDouble(&latOk);
        r.longitude = f.at(2).trimmed().toDouble(&lonOk);
        if (!latOk || !lonOk || !r.time.isValid()) {
            ++result.skippedRows;
            continue;
        }

        r.course            = parseOptionalInt(f.at(3));
        r.speedKts          = parseOptionalInt(f.at(4));
        r.speedMph          = parseOptionalInt(f.at(5));
        r.altitudeFeet      = parseOptionalInt(f.at(6));
        r.reportingFacility = f.at(7).trimmed();

        result.records.push_back(std::move(r));
    }

    result.ok = true;
    return result;
}

} // namespace csv
