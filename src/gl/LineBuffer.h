#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>

#include <utility>
#include <vector>

/// A GPU-resident polyline: the CPU copy, its VAO/VBO, and an upload flag.
///
/// Both the yellow trajectory (Task-7) and the red geodesic (Task-8) are just
/// strips of vertices drawn with the same flat-colour shader, so each view
/// keeps one of these per line rather than duplicating the plumbing.
struct LineBuffer
{
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer            vbo{QOpenGLBuffer::VertexBuffer};
    std::vector<float>       data;
    int                      count = 0;
    bool                     dirty = false;

    void create()
    {
        vao.create();
        vbo.create();
    }

    void destroy()
    {
        vbo.destroy();
        vao.destroy();
    }

    /// Replace the vertex data. `componentsPerVertex` is 2 for the map's
    /// world-space line and 3 for the globe's sphere-space one.
    void set(std::vector<float> vertices, int componentsPerVertex)
    {
        data  = std::move(vertices);
        count = componentsPerVertex > 0 ? int(data.size()) / componentsPerVertex : 0;
        dirty = true;
    }

    bool drawable() const { return count >= 2; }
};
