#include "gl/Globe3DWidget.h"

#include "map/MBTilesReader.h"
#include "map/Mercator.h"

#include <QColor>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QVector3D>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

/// Zoom level stitched into the globe texture. 4 gives a 4096x4096 image,
/// which is plenty for a sphere on screen and well inside every desktop GL
/// implementation's maximum texture size.
constexpr int kGlobeTextureZoom = 4;

constexpr int kLatBands = 128;
constexpr int kLonBands = 256;

const char* kVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;      // unit sphere, +Y north
layout(location = 1) in vec2 aGeo;      // longitude, latitude in degrees

uniform mat4 uMvp;
uniform mat3 uNormalMatrix;

out vec2 vGeo;
out vec3 vNormalView;

void main()
{
    vGeo        = aGeo;
    vNormalView = normalize(uNormalMatrix * aPos);
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char* kFragmentShader = R"(
#version 330 core
in vec2 vGeo;
in vec3 vNormalView;

uniform sampler2D uTexture;

out vec4 fragColor;

const float PI = 3.14159265358979323846;
const float MAX_LAT = 85.05112878;

void main()
{
    // Web Mercator lookup, done per fragment so the sphere needs no
    // resampled equirectangular copy of the imagery.
    float u    = (vGeo.x + 180.0) / 360.0;
    float latR = radians(clamp(vGeo.y, -MAX_LAT, MAX_LAT));
    float merc = log(tan(PI / 4.0 + latR / 2.0));
    float v    = 0.5 + merc / (2.0 * PI);   // texture is flipped, so north = 1

    vec3 albedo = texture(uTexture, vec2(u, clamp(v, 0.0, 1.0))).rgb;

    // Gentle head-on lighting: enough shading to read as a sphere without
    // throwing half the map into darkness.
    float lambert = max(dot(normalize(vNormalView), vec3(0.0, 0.0, 1.0)), 0.0);
    float shade   = mix(0.55, 1.0, lambert);

    fragColor = vec4(albedo * shade, 1.0);
}
)";

/// Task-7: the track sits just above the surface so the sphere's own depth
/// values hide the half of the path that is around the back.
constexpr double kTrackRadius    = 1.004;
constexpr double kGeodesicRadius = 1.003;

const char* kLineVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;

void main()
{
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char* kLineFragmentShader = R"(
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;

void main()
{
    fragColor = uColor;
}
)";

} // namespace

Globe3DWidget::Globe3DWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
}

Globe3DWidget::~Globe3DWidget()
{
    makeCurrent();
    m_texture.reset();
    m_vbo.destroy();
    m_ibo.destroy();
    m_vao.destroy();
    m_track.destroy();
    m_geodesic.destroy();
    m_program.reset();
    m_lineProgram.reset();
    doneCurrent();
}

void Globe3DWidget::setTileSource(MBTilesReader* tiles)
{
    m_tiles        = tiles;
    m_textureDirty = true;
    update();
}

float Globe3DWidget::cameraDistance() const
{
    // zoom 1 -> far enough to frame the whole globe; each level halves the
    // gap to the surface, matching the 2D map's doubling of scale.
    const double d = 1.0 + 4.0 / std::pow(2.0, std::max(0.0, m_zoom - 1.0));
    return float(std::clamp(d, 1.05, 12.0));
}

void Globe3DWidget::buildSphere(int latBands, int lonBands)
{
    std::vector<float>    vertices;   // x, y, z, lon, lat
    std::vector<unsigned> indices;

    vertices.reserve(size_t(latBands + 1) * (lonBands + 1) * 5);

    for (int i = 0; i <= latBands; ++i) {
        const double lat  = 90.0 - 180.0 * double(i) / latBands;
        const double latR = mercator::degToRad(lat);

        for (int j = 0; j <= lonBands; ++j) {
            // The date line gets two coincident vertices (-180 and +180), so
            // nothing interpolates across the seam.
            const double lon  = -180.0 + 360.0 * double(j) / lonBands;
            const double lonR = mercator::degToRad(lon);

            vertices.push_back(float(std::cos(latR) * std::sin(lonR)));
            vertices.push_back(float(std::sin(latR)));
            vertices.push_back(float(std::cos(latR) * std::cos(lonR)));
            vertices.push_back(float(lon));
            vertices.push_back(float(lat));
        }
    }

    const int stride = lonBands + 1;
    for (int i = 0; i < latBands; ++i) {
        for (int j = 0; j < lonBands; ++j) {
            const unsigned a = unsigned(i * stride + j);
            const unsigned b = unsigned(a + stride);

            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(a + 1);

            indices.push_back(a + 1);
            indices.push_back(b);
            indices.push_back(b + 1);
        }
    }

    m_indexCount = int(indices.size());

    m_vao.create();
    QOpenGLVertexArrayObject::Binder binder(&m_vao);

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(vertices.data(), int(vertices.size() * sizeof(float)));

    m_program->bind();
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, 5 * sizeof(float));
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 2, 5 * sizeof(float));
    m_program->release();

    m_ibo.create();
    m_ibo.bind();
    m_ibo.allocate(indices.data(), int(indices.size() * sizeof(unsigned)));
}

