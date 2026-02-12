#include "dfb_pure_renderer.h"
#include "irenderer.h"
#include "loader.h"
#include "config.h"
#include "ndireceiver.h"
#include "texture_manager.h"
#include "websocket_server.h"
#include <iostream>
#include <filesystem>
#include <libgen.h>

#ifdef HAVE_DIRECTFB
    #include "dfb_renderer.h"
#endif

#ifdef ENABLE_GLFW
    #include "glfw_renderer.h"
#endif

int main(int argc, char* argv[]) {
    // Change working directory to the executable's directory
    // This allows the app to find shaders, textures, and config.json
    std::string exePath = argv[0];
    std::string exeDir = std::filesystem::path(exePath).parent_path().string();
    if (!exeDir.empty()) {
        try {
            std::filesystem::current_path(exeDir);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not change to executable directory: " << e.what() << std::endl;
        }
    }
    
    auto config = Configuration::loadFromFile();
    config.overrideFromCommandLine(argc, argv);

    std::unique_ptr<IRenderer> renderer;

#ifdef DFB_PURE_ONLY
    renderer = std::make_unique<DirectFBPureRenderer>();
#else
#ifdef DFB_ONLY
    renderer = std::make_unique<DirectFBRenderer>();
#else
    if (config.backend == "glfw") {
        renderer = std::make_unique<GLFWRenderer>();
    } else if (config.backend == "dfb") {
#ifdef HAVE_DIRECTFB
        renderer = std::make_unique<DirectFBRenderer>();
#else
        std::cerr << "DirectFB support not compiled in" << std::endl;
        return -1;
#endif
    } else if (config.backend == "dfb-pure") {
#ifdef HAVE_DIRECTFB
        renderer = std::make_unique<DirectFBPureRenderer>();
#else
        std::cerr << "Pure DirectFB support not compiled in" << std::endl;
        return -1;
#endif
    } else {
        std::cerr << "Unknown backend: " << config.backend << std::endl;
        std::cerr << "Available backends: glfw";
#ifdef HAVE_DIRECTFB
        std::cerr << ", dfb, dfb-pure";
#endif
        std::cerr << std::endl;
        return -1;
    }
#endif
#endif

    std::unique_ptr<NDIReceiver> ndiReceiver;

    TextureManager textureManager;
    std::string textureName;
    textureManager.scanTextureDirectory();  // Scan current directory for textures

    // Initialize WebSocket server
    WebSocketServer wsServer(textureManager);
    wsServer.start();
    
    if (!renderer->init(config.width, config.height, "Display", config.fullscreen, config.monitorIndex)) {
        return -1;
    }
    renderer->setFullscreenScaling(config.fullscreenScaling);

    // Load default texture for image mode
    if (!textureManager.loadTexture("default.jpg")) {
        std::cerr << "Failed to load default texture" << std::endl;
        return -1;
    }
    textureManager.setCurrentTexture("default.jpg");
    Texture* displayTexture = textureManager.getCurrentTexture();

    if (config.ndiMode) {
        ndiReceiver = std::make_unique<NDIReceiver>();
        ndiReceiver->start();
    }

    // Main loop
    while (!renderer->shouldClose()) {
        renderer->processInput();
        
        if (textureManager.getCurrentTextureName() != textureName) {
            displayTexture = textureManager.getCurrentTexture();
            textureName = textureManager.getCurrentTextureName();
        }

        if (config.ndiMode && ndiReceiver) {
            Texture currentFrame;
            if (ndiReceiver->getLatestFrame(currentFrame)) {
                *displayTexture = currentFrame;
            }
        }

        renderer->render(*displayTexture);
    }

    if (ndiReceiver) {
        ndiReceiver->stop();
    }

    // Before return, stop WebSocket server
    wsServer.stop();
    return 0;
}