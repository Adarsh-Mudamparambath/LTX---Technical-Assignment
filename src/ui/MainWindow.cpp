#include "ui/MainWindow.h"

#include "gl/Globe3DWidget.h"
#include "gl/Map2DWidget.h"
#include "io/BinaryWriter.h"
#include "io/GeoJsonWriter.h"
#include "map/GeodesicPath.h"
#include "map/MBTilesReader.h"
#include "model/CsvLoader.h"
#include "model/FlightTableModel.h"

#include <QCheckBox>
#include <QDir>
#include <QMouseEvent>
#include <QSplitter>
#include <QTextStream>
#include <QWheelEvent>

#include <cmath>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("FlightViz - NZ5 flight data"));
    resize(1600, 900);

    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);

    // --- button row (mirrors the mock-up: Load CSV | Sort | Write Geojson | Write Binary)
    auto* buttons = new QHBoxLayout;
    m_loadButton   = new QPushButton(QStringLiteral("Load CSV"), central);
    m_sortButton   = new QPushButton(QStringLiteral("Sort"), central);
    m_jsonButton   = new QPushButton(QStringLiteral("Write Geojson"), central);
    m_binaryButton = new QPushButton(QStringLiteral("Write Binary"), central);

    for (QPushButton* b : {m_loadButton, m_sortButton, m_jsonButton, m_binaryButton}) {
        b->setMinimumWidth(130);
        buttons->addWidget(b);
    }

    // Task-6: on by default; the toggle is there so the synchronisation can be
    // turned off and the two views driven independently.
    m_syncBox = new QCheckBox(QStringLiteral("Synchronise views"), central);
    m_syncBox->setChecked(true);
    m_syncBox->setToolTip(QStringLiteral(
        "Keep the 2D map and the 3D globe centred on the same latitude/longitude"));
    buttons->addSpacing(18);
    buttons->addWidget(m_syncBox);

    buttons->addStretch(1);
    layout->addLayout(buttons);

    // --- table | 2D map | 3D globe, laid out like the mock-up on the
    //     Instructions sheet
    m_model = new FlightTableModel(this);
    m_table = new QTableView(central);
    m_table->setModel(m_model);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setDefaultSectionSize(22);
    m_table->horizontalHeader()->setStretchLastSection(true);

    m_map2d = new Map2DWidget(central);
    m_globe = new Globe3DWidget(central);
    m_map2d->setToolTip(QStringLiteral("2D map - drag to pan, wheel to zoom"));
    m_globe->setToolTip(QStringLiteral("3D globe - drag to rotate, wheel to zoom"));

    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->addWidget(m_table);
    m_splitter->addWidget(m_map2d);
    m_splitter->addWidget(m_globe);
    m_splitter->setChildrenCollapsible(false);
    for (int i = 0; i < 3; ++i)
        m_splitter->setStretchFactor(i, 1);
    connect(m_splitter, &QSplitter::splitterMoved, this,
            [this] { m_userSizedPanes = true; });
    layout->addWidget(m_splitter, 1);

    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("Load a CSV to begin."));

    // Task-6: each view publishes its camera, the other adopts it. setView's
    // `silent` flag stops the acknowledgement bouncing straight back.
    connect(m_map2d, &Map2DWidget::viewChanged,   this, &MainWindow::onMapViewChanged);
    connect(m_globe, &Globe3DWidget::viewChanged, this, &MainWindow::onGlobeViewChanged);
    connect(m_syncBox, &QCheckBox::toggled, this, [this](bool on) {
        if (on && m_map2d && m_globe)   // adopt the map's camera on re-enable
            m_globe->setView(m_map2d->centerLatitude(), m_map2d->centerLongitude(),
                             m_map2d->zoom(), true);
    });

    connect(m_loadButton,   &QPushButton::clicked, this, &MainWindow::onLoadCsv);
    connect(m_sortButton,   &QPushButton::clicked, this, &MainWindow::onSort);
    connect(m_jsonButton,   &QPushButton::clicked, this, &MainWindow::onWriteGeoJson);
    connect(m_binaryButton, &QPushButton::clicked, this, &MainWindow::onWriteBinary);

    updateButtonState();

    // Debug aid: FLIGHTVIZ_GRAB=<dir> saves each GL pane's own framebuffer a
    // few seconds after startup, which is how the Task-5 rendering was checked
    // without guessing at screenshot crops.
    if (qEnvironmentVariableIsSet("FLIGHTVIZ_GRAB")) {
        const QString dir = qEnvironmentVariable("FLIGHTVIZ_GRAB");
        QTimer::singleShot(4000, this, [this, dir] {
            m_map2d->grabFramebuffer().save(QDir(dir).filePath(QStringLiteral("pane_map2d.png")));
            m_globe->grabFramebuffer().save(QDir(dir).filePath(QStringLiteral("pane_globe.png")));
        });
    }

    // Debug aid: FLIGHTVIZ_SYNCTEST=<file> exercises Task-6 through the real
    // mouse handlers and writes a pass/fail report.
    if (qEnvironmentVariableIsSet("FLIGHTVIZ_SYNCTEST")) {
        const QString path = qEnvironmentVariable("FLIGHTVIZ_SYNCTEST");
        QTimer::singleShot(2500, this, [this, path] { runSyncTest(path); });
    }
}

