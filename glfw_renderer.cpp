#include "glfw_renderer.h"
#include <iostream>

static void glfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

GLFWRenderer::GLFWRenderer() : window(nullptr), shaderProgram(0), VBO(0), VAO(0), EBO(0), texture(0) {}

GLFWRenderer::~GLFWRenderer() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(shaderProgram);
    glDeleteTextures(1, &texture);
    glfwTerminate();
}

GLFWmonitor* GLFWRenderer::getTargetMonitor(int monitorIndex) {
    int monitorCount;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    
    if (monitorCount == 0) {
        std::cerr << "No monitors found!" << std::endl;
        return nullptr;
    }
    
    if (monitorIndex >= monitorCount) {
        std::cerr << "Monitor index " << monitorIndex << " out of range. Using primary monitor." << std::endl;
        return monitors[0];
    }
    
    return monitors[monitorIndex];
}

bool GLFWRenderer::init(int width, int height, const char* title, bool fullscreen, int monitorIndex) {
    if (!initGLFW()) return false;
    
    GLFWmonitor* monitor = nullptr;
    if (fullscreen) {
        monitor = getTargetMonitor(monitorIndex);
        if (!monitor) return false;
        
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        width = mode->width;
        height = mode->height;
    }

    m_width = width;
    m_height = height;
    window = glfwCreateWindow(width, height, title, monitor, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);

    if (!initGLAD()) return false;
    if (!createShaders()) return false;
    if (!setupBuffers()) return false;

    // Create textures (main + U/UV + V for YUV formats)
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Initialize UV/V textures with 1x1 dummy data so the driver
    // doesn't mark them as unloadable before real data arrives.
    unsigned char dummy = 128;

    glGenTextures(1, &m_uvTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_uvTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &dummy);

    glGenTextures(1, &m_vTexture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_vTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &dummy);

    glActiveTexture(GL_TEXTURE0);

    glUseProgram(shaderProgram);
    glUniform1i(glGetUniformLocation(shaderProgram, "screenTexture"), 0);
    glUniform1i(glGetUniformLocation(shaderProgram, "uvTexture"), 1);
    glUniform1i(glGetUniformLocation(shaderProgram, "vTexture"), 2);

    return true;
}

void GLFWRenderer::render(const Texture& texture) {
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shaderProgram);
    glUniform1i(colorFormatLocation, static_cast<int>(texture.format));
    glUniform1i(rotationLocation, m_displayRotation);

    if (texture.format == ColorFormat::NV12) {
        // NV12: Y plane (full res, single channel) + UV interleaved (half res, two channels)
        size_t ySize = (size_t)texture.width * texture.height;

        glUniform1i(glGetUniformLocation(shaderProgram, "screenTexture"), 0);
        glUniform1i(glGetUniformLocation(shaderProgram, "uvTexture"), 1);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, this->texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture.width, texture.height,
                     0, GL_RED, GL_UNSIGNED_BYTE, texture.pixels);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_uvTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, texture.width / 2, texture.height / 2,
                     0, GL_RG, GL_UNSIGNED_BYTE, texture.pixels + ySize);
    } else {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, this->texture);
        int uploadWidth = (texture.format == ColorFormat::UYVY) ? texture.width / 2 : texture.width;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, uploadWidth, texture.height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, texture.pixels);
    }

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void GLFWRenderer::renderOverlay(const Texture& overlay) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(shaderProgram);
    glUniform1i(colorFormatLocation, static_cast<int>(ColorFormat::RGBA));
    glUniform1i(rotationLocation, 0);

    glBindTexture(GL_TEXTURE_2D, this->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, overlay.width, overlay.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, overlay.pixels);

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    glDisable(GL_BLEND);
}

void GLFWRenderer::present() {
    glfwSwapBuffers(window);
    glfwPollEvents();
}

bool GLFWRenderer::shouldClose() const {
    return glfwWindowShouldClose(window);
}

void GLFWRenderer::processInput() {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

bool GLFWRenderer::initGLFW() {
    // Set error callback before initialization
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    #ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
        glfwWindowHint(GLFW_COCOA_GRAPHICS_SWITCHING, GLFW_TRUE);
    #endif

    // Print available video modes for primary monitor
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    if (primaryMonitor) {
        int count;
        const GLFWvidmode* modes = glfwGetVideoModes(primaryMonitor, &count);
        std::cout << "Available video modes:" << std::endl;
        for (int i = 0; i < count; i++) {
            std::cout << "  " << modes[i].width << "x" << modes[i].height 
                     << " @ " << modes[i].refreshRate << "Hz" << std::endl;
        }
    }

    return true;
}

bool GLFWRenderer::initGLAD() {
    if (!gladLoadGL(glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return false;
    }
    
    // Check OpenGL version support (now that context is active)
    const char* glVersion = (const char*)glGetString(GL_VERSION);
    std::cout << "OpenGL Version: " << (glVersion ? glVersion : "unknown") << std::endl;
    
    return true;
}

bool GLFWRenderer::createShaders() {
    std::vector<char> vertexShaderCode = loader.LoadShader("vertex.glsl");
    std::vector<char> fragmentShaderCode = loader.LoadShader("fragment.glsl");
    
    const char* vertexShaderSource = vertexShaderCode.data();
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
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Check program linking
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cerr << "Shader program linking failed:\n" << infoLog << std::endl;
        return false;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Get uniform location
    colorFormatLocation = glGetUniformLocation(shaderProgram, "colorFormat");
    rotationLocation = glGetUniformLocation(shaderProgram, "displayRotation");

    return true;
}

bool GLFWRenderer::setupBuffers() {
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    return true;
}