void Globe3DWidget::uploadTexture()
{
    m_textureDirty = false;
    m_texture.reset();

    if (!m_tiles || !m_tiles->isOpen())
        return;

    // Gaps (this archive has no polar rows) stay as a pale ice tone rather
    // than punching holes in the globe.
    const QImage world = m_tiles->buildWorldImage(kGlobeTextureZoom, QColor(232, 236, 240));
    if (world.isNull())
        return;

    m_texture = std::make_unique<QOpenGLTexture>(world.mirrored(false, true));
    m_texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    m_texture->setMagnificationFilter(QOpenGLTexture::Linear);
    m_texture->setWrapMode(QOpenGLTexture::DirectionS, QOpenGLTexture::Repeat);
    m_texture->setWrapMode(QOpenGLTexture::DirectionT, QOpenGLTexture::ClampToEdge);
}

void Globe3DWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);   // space
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    m_program = std::make_unique<QOpenGLShaderProgram>();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    m_program->link();

    buildSphere(kLatBands, kLonBands);

    // --- Task-7 track
    m_lineProgram = std::make_unique<QOpenGLShaderProgram>();
    m_lineProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, kLineVertexShader);
    m_lineProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, kLineFragmentShader);
    m_lineProgram->link();

    m_track.create();
    m_geodesic.create();
}

void Globe3DWidget::setTrack(const std::vector<GeoPoint>& points)
{
    m_track.set(track::toSpherePositions(points, kTrackRadius), 3);
    update();
}

void Globe3DWidget::setGeodesic(const std::vector<GeoPoint>& points)
{
    // Very slightly lower than the track, so where the two coincide the
    // flight path is the one that wins the depth test.
    m_geodesic.set(track::toSpherePositions(points, kGeodesicRadius), 3);
    update();
}

void Globe3DWidget::uploadLine(LineBuffer& line)
{
    line.dirty = false;
    if (!line.vbo.isCreated() || line.data.empty())
        return;

    QOpenGLVertexArrayObject::Binder binder(&line.vao);
    line.vbo.bind();
    line.vbo.allocate(line.data.data(), int(line.data.size() * sizeof(float)));

    m_lineProgram->bind();
    m_lineProgram->enableAttributeArray(0);
    m_lineProgram->setAttributeBuffer(0, GL_FLOAT, 0, 3, 3 * sizeof(float));
    m_lineProgram->release();
}

void Globe3DWidget::drawLine(LineBuffer& line, const QMatrix4x4& mvp,
                             const QVector4D& color, float width)
{
    if (line.dirty)
        uploadLine(line);
    if (!line.drawable() || !m_lineProgram)
        return;

    m_lineProgram->bind();
    m_lineProgram->setUniformValue("uMvp", mvp);
    m_lineProgram->setUniformValue("uColor", color);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(width);

    QOpenGLVertexArrayObject::Binder binder(&line.vao);
    glDrawArrays(GL_LINE_STRIP, 0, line.count);

    glDisable(GL_BLEND);
    m_lineProgram->release();
}