namespace {

/// Send a real press/move/release through a widget's own event handlers, so
/// the sync test exercises the mouse manipulators rather than calling
/// setView() behind their back.
void synthesizeDrag(QWidget* w, QPoint from, QPoint to)
{
    const auto post = [w](QEvent::Type type, QPoint pos, Qt::MouseButton button,
                          Qt::MouseButtons buttons) {
        QMouseEvent e(type, QPointF(pos), w->mapToGlobal(QPointF(pos)),
                      button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &e);
    };

    post(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    // A couple of intermediate steps, the way a real drag arrives.
    for (int i = 1; i <= 4; ++i) {
        const QPoint step(from.x() + (to.x() - from.x()) * i / 4,
                          from.y() + (to.y() - from.y()) * i / 4);
        post(QEvent::MouseMove, step, Qt::NoButton, Qt::LeftButton);
    }
    post(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
}

void synthesizeWheel(QWidget* w, QPoint pos, int notches)
{
    QWheelEvent e(QPointF(pos), w->mapToGlobal(QPointF(pos)), QPoint(0, 0),
                  QPoint(0, notches * 120), Qt::NoButton, Qt::NoModifier,
                  Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(w, &e);
}

} // namespace

void MainWindow::runSyncTest(const QString& reportPath)
{
    QString     report;
    QTextStream out(&report);
    bool        ok = true;

    const auto agree = [&](const char* label) {
        const double dLat  = std::abs(m_map2d->centerLatitude()  - m_globe->centerLatitude());
        const double dLon  = std::abs(m_map2d->centerLongitude() - m_globe->centerLongitude());
        const double dZoom = std::abs(m_map2d->zoom()            - m_globe->zoom());
        const bool   match = dLat < 1e-9 && dLon < 1e-9 && dZoom < 1e-9;
        ok = ok && match;

        out << (match ? "OK   " : "FAIL ") << label << "\n"
            << "       map  : " << QString::number(m_map2d->centerLatitude(), 'f', 6) << ", "
            << QString::number(m_map2d->centerLongitude(), 'f', 6)
            << "  zoom " << QString::number(m_map2d->zoom(), 'f', 4) << "\n"
            << "       globe: " << QString::number(m_globe->centerLatitude(), 'f', 6) << ", "
            << QString::number(m_globe->centerLongitude(), 'f', 6)
            << "  zoom " << QString::number(m_globe->zoom(), 'f', 4) << "\n";
    };

    out << "Task-6 view synchronisation\n\n";

    m_map2d->setView(0.0, 0.0, 3.0);
    agree("after seeding the map at 0,0 zoom 3");

    const QPoint mapMid(m_map2d->width() / 2, m_map2d->height() / 2);
    synthesizeDrag(m_map2d, mapMid, mapMid + QPoint(-120, -70));
    agree("after dragging the 2D map (globe should follow)");

    const QPoint globeMid(m_globe->width() / 2, m_globe->height() / 2);
    synthesizeDrag(m_globe, globeMid, globeMid + QPoint(90, 40));
    agree("after dragging the 3D globe (map should follow)");

    synthesizeWheel(m_map2d, mapMid, 2);
    agree("after wheel-zooming the 2D map");

    synthesizeWheel(m_globe, globeMid, -1);
    agree("after wheel-zooming the 3D globe");

    // With synchronisation off the views must be able to diverge.
    m_syncBox->setChecked(false);
    const double keptLon = m_globe->centerLongitude();
    synthesizeDrag(m_map2d, mapMid, mapMid + QPoint(-150, 0));
    const bool diverged = std::abs(m_globe->centerLongitude() - keptLon) < 1e-9
                          && std::abs(m_map2d->centerLongitude() - keptLon) > 1e-6;
    ok = ok && diverged;
    out << (diverged ? "OK   " : "FAIL ") << "with sync off the views move independently\n"
        << "       map  : " << QString::number(m_map2d->centerLongitude(), 'f', 6) << "\n"
        << "       globe: " << QString::number(m_globe->centerLongitude(), 'f', 6) << "\n";

    m_syncBox->setChecked(true);
    agree("re-enabling sync snaps the globe back to the map");

    out << "\nRESULT: " << (ok ? "PASS" : "FAIL") << "\n";
    out.flush();

    QFile f(reportPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(report.toUtf8());
}

void MainWindow::onMapViewChanged(double latitude, double longitude, double zoom)
{
    if (m_syncBox && m_syncBox->isChecked() && m_globe)
        m_globe->setView(latitude, longitude, zoom, /*silent=*/true);
    showViewPosition(latitude, longitude, zoom);
}

void MainWindow::onGlobeViewChanged(double latitude, double longitude, double zoom)
{
    if (m_syncBox && m_syncBox->isChecked() && m_map2d)
        m_map2d->setView(latitude, longitude, zoom, /*silent=*/true);
    showViewPosition(latitude, longitude, zoom);
}

void MainWindow::showViewPosition(double latitude, double longitude, double zoom)
{
    statusBar()->showMessage(
        QStringLiteral("Centre  %1° %2   %3° %4     zoom %5%6")
            .arg(std::abs(latitude), 0, 'f', 4)
            .arg(latitude >= 0 ? QStringLiteral("N") : QStringLiteral("S"))
            .arg(std::abs(longitude), 0, 'f', 4)
            .arg(longitude >= 0 ? QStringLiteral("E") : QStringLiteral("W"))
            .arg(zoom, 0, 'f', 2)
            .arg(m_syncBox && m_syncBox->isChecked() ? QStringLiteral("    (views synced)")
                                                     : QString()));
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);

    if (m_userSizedPanes || !m_splitter)
        return;

    const int w = m_splitter->width();
    if (w <= 0)
        return;

    // Table gets 30%, the two GL views split the rest. The table scrolls, the
    // map and globe do not, so the views are the ones that need the pixels.
    const int table = std::max(340, w * 30 / 100);
    const int rest  = std::max(200, (w - table - m_splitter->handleWidth() * 2) / 2);
    m_splitter->setSizes({table, rest, rest});
}

MainWindow::~MainWindow()
{
    // Both GL widgets hold a bare pointer to *m_tiles. Qt deletes child
    // widgets from ~QObject, which runs after this class's members are
    // already gone, so drop them here while the reader is still valid.
    delete m_map2d;
    m_map2d = nullptr;
    delete m_globe;
    m_globe = nullptr;
}

bool MainWindow::loadTiles(const QString& path)
{
    auto reader = std::make_unique<MBTilesReader>();
    if (!reader->open(path)) {
        statusBar()->showMessage(reader->errorString());
        return false;
    }

    m_tiles = std::move(reader);
    m_map2d->setTileSource(m_tiles.get());
    m_globe->setTileSource(m_tiles.get());

    // Frame the whole world to start with.
    m_map2d->setView(0.0, 0.0, m_tiles->minZoom() + 1.0, true);
    m_globe->setView(0.0, -160.0, 1.0, true);

    statusBar()->showMessage(
        QStringLiteral("Tiles: %1  (%2, zoom %3-%4)")
            .arg(QFileInfo(path).fileName(),
                 m_tiles->metadata(QStringLiteral("format")).toUpper())
            .arg(m_tiles->minZoom())
            .arg(m_tiles->maxZoom()));
    return true;
}

void MainWindow::updateButtonState()
{
    const bool hasData = !m_model->isEmpty();
    m_sortButton->setEnabled(hasData);
    m_jsonButton->setEnabled(hasData);
    m_binaryButton->setEnabled(hasData);
}

bool MainWindow::loadCsv(const QString& path)
{
    const csv::LoadResult r = csv::load(path);
    if (!r.ok) {
        statusBar()->showMessage(r.error);
        return false;
    }

    const int count = static_cast<int>(r.records.size());
    m_model->setRecords(r.records);
    m_table->resizeColumnsToContents();
    m_lastDirectory = QFileInfo(path).absolutePath();
    updateButtonState();

    // Task-7: hand the time-ordered path to both views.
    const std::vector<GeoPoint> flightPath = track::fromRecords(m_model->records());
    if (m_map2d)
        m_map2d->setTrack(flightPath);
    if (m_globe)
        m_globe->setTrack(flightPath);

    // Task-8: the geodesic between the first and last fix.
    double geodesicKm = 0.0;
    if (flightPath.size() >= 2) {
        const std::vector<GeoPoint> shortest =
            geodesic::between(flightPath.front(), flightPath.back());
        geodesicKm = geodesic::distanceMetres(flightPath.front(), flightPath.back()) / 1000.0;
        if (m_map2d)
            m_map2d->setGeodesic(shortest);
        if (m_globe)
            m_globe->setGeodesic(shortest);
    }

    // Frame the flight rather than leaving the views at 0,0.
    if (!flightPath.empty() && m_map2d) {
        const GeoPoint& mid = flightPath[flightPath.size() / 2];
        m_map2d->setView(mid.lat, mid.lon, 2.0);
    }

    QString msg = QStringLiteral("Loaded %1 rows from %2")
                      .arg(count).arg(QFileInfo(path).fileName());
    if (r.skippedRows > 0)
        msg += QStringLiteral("  (%1 malformed rows skipped)").arg(r.skippedRows);
    if (geodesicKm > 0.0)
        msg += QStringLiteral("   |   geodesic %1 km").arg(geodesicKm, 0, 'f', 1);
    statusBar()->showMessage(msg);
    return true;
}

void MainWindow::onLoadCsv()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open flight data CSV"),
        m_lastDirectory.isEmpty() ? QDir::currentPath() : m_lastDirectory,
        QStringLiteral("CSV files (*.csv);;All files (*)"));

    if (path.isEmpty())
        return;

    if (!loadCsv(path))
        QMessageBox::warning(this, QStringLiteral("Load failed"), statusBar()->currentMessage());
}

void MainWindow::onSort()
{
    m_model->sortByTime();
    m_table->scrollToTop();
    statusBar()->showMessage(
        QStringLiteral("Sorted %1 rows by Time (ascending).").arg(m_model->records().size()));
}

void MainWindow::onWriteGeoJson()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Write GeoJSON"),
        QDir(m_lastDirectory.isEmpty() ? QDir::currentPath() : m_lastDirectory)
            .filePath(QStringLiteral("flight.geojson")),
        QStringLiteral("GeoJSON (*.geojson *.json)"));

    if (path.isEmpty())
        return;

    const geojson::WriteResult r = geojson::write(path, m_model->records());
    if (!r.ok) {
        QMessageBox::warning(this, QStringLiteral("GeoJSON"), r.error);
        return;
    }
    statusBar()->showMessage(
        QStringLiteral("Wrote %1 point features (+1 LineString) to %2")
            .arg(r.featureCount).arg(QFileInfo(path).fileName()));
}

