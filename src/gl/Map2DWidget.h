#pragma once

#include <QHash>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>

#include "gl/LineBuffer.h"
#include "map/FlightTrack.h"

#include <QVector4D>

#include <memory>
#include <vector>

class MBTilesReader;
class QOpenGLShaderProgram;
class QOpenGLTexture;

/// Task-5: the 2D half - a Web Mercator slippy map drawn in OpenGL from the
/// MBTiles pyramid.
///
/// Tiles are uploaded lazily as they come into view and kept in a texture
/// cache. The camera is (centre latitude, centre longitude, fractional zoom),
/// which is exactly the state Task-6 has to keep in step with the globe.
class Map2DWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit Map2DWidget(QWidget* parent = nullptr);
    ~Map2DWidget() override;

    /// The reader stays owned by the caller and must outlive this widget.
    void setTileSource(MBTilesReader* tiles);

    /// Task-7: the flight path, drawn as a connected yellow line.
    void setTrack(const std::vector<GeoPoint>& points);

    /// Task-8: the geodesic between the first and last fix, drawn in red.
    void setGeodesic(const std::vector<GeoPoint>& points);

    double centerLatitude() const { return m_centerLat; }
    double centerLongitude() const { return m_centerLon; }
    double zoom() const { return m_zoom; }

public slots:
    /// Move the camera. Emits viewChanged() unless `silent` is set, which is
    /// how Task-6 avoids a signal ping-pong between the two views.
    void setView(double latitude, double longitude, double zoom, bool silent = false);

signals:
    void viewChanged(double latitude, double longitude, double zoom);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    /// Pixels across the whole world at the current fractional zoom.
    double worldPixels() const;

    /// Integer pyramid level to draw at, clamped to what the archive holds.
    int tileZoom() const;

    QOpenGLTexture* textureFor(int z, int x, int y);
    void            clampCenter();
    void            uploadLine(LineBuffer& line);
    void            drawLine(LineBuffer& line, const QMatrix4x4& mvp,
                             const QVector4D& color, float width);

    MBTilesReader* m_tiles = nullptr;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    QOpenGLVertexArrayObject              m_vao;
    QOpenGLBuffer                         m_vbo{QOpenGLBuffer::VertexBuffer};

    // key = (z << 58) | (x << 29) | y
    QHash<quint64, QOpenGLTexture*> m_textures;

    // --- overlays: Task-7 track, Task-8 geodesic (unwrapped world coords)
    std::unique_ptr<QOpenGLShaderProgram> m_lineProgram;
    LineBuffer                            m_track;
    LineBuffer                            m_geodesic;

    double m_centerLat = 0.0;
    double m_centerLon = 0.0;
    double m_zoom      = 2.0;

    QPoint m_lastMousePos;
    bool   m_dragging = false;
};
