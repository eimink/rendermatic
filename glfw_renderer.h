#pragma once
#include "irenderer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "loader.h"
#include "texture.h"

class GLFWRenderer : public IRenderer {

public:
    GLFWRenderer();
    ~GLFWRenderer();

    bool init(int width, int height, const char* title, 
             bool fullscreen = false, int monitorIndex = 0) override;
    void render(const Texture& texture) override;
    bool shouldClose() const override; // Changed to match pure virtual signature
    void processInput() override;
    GLFWwindow* getWindow() { return window; }

private:
    bool initGLFW();
    bool initGLAD();
    bool createShaders();
    bool setupBuffers();

    GLFWmonitor* getTargetMonitor(int monitorIndex);

    GLFWwindow* window;
    unsigned int shaderProgram;
    unsigned int VBO, VAO, EBO;
    unsigned int texture;
    int colorFormatLocation;
    Loader loader;

    // Vertex data
    float vertices[20] = {
        -1.0f,  1.0f, 0.0f,  0.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 1.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 0.0f
    };
    unsigned int indices[6] = {
        0, 1, 2,
        0, 2, 3
    };
};