void MainWindow::onWriteBinary()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose a folder for the two binary files"),
        m_lastDirectory.isEmpty() ? QDir::currentPath() : m_lastDirectory);

    if (dir.isEmpty())
        return;

    const QString coursePath   = QDir(dir).filePath(QStringLiteral("course_lt_220.bin"));
    const QString altitudePath = QDir(dir).filePath(QStringLiteral("altitude_37900_38000.bin"));

    const auto results = binout::writeFilteredPair(coursePath, altitudePath, m_model->records());

    QStringList lines;
    bool        allOk = true;
    for (const binout::WriteResult& r : results) {
        if (r.ok) {
            lines << QStringLiteral("%1: %2 records, %3 bytes")
                         .arg(QFileInfo(r.path).fileName()).arg(r.recordsWritten).arg(r.bytesWritten);
        } else {
            allOk = false;
            lines << QStringLiteral("%1: %2").arg(QFileInfo(r.path).fileName(), r.error);
        }
    }

    if (allOk) {
        statusBar()->showMessage(lines.join(QStringLiteral("   |   ")));
        QMessageBox::information(this, QStringLiteral("Binary written"),
                                 lines.join(QStringLiteral("\n")));
    } else {
        QMessageBox::warning(this, QStringLiteral("Binary write"),
                             lines.join(QStringLiteral("\n")));
    }
}
