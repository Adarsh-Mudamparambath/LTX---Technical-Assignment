#include "io/BinaryWriter.h"
#include "io/GeoJsonWriter.h"
#include "map/FlightTrack.h"
#include "map/GeodesicPath.h"
#include "model/CsvLoader.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QSurfaceFormat>
#include <QTextStream>

#include <algorithm>

namespace {

/// If NZ5_Flightdata.csv sits next to the executable (the build stages it
/// there) load it immediately, so the app opens with data on screen.
QString defaultCsvPath()
{
    const QString beside = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("NZ5_Flightdata.csv"));
    return QFile::exists(beside) ? beside : QString();
}

/// Same idea for the tile archive used by the 2D map and the 3D globe.
QString defaultTilesPath()
{
    const QString beside = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("World_10M.mbtiles"));
    return QFile::exists(beside) ? beside : QString();
}

/// Headless run of Tasks 1-4, used by the build check and by CI.
///   flightviz --selftest <csv> <outputDir>
/// Writes <outputDir>/selftest.txt and returns 0 only if every step passed.
int runSelfTest(const QString& csvPath, const QString& outDir)
{
    QDir().mkpath(outDir);

    QString     report;
    QTextStream out(&report);
    bool        ok = true;

    out << "FlightViz self-test\n";
    out << "csv: " << csvPath << "\n";
    out << "out: " << outDir << "\n\n";

    // Task-1 ------------------------------------------------------------
    const csv::LoadResult load = csv::load(csvPath);
    if (!load.ok) {
        out << "[Task-1] FAIL " << load.error << "\n";
        QFile f(QDir(outDir).filePath(QStringLiteral("selftest.txt")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(report.toUtf8());
        return 1;
    }

    auto records = load.records;
    const int nullSpeeds = static_cast<int>(
        std::count_if(records.begin(), records.end(),
                      [](const FlightRecord& r) { return !r.speedKts.has_value(); }));

    out << "[Task-1] OK   " << records.size() << " records, "
        << load.skippedRows << " skipped, "
        << nullSpeeds << " rows with NULL SpeedKts\n";

    // Task-2 ------------------------------------------------------------
    std::stable_sort(records.begin(), records.end(),
                     [](const FlightRecord& a, const FlightRecord& b) { return a.time < b.time; });
    const bool sorted = std::is_sorted(records.begin(), records.end(),
                                       [](const FlightRecord& a, const FlightRecord& b) {
                                           return a.time < b.time;
                                       });
    ok = ok && sorted;
    out << "[Task-2] " << (sorted ? "OK   " : "FAIL ")
        << "first=" << records.front().time.toString(QStringLiteral("hh:mm:ss"))
        << " last="  << records.back().time.toString(QStringLiteral("hh:mm:ss")) << "\n";

    // Task-3 ------------------------------------------------------------
    const QString geoPath = QDir(outDir).filePath(QStringLiteral("flight.geojson"));
    const geojson::WriteResult geo = geojson::write(geoPath, records);
    ok = ok && geo.ok;
    out << "[Task-3] " << (geo.ok ? "OK   " : "FAIL ")
        << (geo.ok ? QStringLiteral("%1 features, %2 bytes")
                         .arg(geo.featureCount).arg(QFileInfo(geoPath).size())
                   : geo.error)
        << "\n";

    // Task-4 ------------------------------------------------------------
    const QString coursePath = QDir(outDir).filePath(QStringLiteral("course_lt_220.bin"));
    const QString altPath    = QDir(outDir).filePath(QStringLiteral("altitude_37900_38000.bin"));

    out << "[Task-4] sizeof(OutputRecord)=" << int(sizeof(binout::OutputRecord)) << "\n";
    ok = ok && (sizeof(binout::OutputRecord) == 15);

    for (const binout::WriteResult& r :
         binout::writeFilteredPair(coursePath, altPath, records)) {
        const bool sizeOk = r.ok && r.bytesWritten == 3000;
        ok = ok && sizeOk;
        out << "[Task-4] " << (sizeOk ? "OK   " : "FAIL ")
            << QFileInfo(r.path).fileName() << " -> "
            << (r.ok ? QStringLiteral("%1 records, %2 bytes").arg(r.recordsWritten).arg(r.bytesWritten)
                     : r.error)
            << "\n";
    }

    // Task-7 / Task-8 ---------------------------------------------------
    const std::vector<GeoPoint> flightPath = track::fromRecords(records);
    const bool trackOk = flightPath.size() == records.size();
    ok = ok && trackOk;
    out << "[Task-7] " << (trackOk ? "OK   " : "FAIL ")
        << flightPath.size() << " track points, "
        << QString::number(flightPath.front().lat, 'f', 4) << ","
        << QString::number(flightPath.front().lon, 'f', 4) << " -> "
        << QString::number(flightPath.back().lat, 'f', 4) << ","
        << QString::number(flightPath.back().lon, 'f', 4) << "\n";

    if (flightPath.size() >= 2) {
        const auto   shortest = geodesic::between(flightPath.front(), flightPath.back());
        const double km = geodesic::distanceMetres(flightPath.front(), flightPath.back()) / 1000.0;
        const bool   geoOk = shortest.size() > 2 && km > 0.0;
        ok = ok && geoOk;
        out << "[Task-8] " << (geoOk ? "OK   " : "FAIL ")
            << shortest.size() << " interpolated points, WGS84 geodesic "
            << QString::number(km, 'f', 3) << " km\n";
    }

    out << "\nRESULT: " << (ok ? "PASS" : "FAIL") << "\n";
    out.flush();

    QFile f(QDir(outDir).filePath(QStringLiteral("selftest.txt")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(report.toUtf8());

    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[])
{
    // Ask for a 3.3 core profile before any QOpenGLWidget is built; both GL
    // widgets target QOpenGLFunctions_3_3_Core and #version 330 shaders.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("FlightViz"));
    QCoreApplication::setOrganizationName(QStringLiteral("LTX-AE-Assignment"));

    const QStringList args = QCoreApplication::arguments();
    const int         idx  = args.indexOf(QStringLiteral("--selftest"));
    if (idx >= 0 && args.size() > idx + 2)
        return runSelfTest(args.at(idx + 1), args.at(idx + 2));

    MainWindow w;
    if (const QString tiles = defaultTilesPath(); !tiles.isEmpty())
        w.loadTiles(tiles);
    if (const QString csv = defaultCsvPath(); !csv.isEmpty())
        w.loadCsv(csv);
    w.show();

    return app.exec();
}
