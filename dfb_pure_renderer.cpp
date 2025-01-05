#ifdef HAVE_DIRECTFB
#include "dfb_pure_renderer.h"
#include <iostream>

DirectFBPureRenderer::DirectFBPureRenderer() 
    : m_dfb(nullptr), m_primary(nullptr), m_texture(nullptr), m_shouldClose(false) {}

DirectFBPureRenderer::~DirectFBPureRenderer() {
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
    desc.flags = (DFBSurfaceDescriptionFlags)(DSDESC_CAPS | DSDESC_WIDTH | DSDESC_HEIGHT);
    desc.caps = (DFBSurfaceCapabilities)(DSCAPS_PRIMARY);
    desc.width = width;
    desc.height = height;

    result = m_dfb->CreateSurface(m_dfb, &desc, &m_primary);
    if (result != DFB_OK) {
        std::cerr << "Failed to create primary surface: " << DirectFBErrorString(result) << std::endl;
        return false;
    }

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
    if (!m_texture || 
        texture.width != m_width || 
        texture.height != m_height) {
        
        if (m_texture) {
            m_texture->Release(m_texture);
        }

        DFBSurfaceDescription desc;
        desc.flags = (DFBSurfaceDescriptionFlags)(DSDESC_WIDTH | DSDESC_HEIGHT | DSDESC_PIXELFORMAT);
        desc.width = texture.width;
        desc.height = texture.height;
        desc.pixelformat = DSPF_RGBA;  // Changed to match input format

        if (m_dfb->CreateSurface(m_dfb, &desc, &m_texture) != DFB_OK) {
            std::cerr << "Failed to create texture surface" << std::endl;
            return;
        }
    }

    void* dest;
    int pitch;
    if (m_texture->Lock(m_texture, DSLF_WRITE, &dest, &pitch) != DFB_OK) {
        std::cerr << "Failed to lock texture surface" << std::endl;
        return;
    }

    // Direct pixel copy without conversion
    const uint32_t* srcPixels = reinterpret_cast<const uint32_t*>(texture.pixels);
    uint32_t* destPixels = static_cast<uint32_t*>(dest);
    
    for (int y = 0; y < texture.height; ++y) {
        memcpy(&destPixels[y * (pitch/4)], 
               &srcPixels[y * texture.width], 
               texture.width * sizeof(uint32_t));
    }

    m_texture->Unlock(m_texture);

    // Calculate scaling and position
    DFBRectangle srcRect = { 0, 0, (int)texture.width, (int)texture.height };
    DFBRectangle dstRect;
    
    if (m_fullscreenScaling) {
        // Scale to full screen
        dstRect = { 0, 0, m_width, m_height };
    } else {
        // Center without scaling
        dstRect = { (m_width - texture.width) / 2, 
                   (m_height - texture.height) / 2,
                   (int)texture.width, 
                   (int)texture.height };
    }

    m_primary->StretchBlit(m_primary, m_texture, &srcRect, &dstRect);
    m_primary->Flip(m_primary, nullptr, DSFLIP_WAITFORSYNC);
}

bool DirectFBPureRenderer::shouldClose() const {
    return m_shouldClose;
}

#endif // HAVE_DIRECTFB
