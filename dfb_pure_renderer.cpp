#ifdef HAVE_DIRECTFB
#include "dfb_pure_renderer.h"
#include <iostream>

DirectFBPureRenderer::DirectFBPureRenderer() = default;

DirectFBPureRenderer::~DirectFBPureRenderer() {
    if (m_overlaySurface) {
        m_overlaySurface->Release(m_overlaySurface);
    }
    if (m_texture) {
        m_texture->Release(m_texture);
    }
    if (m_primary) {
        m_primary->Release(m_primary);
    }
    if (m_dfb) {
        m_dfb->Release(m_dfb);
    }
}

bool DirectFBPureRenderer::init(int width, int height, const char* title, bool fullscreen, int monitorIndex) {
    m_width = width;
    m_height = height;
    DFBResult result;

    result = DirectFBInit(nullptr, nullptr);
    if (result != DFB_OK) {
        std::cerr << "Failed to initialize DirectFB: " << DirectFBErrorString(result) << std::endl;
        return false;
    }

    result = DirectFBCreate(&m_dfb);
    if (result != DFB_OK) {
        std::cerr << "Failed to create DirectFB interface: " << DirectFBErrorString(result) << std::endl;
        return false;
    }

    if (fullscreen) {
        m_dfb->SetCooperativeLevel(m_dfb, DFSCL_FULLSCREEN);
    }

    DFBSurfaceDescription desc;
    desc.flags = (DFBSurfaceDescriptionFlags)(DSDESC_CAPS);
    desc.caps = (DFBSurfaceCapabilities)(DSCAPS_PRIMARY | DSCAPS_DOUBLE);

    result = m_dfb->CreateSurface(m_dfb, &desc, &m_primary);
    if (result != DFB_OK) {
        std::cerr << "Failed to create primary surface: " << DirectFBErrorString(result) << std::endl;
        return false;
    }

    // Query actual framebuffer resolution from the display
    m_primary->GetSize(m_primary, &m_width, &m_height);
    std::cout << "Display resolution: " << m_width << "x" << m_height << std::endl;

    m_primary->Clear(m_primary, 0, 0, 0, 0xFF);
    return true;
}

void DirectFBPureRenderer::processInput() {
    DFBInputEvent event;
    IDirectFBEventBuffer* eventBuffer;
    
    if (m_dfb->CreateInputEventBuffer(m_dfb, DICAPS_KEYS, DFB_FALSE, &eventBuffer) == DFB_OK) {
        if (eventBuffer->GetEvent(eventBuffer, DFB_EVENT(&event)) == DFB_OK) {
            if (event.type == DIET_KEYPRESS && event.key_symbol == DIKS_ESCAPE) {
                m_shouldClose = true;
            }
        }
        eventBuffer->Release(eventBuffer);
    }
}

