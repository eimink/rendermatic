#pragma once
#include "irenderer.h"
#include <glad/gl.h>
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
    void renderOverlay(const Texture& overlay) override;
    void present() override;
    bool shouldClose() const override;
    int getWidth() const override { return m_width; }
    int getHeight() const override { return m_height; }
    void setRotation(int degrees) override { m_displayRotation = degrees / 90; }
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
    unsigned int m_uvTexture = 0;
    unsigned int m_vTexture = 0;
    int colorFormatLocation;
    int rotationLocation = -1;
    int m_displayRotation = 0;
    Loader loader;
    int m_width = 0;
    int m_height = 0;

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
