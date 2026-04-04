#ifdef DFB_ONLY
#include "drm_egl_renderer.h"
#include "texture.h"
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/gl.h>

DrmEglRenderer::DrmEglRenderer() {}

DrmEglRenderer::~DrmEglRenderer() {
    if (m_shader) glDeleteProgram(m_shader);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_texture) glDeleteTextures(1, &m_texture);

    auto display = static_cast<EGLDisplay>(m_eglDisplay);
    if (m_eglSurface) eglDestroySurface(display, static_cast<EGLSurface>(m_eglSurface));
    if (m_eglContext) eglDestroyContext(display, static_cast<EGLContext>(m_eglContext));
    if (m_eglDisplay) eglTerminate(display);

    // Restore original CRTC
    if (m_drmFd >= 0 && m_crtcFbId) {
        auto mode = static_cast<drmModeModeInfo*>(m_drmMode);
        drmModeSetCrtc(m_drmFd, m_crtcId, m_crtcFbId, 0, 0, &m_connectorId, 1, mode);
    }

    if (m_prevBo) {
        drmModeRmFB(m_drmFd, m_prevFb);
        gbm_surface_release_buffer(m_gbmSurface, m_prevBo);
    }
    if (m_gbmSurface) gbm_surface_destroy(m_gbmSurface);
    if (m_gbmDevice) gbm_device_destroy(m_gbmDevice);
    if (m_drmMode) free(m_drmMode);
    if (m_drmFd >= 0) close(m_drmFd);
}

bool DrmEglRenderer::init(int width, int height, const char* title,
                           bool fullscreen, int monitorIndex) {
    (void)width; (void)height; (void)title; (void)fullscreen; (void)monitorIndex;

    // Suppress Mesa shader cache warnings on read-only rootfs
    setenv("MESA_SHADER_CACHE_DISABLE", "true", 0);

    if (!initDrm()) return false;
    if (!initGbm()) return false;
    if (!initEgl()) return false;
    if (!initGl()) return false;

    std::cout << "DRM/EGL renderer initialized: " << m_width << "x" << m_height << std::endl;
    return true;
}

bool DrmEglRenderer::initDrm() {
    // Try common DRI device paths
    const char* devices[] = { "/dev/dri/card0", "/dev/dri/card1", nullptr };
    for (const char** dev = devices; *dev; ++dev) {
        m_drmFd = open(*dev, O_RDWR | O_CLOEXEC);
        if (m_drmFd >= 0) break;
    }
    if (m_drmFd < 0) {
        std::cerr << "DRM/EGL: Failed to open DRI device" << std::endl;
        return false;
    }

    // Need DRM master for modesetting
    if (drmSetMaster(m_drmFd) < 0) {
        // Not fatal — might already be master or running as root
    }

    drmModeRes* res = drmModeGetResources(m_drmFd);
    if (!res) {
        std::cerr << "DRM/EGL: Failed to get DRM resources" << std::endl;
        return false;
    }

    // Find first connected connector
    drmModeConnector* connector = nullptr;
    for (int i = 0; i < res->count_connectors; i++) {
        connector = drmModeGetConnector(m_drmFd, res->connectors[i]);
        if (connector && connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0)
            break;
        if (connector) drmModeFreeConnector(connector);
        connector = nullptr;
    }

    if (!connector) {
        std::cerr << "DRM/EGL: No connected display found" << std::endl;
        drmModeFreeResources(res);
        return false;
    }

    m_connectorId = connector->connector_id;

    // Use preferred mode (or first available)
    drmModeModeInfo* selectedMode = &connector->modes[0];
    for (int i = 0; i < connector->count_modes; i++) {
        if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            selectedMode = &connector->modes[i];
            break;
        }
    }

    m_width = selectedMode->hdisplay;
    m_height = selectedMode->vdisplay;

    // Save mode (need to keep it alive)
    m_drmMode = malloc(sizeof(drmModeModeInfo));
    memcpy(m_drmMode, selectedMode, sizeof(drmModeModeInfo));

    // Find CRTC for this connector
    drmModeEncoder* encoder = nullptr;
    if (connector->encoder_id) {
        encoder = drmModeGetEncoder(m_drmFd, connector->encoder_id);
    }
    if (!encoder) {
        for (int i = 0; i < connector->count_encoders; i++) {
            encoder = drmModeGetEncoder(m_drmFd, connector->encoders[i]);
            if (encoder) break;
        }
    }

    if (!encoder) {
        std::cerr << "DRM/EGL: No encoder found" << std::endl;
        drmModeFreeConnector(connector);
        drmModeFreeResources(res);
        return false;
    }

    if (encoder->crtc_id) {
        m_crtcId = encoder->crtc_id;
    } else {
        // Find available CRTC
        for (int i = 0; i < res->count_crtcs; i++) {
            if (encoder->possible_crtcs & (1 << i)) {
                m_crtcId = res->crtcs[i];
                break;
            }
        }
    }

    // Save original CRTC fb for cleanup
    drmModeCrtc* crtc = drmModeGetCrtc(m_drmFd, m_crtcId);
    if (crtc) {
        m_crtcFbId = crtc->buffer_id;
        drmModeFreeCrtc(crtc);
    }

    drmModeFreeEncoder(encoder);
    drmModeFreeConnector(connector);
    drmModeFreeResources(res);

    std::cout << "DRM/EGL: Display " << m_width << "x" << m_height
              << " on connector " << m_connectorId << std::endl;
    return true;
}

