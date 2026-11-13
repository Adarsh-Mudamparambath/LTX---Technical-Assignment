#include "gl/Map2DWidget.h"

#include "map/MBTilesReader.h"
#include "map/Mercator.h"

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kTilePixels = 256;

const char* kVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aCorner;   // 0..1, (0,0) = north-west of the tile

uniform mat4 uMvp;
uniform vec4 uRect;                     // x, y(north edge), width, height in world units

out vec2 vUv;

void main()
{
    vec2 world = uRect.xy + aCorner * uRect.zw;
    // The texture is uploaded flipped (OpenGL's v runs bottom-up), so the
    // tile's north edge is v = 1.
    vUv = vec2(aCorner.x, 1.0 - aCorner.y);
    gl_Position = uMvp * vec4(world, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(
#version 330 core
in vec2 vUv;
uniform sampler2D uTexture;
out vec4 fragColor;

void main()
{
    fragColor = texture(uTexture, vUv);
}
)";

// Task-7: flat-colour line, positioned in the same world space as the tiles.
const char* kLineVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aWorld;

uniform mat4  uMvp;
uniform float uXOffset;   // draws the track on a neighbouring world copy

void main()
{
    gl_Position = uMvp * vec4(aWorld.x + uXOffset, aWorld.y, 0.0, 1.0);
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

quint64 tileKey(int z, int x, int y)
{
    return (quint64(z) << 58) | (quint64(x) << 29) | quint64(y);
}

} // namespace

Map2DWidget::Map2DWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
}

Map2DWidget::~Map2DWidget()
{
    // Textures are GL objects - they need the context current to be destroyed.
    makeCurrent();
    qDeleteAll(m_textures);
    m_textures.clear();
    m_vbo.destroy();
    m_vao.destroy();
    m_track.destroy();
    m_geodesic.destroy();
    m_program.reset();
    m_lineProgram.reset();
    doneCurrent();
}

void Map2DWidget::setTileSource(MBTilesReader* tiles)
{
    m_tiles = tiles;
    if (m_tiles && m_tiles->isOpen())
        m_zoom = std::max<double>(m_tiles->minZoom(), 2.0);
    update();
}

double Map2DWidget::worldPixels() const
{
    return double(kTilePixels) * std::pow(2.0, m_zoom);
}

int Map2DWidget::tileZoom() const
{
    if (!m_tiles || !m_tiles->isOpen())
        return 0;
    const int wanted = int(std::floor(m_zoom + 0.5));
    return std::clamp(wanted, m_tiles->minZoom(), m_tiles->maxZoom());
}

void Map2DWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.05f, 0.08f, 0.14f, 1.0f);   // deep ocean, shows through gaps
    glDisable(GL_DEPTH_TEST);

    m_program = std::make_unique<QOpenGLShaderProgram>();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    m_program->link();

    static const float corners[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
    };

    m_vao.create();
    QOpenGLVertexArrayObject::Binder binder(&m_vao);

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(corners, sizeof(corners));

    m_program->bind();
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 2 * sizeof(float));
    m_program->release();

    // --- Task-7 track
    m_lineProgram = std::make_unique<QOpenGLShaderProgram>();
    m_lineProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, kLineVertexShader);
    m_lineProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, kLineFragmentShader);
    m_lineProgram->link();

    m_track.create();
    m_geodesic.create();
}

void Map2DWidget::setTrack(const std::vector<GeoPoint>& points)
{
    m_track.set(track::toUnwrappedWorldXY(points), 2);
    update();
}

void Map2DWidget::setGeodesic(const std::vector<GeoPoint>& points)
{
    m_geodesic.set(track::toUnwrappedWorldXY(points), 2);
    update();
}

void Map2DWidget::uploadLine(LineBuffer& line)
{
    line.dirty = false;
    if (!line.vbo.isCreated() || line.data.empty())
        return;

    QOpenGLVertexArrayObject::Binder binder(&line.vao);
    line.vbo.bind();
    line.vbo.allocate(line.data.data(), int(line.data.size() * sizeof(float)));

    m_lineProgram->bind();
    m_lineProgram->enableAttributeArray(0);
    m_lineProgram->setAttributeBuffer(0, GL_FLOAT, 0, 2, 2 * sizeof(float));
    m_lineProgram->release();
}

void Map2DWidget::drawLine(LineBuffer& line, const QMatrix4x4& mvp,
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
    // The unwrapped line can run past the edge of the world square, so draw
    // the neighbouring copies too; whichever one is on screen shows up.
    for (int copy = -1; copy <= 1; ++copy) {
        m_lineProgram->setUniformValue("uXOffset", float(copy));
        glDrawArrays(GL_LINE_STRIP, 0, line.count);
    }

    glDisable(GL_BLEND);
    m_lineProgram->release();
}

void Map2DWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

