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
        desc.pixelformat = DSPF_ARGB;

        if (m_dfb->CreateSurface(m_dfb, &desc, &m_texture) != DFB_OK) {
            std::cerr << "Failed to create texture surface" << std::endl;
            return;
        }
    }

    m_texture->Lock(m_texture, DSLF_WRITE, (void**)&texture.pixels, nullptr);
    m_texture->Unlock(m_texture);

    DFBRectangle rect = { 0, 0, (int)texture.width, (int)texture.height };
    m_primary->Blit(m_primary, m_texture, &rect, 0, 0);
    m_primary->Flip(m_primary, nullptr, DSFLIP_WAITFORSYNC);
}

bool DirectFBPureRenderer::shouldClose() const {
    return m_shouldClose;
}

#endif // HAVE_DIRECTFB