bool DrmEglRenderer::initGbm() {
    m_gbmDevice = gbm_create_device(m_drmFd);
    if (!m_gbmDevice) {
        std::cerr << "DRM/EGL: Failed to create GBM device" << std::endl;
        return false;
    }

    m_gbmSurface = gbm_surface_create(m_gbmDevice,
        m_width, m_height, GBM_FORMAT_XRGB8888,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!m_gbmSurface) {
        std::cerr << "DRM/EGL: Failed to create GBM surface" << std::endl;
        return false;
    }

    return true;
}

bool DrmEglRenderer::initEgl() {
    // Get EGL display from GBM device
    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, m_gbmDevice, nullptr);
    if (display == EGL_NO_DISPLAY) {
        // Fallback to legacy path
        display = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(m_gbmDevice));
    }
    if (display == EGL_NO_DISPLAY) {
        std::cerr << "DRM/EGL: Failed to get EGL display" << std::endl;
        return false;
    }
    m_eglDisplay = display;

    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        std::cerr << "DRM/EGL: Failed to initialize EGL" << std::endl;
        return false;
    }
    std::cout << "DRM/EGL: EGL " << major << "." << minor << std::endl;

    // Use OpenGL (not GLES) to keep shader compatibility
    if (!eglBindAPI(EGL_OPENGL_API)) {
        std::cerr << "DRM/EGL: Failed to bind OpenGL API, trying GLES" << std::endl;
        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            std::cerr << "DRM/EGL: Failed to bind any GL API" << std::endl;
            return false;
        }
    }

    // Find EGL config matching our GBM format
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };

    EGLConfig configs[32];
    EGLint numConfigs;
    if (!eglChooseConfig(display, configAttribs, configs, 32, &numConfigs) || numConfigs == 0) {
        std::cerr << "DRM/EGL: Failed to choose EGL config" << std::endl;
        return false;
    }

    // Pick config whose native visual ID matches GBM_FORMAT_XRGB8888
    EGLConfig config = configs[0];
    for (int i = 0; i < numConfigs; i++) {
        EGLint id;
        eglGetConfigAttrib(display, configs[i], EGL_NATIVE_VISUAL_ID, &id);
        if (id == GBM_FORMAT_XRGB8888) {
            config = configs[i];
            break;
        }
    }

    // Create OpenGL 3.3 core context
    EGLint contextAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };

    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        std::cerr << "DRM/EGL: GL 3.3 unavailable, trying 3.0" << std::endl;
        EGLint fallbackAttribs[] = {
            EGL_CONTEXT_MAJOR_VERSION, 3,
            EGL_CONTEXT_MINOR_VERSION, 0,
            EGL_NONE
        };
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, fallbackAttribs);
    }
    if (context == EGL_NO_CONTEXT) {
        std::cerr << "DRM/EGL: Failed to create EGL context: 0x" << std::hex << eglGetError() << std::dec << std::endl;
        return false;
    }
    m_eglContext = context;

    // Create EGL surface from GBM surface using platform API
    EGLSurface surface = eglCreatePlatformWindowSurface(display, config, m_gbmSurface, nullptr);
    if (surface == EGL_NO_SURFACE) {
        // Fallback to legacy call
        surface = eglCreateWindowSurface(display, config,
            reinterpret_cast<EGLNativeWindowType>(m_gbmSurface), nullptr);
    }
    if (surface == EGL_NO_SURFACE) {
        std::cerr << "DRM/EGL: Failed to create EGL surface: 0x" << std::hex << eglGetError() << std::dec << std::endl;
        return false;
    }
    m_eglSurface = surface;

    if (!eglMakeCurrent(display, surface, surface, context)) {
        std::cerr << "DRM/EGL: Failed to make EGL context current" << std::endl;
        return false;
    }

    return true;
}

