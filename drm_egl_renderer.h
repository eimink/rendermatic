#pragma once
#ifdef DFB_ONLY

#include "irenderer.h"
#include "loader.h"
#include <cstdint>

struct gbm_device;
struct gbm_surface;
struct gbm_bo;

class DrmEglRenderer : public IRenderer {
public:
    DrmEglRenderer();
    ~DrmEglRenderer();

    bool init(int width, int height, const char* title,
              bool fullscreen = true, int monitorIndex = 0) override;
    void processInput() override {}
    void render(const Texture& texture) override;
    void renderOverlay(const Texture& overlay) override;
    void present() override;
    bool shouldClose() const override { return false; }
    int getWidth() const override { return m_width; }
    int getHeight() const override { return m_height; }
    void setRotation(int degrees) override { m_displayRotation = degrees / 90; }

private:
    bool initDrm();
    bool initGbm();
    bool initEgl();
    bool initGl();

    // DRM
    int m_drmFd = -1;
    uint32_t m_connectorId = 0;
    uint32_t m_crtcId = 0;
    uint32_t m_crtcFbId = 0;  // original CRTC fb for cleanup
    void* m_drmMode = nullptr; // drmModeModeInfo*

    // GBM
    gbm_device* m_gbmDevice = nullptr;
    gbm_surface* m_gbmSurface = nullptr;
    gbm_bo* m_prevBo = nullptr;
    uint32_t m_prevFb = 0;

    // EGL (stored as void* to avoid header pollution)
    void* m_eglDisplay = nullptr;
    void* m_eglContext = nullptr;
    void* m_eglSurface = nullptr;

    // GL resources
    unsigned int m_shader = 0;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    unsigned int m_texture = 0;
    int m_colorFormatLocation = -1;
    int m_rotationLocation = -1;
    int m_displayRotation = 0;

    Loader m_loader;
    int m_width = 0;
    int m_height = 0;
    bool m_firstFrame = true;

    // Vertex data (full-screen quad)
    float m_vertices[20] = {
        -1.0f,  1.0f, 0.0f,  0.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 1.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 0.0f
    };
    unsigned int m_indices[6] = { 0, 1, 2, 0, 2, 3 };
};

#endif
