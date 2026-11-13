#include "map/MBTilesReader.h"

#include <QColor>
#include <QFileInfo>
#include <QPainter>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {
constexpr int kTilePixels = 256;
}

MBTilesReader::MBTilesReader()
    : m_connectionName(QStringLiteral("mbtiles-%1").arg(
          QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

MBTilesReader::~MBTilesReader()
{
    close();
}

bool MBTilesReader::open(const QString& path)
{
    close();

    if (!QFileInfo::exists(path)) {
        m_error = QStringLiteral("MBTiles file not found: %1").arg(path);
        return false;
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(path);
    // Read-only: the archive is an input, never written back.
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));

    if (!m_db.open()) {
        m_error = QStringLiteral("Cannot open %1: %2").arg(path, m_db.lastError().text());
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    // --- metadata
    QSqlQuery meta(m_db);
    if (meta.exec(QStringLiteral("SELECT name, value FROM metadata"))) {
        while (meta.next())
            m_metadata.insert(meta.value(0).toString(), meta.value(1).toString());
    }
    m_isTms = m_metadata.value(QStringLiteral("scheme"), QStringLiteral("tms")) != QLatin1String("xyz");

    // --- zoom range: trust the tiles table over the metadata, because this
    //     archive advertises minzoom=0 but holds nothing at z=0.
    QSqlQuery zooms(m_db);
    if (!zooms.exec(QStringLiteral("SELECT MIN(zoom_level), MAX(zoom_level) FROM tiles"))
        || !zooms.next()) {
        m_error = QStringLiteral("%1 has no usable tiles table: %2")
                      .arg(path, zooms.lastError().text());
        m_db.close();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }
    m_minZoom = zooms.value(0).toInt();
    m_maxZoom = zooms.value(1).toInt();

    m_path  = path;
    m_open  = true;
    m_error.clear();
    return true;
}

void MBTilesReader::close()
{
    if (m_db.isOpen())
        m_db.close();
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);

    m_metadata.clear();
    m_open = false;
}

QImage MBTilesReader::tile(int z, int x, int y) const
{
    if (!m_open || z < m_minZoom || z > m_maxZoom)
        return {};

    const int n = 1 << z;
    if (x < 0 || x >= n || y < 0 || y >= n)
        return {};

    // The caller addresses tiles XYZ-style (y = 0 at the north). MBTiles rows
    // are TMS by default, which counts from the south.
    const int row = m_isTms ? (n - 1 - y) : y;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT tile_data FROM tiles"
                             " WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?"));
    q.addBindValue(z);
    q.addBindValue(x);
    q.addBindValue(row);

    if (!q.exec() || !q.next())
        return {};

    QImage img;
    if (!img.loadFromData(q.value(0).toByteArray()))
        return {};

    return img.convertToFormat(QImage::Format_RGBA8888);
}

QImage MBTilesReader::buildWorldImage(int z, const QColor& background) const
{
    if (!m_open)
        return {};

    const int zoom = std::clamp(z, m_minZoom, m_maxZoom);
    const int n    = 1 << zoom;
    const int side = n * kTilePixels;

    QImage world(side, side, QImage::Format_RGBA8888);
    world.fill(background);

    QPainter painter(&world);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const QImage t = tile(zoom, x, y);
            if (!t.isNull())
                painter.drawImage(x * kTilePixels, y * kTilePixels, t);
        }
    }
    painter.end();

    return world;
}
