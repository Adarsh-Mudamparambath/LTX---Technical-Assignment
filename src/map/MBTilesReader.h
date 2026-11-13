#pragma once

#include <QHash>
#include <QImage>
#include <QSqlDatabase>
#include <QString>

/// Reads raster tiles out of an MBTiles archive.
///
/// An .mbtiles file is a SQLite database (spec: mapbox/mbtiles-spec) holding a
/// `metadata` key/value table and a `tiles(zoom_level, tile_column, tile_row,
/// tile_data)` table of encoded images. World_10M.mbtiles is PNG, 256x256,
/// zoom 1..6, `scheme=tms`.
///
/// Qt's own SQLite driver (Qt6::Sql) does the database work, so no extra
/// third-party dependency is needed here.
class MBTilesReader
{
public:
    MBTilesReader();
    ~MBTilesReader();

    MBTilesReader(const MBTilesReader&)            = delete;
    MBTilesReader& operator=(const MBTilesReader&) = delete;

    /// Open `path`. Returns false and sets errorString() on failure.
    bool open(const QString& path);
    void close();

    bool    isOpen() const { return m_open; }
    QString errorString() const { return m_error; }
    QString path() const { return m_path; }

    int minZoom() const { return m_minZoom; }
    int maxZoom() const { return m_maxZoom; }

    /// Raw value from the metadata table ("format", "scheme", "bounds", ...).
    QString metadata(const QString& key) const { return m_metadata.value(key); }

    /// Decode one tile addressed in **XYZ** convention (y = 0 at the north).
    /// Returns a null QImage when the archive has no tile there - this file is
    /// missing its polar rows, so that is a normal result, not an error.
    QImage tile(int z, int x, int y) const;

    /// Stitch a whole zoom level into one Web Mercator image, 256 * 2^z square.
    /// Used to build the globe texture. Gaps are left as `background`.
    QImage buildWorldImage(int z, const QColor& background) const;

private:
    QSqlDatabase            m_db;
    QString                 m_connectionName;
    QString                 m_path;
    QString                 m_error;
    QHash<QString, QString> m_metadata;
    bool                    m_open    = false;
    bool                    m_isTms   = true;
    int                     m_minZoom = 0;
    int                     m_maxZoom = 0;
};
