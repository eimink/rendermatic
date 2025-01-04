#pragma once
#ifdef HAVE_DIRECTFB
#include "irenderer.h"
#include <directfb.h>
#include <directfbgl.h>
#include "loader.h"

class DirectFBRenderer : public IRenderer {
public:
    DirectFBRenderer();
    ~DirectFBRenderer();

    bool init(int width, int height, const char* title, bool fullscreen, int monitorIndex) override;
    void processInput() override;
    void render(const Texture& texture) override;
    bool shouldClose() const override;

private:
    bool initGL();
    void setupGLState();

    IDirectFB* m_dfb = nullptr;
    IDirectFBSurface* m_primary = nullptr;
    IDirectFBGL* m_gl = nullptr;
    bool m_shouldClose = false;
    
    // OpenGL resources
    unsigned int m_shader = 0;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    unsigned int m_texture = 0;

    Loader m_loader;
    int m_width, m_height;
    int m_colorFormatLocation;

    // Vertex data
    float m_vertices[20] = {
        -1.0f,  1.0f, 0.0f,  0.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 1.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 0.0f
    };
    unsigned int m_indices[6] = {
        0, 1, 2,
        0, 2, 3
    };
};
#endif
