#pragma once

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

/// Task-5: the 3D half - a textured globe.
///
/// The whole MBTiles level is stitched into one Web Mercator image once, and
/// the sphere's fragment shader does the Mercator lookup per pixel, so no
/// resampling pass or seam fix-up is needed.
///
/// The camera orbits: the point facing the viewer is (centre latitude, centre
/// longitude), which deliberately mirrors Map2DWidget's camera so Task-6 can
/// tie the two together.
class Globe3DWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit Globe3DWidget(QWidget* parent = nullptr);
    ~Globe3DWidget() override;

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
    void buildSphere(int latBands, int lonBands);
    void uploadTexture();
    void uploadLine(LineBuffer& line);
    void drawLine(LineBuffer& line, const QMatrix4x4& mvp,
                  const QVector4D& color, float width);

    /// Camera distance in globe radii, derived from the shared zoom value so
    /// "zoomed in" means the same thing in both views.
    float cameraDistance() const;

    MBTilesReader* m_tiles = nullptr;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLTexture>       m_texture;
    QOpenGLVertexArrayObject              m_vao;
    QOpenGLBuffer                         m_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer                         m_ibo{QOpenGLBuffer::IndexBuffer};
    int                                   m_indexCount = 0;

    bool m_textureDirty = true;

    // --- overlays: Task-7 track, Task-8 geodesic (sphere-space positions)
    std::unique_ptr<QOpenGLShaderProgram> m_lineProgram;
    LineBuffer                            m_track;
    LineBuffer                            m_geodesic;

    double m_centerLat = 0.0;
    double m_centerLon = 0.0;
    double m_zoom      = 2.0;

    QPoint m_lastMousePos;
    bool   m_dragging = false;
};
