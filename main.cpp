#include "glfw_renderer.h"
#include "dfb_renderer.h"
#include "dfb_pure_renderer.h"
#include "renderer.h"
#include "loader.h"
#include "config.h"
#include "ndireceiver.h"
#include <iostream>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [-w] [-m monitor_index] [-n] [-b backend] [-r width height] [-f]\n"
              << "  -w            : Launch in windowed mode\n"
              << "  -m <index>    : Specify monitor index (default: 0)\n"
              << "  -n            : Enable NDI mode\n"
              << "  -b <backend>  : Rendering backend (glfw/dfb/dfb-pure, default: glfw)\n"
              << "  -r <w> <h>    : Set resolution (default: " << WIDTH << "x" << HEIGHT << ")\n"
              << "  -f            : Enable fullscreen scaling\n";
}

int main(int argc, char* argv[]) {
    auto config = Configuration::loadFromFile();
    config.overrideFromCommandLine(argc, argv);

    std::unique_ptr<IRenderer> renderer;
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

    Loader loader;
    std::unique_ptr<NDIReceiver> ndiReceiver;

    if (!renderer->init(config.width, config.height, "Display", config.fullscreen, config.monitorIndex)) {
        return -1;
    }
    renderer->setFullscreenScaling(config.fullscreenScaling);

    // Load default texture for image mode
    Texture displayTexture = loader.LoadTexture("safety_cat_ears.jpg", ColorFormat::RGBA);

    if (config.ndiMode) {
        ndiReceiver = std::make_unique<NDIReceiver>();
        ndiReceiver->start();
    }

    // Main loop
    while (!renderer->shouldClose()) {
        renderer->processInput();
        
        if (config.ndiMode && ndiReceiver) {
            Texture currentFrame;
            if (ndiReceiver->getLatestFrame(currentFrame)) {
                displayTexture = currentFrame;
            }
        }

        renderer->render(displayTexture);
    }

    if (ndiReceiver) {
        ndiReceiver->stop();
    }

    return 0;
}