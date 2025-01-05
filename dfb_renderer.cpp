#ifdef HAVE_DIRECTFB
#include "dfb_renderer.h"
#include <iostream>
#include <glad/glad.h>

DirectFBRenderer::DirectFBRenderer() 
    : m_dfb(nullptr), m_primary(nullptr), m_shouldClose(false), 
      m_gl(nullptr), m_shader(0), m_vao(0), m_vbo(0), 
      m_ebo(0), m_texture(0) {}

DirectFBRenderer::~DirectFBRenderer() {
    if (m_gl) {
        glDeleteProgram(m_shader);
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_vbo);
        glDeleteBuffers(1, &m_ebo);
        glDeleteTextures(1, &m_texture);
        m_gl->Release(m_gl);
    }
    if (m_primary) {
        m_primary->Release(m_primary);
    }
    if (m_dfb) {
        m_dfb->Release(m_dfb);
    }
}

bool DirectFBRenderer::init(int width, int height, const char* title, bool fullscreen, int monitorIndex) {
    m_width = width;
    m_height = height;
    DFBResult result;
    
    // Initialize DirectFB with OpenGL support
    result = DirectFBInit(nullptr, nullptr);
    if (result != DFB_OK) {
        std::cerr << "Failed to initialize DirectFB: " << DirectFBErrorString(result) << std::endl;
        return false;
    }

    // Create the super interface with OpenGL support
    result = DirectFBCreate(&m_dfb);
    if (result != DFB_OK) {
        std::cerr << "Failed to create DirectFB interface: " << DirectFBErrorString(result) << std::endl;
        return false;
    }

    // Set cooperative level
    if (fullscreen) {
        m_dfb->SetCooperativeLevel(m_dfb, DFSCL_FULLSCREEN);
    }

    // Create primary surface with OpenGL capabilities
    DFBSurfaceDescription desc;
    desc.flags = (DFBSurfaceDescriptionFlags)(DSDESC_CAPS | DSDESC_WIDTH | DSDESC_HEIGHT);
    desc.caps = (DFBSurfaceCapabilities)(DSCAPS_PRIMARY | DSCAPS_GL);
    desc.width = width;
    desc.height = height;

    result = m_dfb->CreateSurface(m_dfb, &desc, &m_primary);
    if (result != DFB_OK) {
        std::cerr << "Failed to create primary surface: " << DirectFBErrorString(result) << std::endl;
        return false;
    }

    // Get OpenGL interface
    result = m_primary->GetGL(m_primary, &m_gl);
    if (result != DFB_OK) {
        std::cerr << "Failed to get OpenGL interface: " << DirectFBErrorString(result) << std::endl;
        return false;
    }

    // Initialize OpenGL context
    if (!m_gl->Lock(m_gl)) {
        std::cerr << "Failed to lock GL surface" << std::endl;
        return false;
    }

    if (!initGL()) {
        m_gl->Unlock(m_gl);
        return false;
    }

    setupGLState();
    m_gl->Unlock(m_gl);
    return true;
}

void DirectFBRenderer::processInput() {
    // In a real implementation, you would process DirectFB input events here
    // For now, we'll just check for a specific key to close
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

void DirectFBRenderer::render(const Texture& texture) {
    if (!m_gl->Lock(m_gl)) {
        std::cerr << "Failed to lock GL surface for rendering" << std::endl;
        return;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_shader);
    glUniform1i(m_colorFormatLocation, static_cast<int>(texture.format));
    glUniform1i(m_scalingLocation, m_fullscreenScaling ? 1 : 0);
    
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture.width, texture.height, 
                 0, GL_RGBA, GL_UNSIGNED_BYTE, texture.pixels);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    
    m_gl->Unlock(m_gl);
    m_primary->Flip(m_primary, NULL, DSFLIP_WAITFORSYNC);
}

bool DirectFBRenderer::shouldClose() const {
    return m_shouldClose;
}

bool DirectFBRenderer::initGL() {
    if (!gladLoadGL()) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return false;
    }

    // Create and compile shaders with scaling support
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aTexCoord;
        uniform bool fullscreenScaling;
        out vec2 TexCoord;
        
        void main() {
            vec3 pos = aPos;
            if (fullscreenScaling) {
                pos = vec3(aPos.x * 2.0, aPos.y * 2.0, aPos.z);
            }
            gl_Position = vec4(pos, 1.0);
            TexCoord = aTexCoord;
        }
    )";

    std::vector<char> fragmentShaderCode = m_loader.LoadShader("fragment.glsl");
    const char* fragmentShaderSource = fragmentShaderCode.data();

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    // Check vertex shader compilation
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cerr << "Vertex shader compilation failed:\n" << infoLog << std::endl;
        return false;
    }

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    // Check fragment shader compilation
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cerr << "Fragment shader compilation failed:\n" << infoLog << std::endl;
        return false;
    }

    // Create shader program
    m_shader = glCreateProgram();
    glAttachShader(m_shader, vertexShader);
    glAttachShader(m_shader, fragmentShader);
    glLinkProgram(m_shader);

    // Check program linking
    glGetProgramiv(m_shader, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(m_shader, 512, NULL, infoLog);
        std::cerr << "Shader program linking failed:\n" << infoLog << std::endl;
        return false;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Get uniform locations
    m_colorFormatLocation = glGetUniformLocation(m_shader, "colorFormat");
    m_scalingLocation = glGetUniformLocation(m_shader, "fullscreenScaling");

    // Setup buffers
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

    return true;
}

void DirectFBRenderer::setupGLState() {
    glViewport(0, 0, m_width, m_height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

#endif // HAVE_DIRECTFB