bool DrmEglRenderer::initGl() {
    if (!gladLoadGL((GLADloadfunc)eglGetProcAddress)) {
        std::cerr << "DRM/EGL: Failed to load GL functions" << std::endl;
        return false;
    }

    const char* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    std::cout << "DRM/EGL: OpenGL " << (glVersion ? glVersion : "unknown")
              << " on " << (glRenderer ? glRenderer : "unknown") << std::endl;

    // Load shaders from files (same as GLFW renderer)
    std::vector<char> vertexShaderCode = m_loader.LoadShader("vertex.glsl");
    std::vector<char> fragmentShaderCode = m_loader.LoadShader("fragment.glsl");
    const char* vertexSrc = vertexShaderCode.data();
    const char* fragmentSrc = fragmentShaderCode.data();

    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexSrc, NULL);
    glCompileShader(vs);
    int success;
    char infoLog[512];
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vs, 512, NULL, infoLog);
        std::cerr << "DRM/EGL: Vertex shader failed: " << infoLog << std::endl;
        return false;
    }

    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentSrc, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fs, 512, NULL, infoLog);
        std::cerr << "DRM/EGL: Fragment shader failed: " << infoLog << std::endl;
        return false;
    }

    m_shader = glCreateProgram();
    glAttachShader(m_shader, vs);
    glAttachShader(m_shader, fs);
    glLinkProgram(m_shader);
    glGetProgramiv(m_shader, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(m_shader, 512, NULL, infoLog);
        std::cerr << "DRM/EGL: Shader link failed: " << infoLog << std::endl;
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    m_colorFormatLocation = glGetUniformLocation(m_shader, "colorFormat");
    m_rotationLocation = glGetUniformLocation(m_shader, "displayRotation");

    // Setup quad buffers
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(m_vertices), m_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(m_indices), m_indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Setup texture
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glViewport(0, 0, m_width, m_height);
    return true;
}

void DrmEglRenderer::render(const Texture& texture) {
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_shader);
    glUniform1i(m_colorFormatLocation, static_cast<int>(texture.format));
    glUniform1i(m_rotationLocation, m_displayRotation);

    glBindTexture(GL_TEXTURE_2D, m_texture);
    int uploadWidth = (texture.format == ColorFormat::UYVY) ? texture.width / 2 : texture.width;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, uploadWidth, texture.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, texture.pixels);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void DrmEglRenderer::renderOverlay(const Texture& overlay) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_shader);
    glUniform1i(m_colorFormatLocation, static_cast<int>(ColorFormat::RGBA));
    glUniform1i(m_rotationLocation, 0);

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, overlay.width, overlay.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, overlay.pixels);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glDisable(GL_BLEND);
}

void DrmEglRenderer::present() {
    auto display = static_cast<EGLDisplay>(m_eglDisplay);
    auto surface = static_cast<EGLSurface>(m_eglSurface);

    eglSwapBuffers(display, surface);

    // Get the front buffer from GBM
    gbm_bo* bo = gbm_surface_lock_front_buffer(m_gbmSurface);
    if (!bo) return;

    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t fb = 0;

    drmModeAddFB(m_drmFd, m_width, m_height, 24, 32, stride, handle, &fb);

    if (m_firstFrame) {
        auto mode = static_cast<drmModeModeInfo*>(m_drmMode);
        drmModeSetCrtc(m_drmFd, m_crtcId, fb, 0, 0, &m_connectorId, 1, mode);
        m_firstFrame = false;
    } else {
        // Page flip with vsync
        drmModePageFlip(m_drmFd, m_crtcId, fb, DRM_MODE_PAGE_FLIP_EVENT, nullptr);

        // Wait for flip to complete
        struct pollfd pfd = { m_drmFd, POLLIN, 0 };
        poll(&pfd, 1, 100); // 100ms timeout
        if (pfd.revents & POLLIN) {
            drmEventContext evctx = {};
            evctx.version = 2;
            drmHandleEvent(m_drmFd, &evctx);
        }
    }

    // Release previous buffer
    if (m_prevBo) {
        drmModeRmFB(m_drmFd, m_prevFb);
        gbm_surface_release_buffer(m_gbmSurface, m_prevBo);
    }
    m_prevBo = bo;
    m_prevFb = fb;
}

#endif
