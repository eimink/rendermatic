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
    bool shouldClose() const override;

private:
    IDirectFB* m_dfb;
    IDirectFBSurface* m_primary;
    IDirectFBSurface* m_texture;
    bool m_shouldClose;
    int m_width;
    int m_height;
};

#endif // HAVE_DIRECTFB