QOpenGLTexture* Map2DWidget::textureFor(int z, int x, int y)
{
    const quint64 key = tileKey(z, x, y);
    if (const auto it = m_textures.constFind(key); it != m_textures.constEnd())
        return it.value();

    QOpenGLTexture* texture = nullptr;
    if (m_tiles) {
        const QImage img = m_tiles->tile(z, x, y);
        if (!img.isNull()) {
            // mirrored(): QImage row 0 is the top, OpenGL's v = 0 is the bottom.
            texture = new QOpenGLTexture(img.mirrored(false, true));
            texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            texture->setMagnificationFilter(QOpenGLTexture::Linear);
            texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        }
    }

    // A null entry is cached too, so a missing polar tile is looked up once.
    m_textures.insert(key, texture);
    return texture;
}

void Map2DWidget::paintGL()
{
    // See Globe3DWidget::paintGL - restated every frame rather than trusted
    // from resizeGL, since the two widgets share a GL context.
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, int(width() * dpr), int(height() * dpr));

    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_tiles || !m_tiles->isOpen() || !m_program)
        return;

    const double wp = worldPixels();
    const double halfW = width() / wp / 2.0;
    const double halfH = height() / wp / 2.0;

    const double cx = mercator::lonToX(m_centerLon);
    const double cy = mercator::latToY(m_centerLat);

    QMatrix4x4 mvp;
    // Bottom and top are swapped because world y grows southwards.
    mvp.ortho(float(cx - halfW), float(cx + halfW),
              float(cy + halfH), float(cy - halfH),
              -1.0f, 1.0f);

    const int    z        = tileZoom();
    const int    n        = 1 << z;
    const double tileSize = 1.0 / n;

    // X is deliberately not clamped: the map wraps east-west, so a view
    // straddling the antimeridian (which is where this flight lives) keeps
    // drawing into the next world copy instead of running out of tiles.
    // Y does not wrap - there is nothing beyond the poles.
    const int x0 = int(std::floor((cx - halfW) * n));
    const int x1 = int(std::floor((cx + halfW) * n));
    const int y0 = std::clamp(int(std::floor((cy - halfH) * n)), 0, n - 1);
    const int y1 = std::clamp(int(std::floor((cy + halfH) * n)), 0, n - 1);

    m_program->bind();
    m_program->setUniformValue("uMvp", mvp);
    m_program->setUniformValue("uTexture", 0);
    glActiveTexture(GL_TEXTURE0);

    QOpenGLVertexArrayObject::Binder binder(&m_vao);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            // Wrap the lookup, but keep the quad at its unwrapped position.
            const int wrappedX = ((x % n) + n) % n;

            QOpenGLTexture* texture = textureFor(z, wrappedX, y);
            if (!texture)
                continue;   // gap in the archive: the clear colour shows through

            m_program->setUniformValue(
                "uRect", QVector4D(float(x * tileSize), float(y * tileSize),
                                   float(tileSize), float(tileSize)));
            texture->bind();
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }

    m_program->release();

    // Geodesic underneath, flight path on top, so the actual route stays
    // readable where the two run close together.
    drawLine(m_geodesic, mvp, QVector4D(0.90f, 0.15f, 0.15f, 1.0f), 2.0f);  // red
    drawLine(m_track,    mvp, QVector4D(1.00f, 0.85f, 0.10f, 1.0f), 2.0f);  // yellow
}

void Map2DWidget::clampCenter()
{
    m_centerLat = std::clamp(m_centerLat, -mercator::kMaxLatitude, mercator::kMaxLatitude);

    // Wrap longitude so dragging east past the date line continues smoothly.
    while (m_centerLon > 180.0)
        m_centerLon -= 360.0;
    while (m_centerLon < -180.0)
        m_centerLon += 360.0;
}

void Map2DWidget::setView(double latitude, double longitude, double zoomLevel, bool silent)
{
    const double minZoom = m_tiles && m_tiles->isOpen() ? double(m_tiles->minZoom()) : 0.0;
    const double maxZoom = m_tiles && m_tiles->isOpen() ? double(m_tiles->maxZoom()) + 2.0 : 6.0;

    m_centerLat = latitude;
    m_centerLon = longitude;
    m_zoom      = std::clamp(zoomLevel, minZoom, maxZoom);
    clampCenter();

    update();
    if (!silent)
        emit viewChanged(m_centerLat, m_centerLon, m_zoom);
}

void Map2DWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging     = true;
        m_lastMousePos = e->pos();
    }
}

void Map2DWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging)
        return;

    const QPoint delta = e->pos() - m_lastMousePos;
    m_lastMousePos     = e->pos();

    const double wp = worldPixels();
    const double cx = mercator::lonToX(m_centerLon) - delta.x() / wp;
    const double cy = mercator::latToY(m_centerLat) - delta.y() / wp;

    setView(mercator::yToLat(std::clamp(cy, 0.0, 1.0)), mercator::xToLon(cx), m_zoom);
}

void Map2DWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        m_dragging = false;
}

void Map2DWidget::wheelEvent(QWheelEvent* e)
{
    const double steps = e->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps))
        return;

    setView(m_centerLat, m_centerLon, m_zoom + steps * 0.35);
}