void Globe3DWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void Globe3DWidget::paintGL()
{
    // Restated every frame rather than trusted from resizeGL(). Qt does set it
    // correctly for each QOpenGLWidget's own FBO, but these two widgets share a
    // GL context, so being explicit here costs nothing and removes a class of
    // ordering bug. Dimensions are device pixels, which is how the FBO is sized.
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, int(width() * dpr), int(height() * dpr));

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_textureDirty)
        uploadTexture();

    if (!m_program || !m_texture || m_indexCount == 0)
        return;

    const float distance = cameraDistance();
    const float latR     = float(mercator::degToRad(m_centerLat));
    const float lonR     = float(mercator::degToRad(m_centerLon));

    // Camera sits above (centre lat, centre lon) looking at the origin, so the
    // point facing the viewer is exactly the 2D map's centre.
    const QVector3D eye(distance * std::cos(latR) * std::sin(lonR),
                        distance * std::sin(latR),
                        distance * std::cos(latR) * std::cos(lonR));

    QMatrix4x4 view;
    view.lookAt(eye, QVector3D(0, 0, 0), QVector3D(0, 1, 0));

    QMatrix4x4  projection;
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;

    // perspective() fixes the *vertical* field of view, which frames the globe
    // badly in a pane that is not roughly square: too small when it is wide,
    // cropped left and right when it is tall. Derive the angle instead, so the
    // sphere always spans the same fraction of the pane's shorter axis.
    constexpr double kFillFraction = 0.85;
    const double     d             = std::max(1.0001, double(distance));
    const double     tanAngular    = 1.0 / std::sqrt(d * d - 1.0);  // tan(asin(1/d))
    double           tanHalfMin    = tanAngular / kFillFraction;
    if (aspect < 1.0)
        tanHalfMin /= aspect;   // shorter axis is horizontal; open up vertically

    const float fovY = 2.0f * float(mercator::radToDeg(std::atan(tanHalfMin)));
    projection.perspective(std::clamp(fovY, 1.0f, 170.0f), aspect, 0.01f, 100.0f);

    m_program->bind();
    m_program->setUniformValue("uMvp", projection * view);
    m_program->setUniformValue("uNormalMatrix", view.normalMatrix());
    m_program->setUniformValue("uTexture", 0);

    glActiveTexture(GL_TEXTURE0);
    m_texture->bind();

    {
        QOpenGLVertexArrayObject::Binder binder(&m_vao);
        glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    }
    m_program->release();

    const QMatrix4x4 mvp = projection * view;
    drawLine(m_geodesic, mvp, QVector4D(0.90f, 0.15f, 0.15f, 1.0f), 2.0f);  // red
    drawLine(m_track,    mvp, QVector4D(1.00f, 0.85f, 0.10f, 1.0f), 2.0f);  // yellow
}

void Globe3DWidget::setView(double latitude, double longitude, double zoomLevel, bool silent)
{
    // Clamped to the Mercator cut-off rather than to the pole: the 2D map
    // cannot centre any further north or south, and Task-6 requires the two
    // views to agree on a single latitude.
    m_centerLat = std::clamp(latitude, -mercator::kMaxLatitude, mercator::kMaxLatitude);
    m_centerLon = longitude;
    while (m_centerLon > 180.0)
        m_centerLon -= 360.0;
    while (m_centerLon < -180.0)
        m_centerLon += 360.0;

    const double maxZoom = m_tiles && m_tiles->isOpen() ? double(m_tiles->maxZoom()) + 2.0 : 8.0;
    m_zoom               = std::clamp(zoomLevel, 1.0, maxZoom);

    update();
    if (!silent)
        emit viewChanged(m_centerLat, m_centerLon, m_zoom);
}

void Globe3DWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging     = true;
        m_lastMousePos = e->pos();
    }
}

void Globe3DWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging)
        return;

    const QPoint delta = e->pos() - m_lastMousePos;
    m_lastMousePos     = e->pos();

    // Slow the drag down as the camera closes in, so the feel matches the
    // 2D map's pan at the same zoom.
    const double perPixel = 90.0 / std::max(1, height()) / std::max(1.0, m_zoom * 0.5);

    setView(m_centerLat + delta.y() * perPixel,
            m_centerLon - delta.x() * perPixel,
            m_zoom);
}

void Globe3DWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        m_dragging = false;
}

void Globe3DWidget::wheelEvent(QWheelEvent* e)
{
    const double steps = e->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps))
        return;

    setView(m_centerLat, m_centerLon, m_zoom + steps * 0.35);
}
