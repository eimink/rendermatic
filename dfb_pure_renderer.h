#pragma once
#ifdef HAVE_DIRECTFB
#include "irenderer.h"
#include <directfb.h>

class DirectFBPureRenderer : public IRenderer {
public:
    DirectFBPureRenderer();
    ~DirectFBPureRenderer();

    bool init(int width, int height, const char* title, bool fullscreen, int monitorIndex) override;
    void processInput() override;
    void render(const Texture& texture) override;
    void renderOverlay(const Texture& overlay) override;
    void present() override;
    bool shouldClose() const override;
    int getWidth() const override { return m_width; }
    int getHeight() const override { return m_height; }
    void setRotation(int degrees) override { m_displayRotation = degrees / 90; }

private:
    IDirectFB* m_dfb = nullptr;
    IDirectFBSurface* m_primary = nullptr;
    IDirectFBSurface* m_texture = nullptr;
    IDirectFBSurface* m_overlaySurface = nullptr;
    bool m_shouldClose = false;
    int m_width = 0;
    int m_height = 0;
    int m_texW = 0;
    int m_texH = 0;
    int m_displayRotation = 0;
};

#endif // HAVE_DIRECTFB