void DirectFBPureRenderer::render(const Texture& texture) {
    if (!texture.pixels) return;

    // For 90°/270° rotation, the output surface has swapped dimensions
    int outW = texture.width;
    int outH = texture.height;
    if (m_displayRotation == 1 || m_displayRotation == 3) {
        outW = texture.height;
        outH = texture.width;
    }

    if (!m_texture ||
        outW != m_texW || outH != m_texH) {

        if (m_texture) {
            m_texture->Release(m_texture);
        }

        DFBSurfaceDescription desc;
        desc.flags = (DFBSurfaceDescriptionFlags)(DSDESC_WIDTH | DSDESC_HEIGHT | DSDESC_PIXELFORMAT);
        desc.width = outW;
        desc.height = outH;
        desc.pixelformat = DSPF_ARGB;

        if (m_dfb->CreateSurface(m_dfb, &desc, &m_texture) != DFB_OK) {
            std::cerr << "Failed to create texture surface" << std::endl;
            return;
        }
        m_texW = outW;
        m_texH = outH;
    }

    void* dest;
    int pitch;
    if (m_texture->Lock(m_texture, DSLF_WRITE, &dest, &pitch) != DFB_OK) {
        std::cerr << "Failed to lock texture surface" << std::endl;
        return;
    }

    uint32_t* destPixels = static_cast<uint32_t*>(dest);
    const uint32_t* srcPixels = reinterpret_cast<const uint32_t*>(texture.pixels);
    int srcW = texture.width;
    int srcH = texture.height;

    auto swapRB = [](uint32_t rgba) -> uint32_t {
        return (rgba & 0x000000FF) << 16 |
               (rgba & 0x0000FF00) |
               (rgba & 0x00FF0000) >> 16 |
               (rgba & 0xFF000000);
    };

    for (int dy = 0; dy < outH; ++dy) {
        for (int dx = 0; dx < outW; ++dx) {
            int sx, sy;
            switch (m_displayRotation) {
                case 1: // 90° CW
                    sx = dy;
                    sy = outW - 1 - dx;
                    break;
                case 2: // 180°
                    sx = srcW - 1 - dx;
                    sy = srcH - 1 - dy;
                    break;
                case 3: // 270° CW
                    sx = outH - 1 - dy;
                    sy = dx;
                    break;
                default: // 0°
                    sx = dx;
                    sy = dy;
                    break;
            }
            destPixels[dy * (pitch/4) + dx] = swapRB(srcPixels[sy * srcW + sx]);
        }
    }

    m_texture->Unlock(m_texture);

    DFBRectangle srcRect = { 0, 0, outW, outH };
    DFBRectangle dstRect;

    if (m_fullscreenScaling) {
        dstRect = { 0, 0, m_width, m_height };
    } else {
        dstRect = { (m_width - outW) / 2,
                   (m_height - outH) / 2,
                   outW, outH };
    }

    m_primary->StretchBlit(m_primary, m_texture, &srcRect, &dstRect);
}

void DirectFBPureRenderer::renderOverlay(const Texture& overlay) {
    if (!overlay.pixels || !m_primary) return;

    // Create or recreate overlay surface if needed
    if (!m_overlaySurface) {
        DFBSurfaceDescription desc;
        desc.flags = (DFBSurfaceDescriptionFlags)(DSDESC_WIDTH | DSDESC_HEIGHT | DSDESC_PIXELFORMAT | DSDESC_CAPS);
        desc.width = overlay.width;
        desc.height = overlay.height;
        desc.pixelformat = DSPF_ARGB;
        desc.caps = DSCAPS_NONE;

        if (m_dfb->CreateSurface(m_dfb, &desc, &m_overlaySurface) != DFB_OK) {
            std::cerr << "Failed to create overlay surface" << std::endl;
            return;
        }
    }

    // Upload overlay pixels
    void* dest;
    int pitch;
    if (m_overlaySurface->Lock(m_overlaySurface, DSLF_WRITE, &dest, &pitch) != DFB_OK) return;

    uint32_t* destPixels = static_cast<uint32_t*>(dest);
    const uint32_t* srcPixels = reinterpret_cast<const uint32_t*>(overlay.pixels);

    for (int y = 0; y < overlay.height; ++y) {
        for (int x = 0; x < overlay.width; ++x) {
            uint32_t rgba = srcPixels[y * overlay.width + x];
            uint32_t bgra = (rgba & 0x000000FF) << 16 |
                           (rgba & 0x0000FF00) |
                           (rgba & 0x00FF0000) >> 16 |
                           (rgba & 0xFF000000);
            destPixels[y * (pitch/4) + x] = bgra;
        }
    }

    m_overlaySurface->Unlock(m_overlaySurface);

    // Blit with alpha blending onto primary
    m_primary->SetBlittingFlags(m_primary, DSBLIT_BLEND_ALPHACHANNEL);
    DFBRectangle srcRect = { 0, 0, overlay.width, overlay.height };
    DFBRectangle dstRect = { 0, 0, m_width, m_height };
    m_primary->StretchBlit(m_primary, m_overlaySurface, &srcRect, &dstRect);
    m_primary->SetBlittingFlags(m_primary, DSBLIT_NOFX);
}

void DirectFBPureRenderer::present() {
    if (m_primary) {
        m_primary->Flip(m_primary, nullptr, DSFLIP_WAITFORSYNC);
    }
}

bool DirectFBPureRenderer::shouldClose() const {
    return m_shouldClose;
}

#endif // HAVE_DIRECTFB
