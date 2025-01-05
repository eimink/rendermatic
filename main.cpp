#include "glfw_renderer.h"
#include "dfb_renderer.h"
#include "dfb_pure_renderer.h"
#include "renderer.h"
#include "loader.h"
#include "config.h"
#include "ndireceiver.h"
#include <iostream>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [-w] [-m monitor_index] [-n] [-b backend]\n"
              << "  -w            : Launch in windowed mode\n"
              << "  -m <index>    : Specify monitor index (default: 0)\n"
              << "  -n            : Enable NDI mode\n"
              << "  -b <backend>  : Rendering backend (glfw/dfb/dfb-pure, default: glfw)\n";
}

int main(int argc, char* argv[]) {
    bool fullscreen = true;
    int monitorIndex = 0;
    bool ndiMode = false;
    std::string backend = "glfw";

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-w") {
            fullscreen = false;
        } else if (arg == "-m" && i + 1 < argc) {
            monitorIndex = std::atoi(argv[++i]);
        } else if (arg == "-n") {
            ndiMode = true;
        } else if (arg == "-b" && i + 1 < argc) {
            backend = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    std::unique_ptr<IRenderer> renderer;
    if (backend == "glfw") {
        renderer = std::make_unique<GLFWRenderer>();
    } else if (backend == "dfb") {
#ifdef HAVE_DIRECTFB
        renderer = std::make_unique<DirectFBRenderer>();
#else
        std::cerr << "DirectFB support not compiled in" << std::endl;
        return -1;
#endif
    } else if (backend == "dfb-pure") {
#ifdef HAVE_DIRECTFB
        renderer = std::make_unique<DirectFBPureRenderer>();
#else
        std::cerr << "Pure DirectFB support not compiled in" << std::endl;
        return -1;
#endif
    } else {
        std::cerr << "Unknown backend: " << backend << std::endl;
        std::cerr << "Available backends: glfw";
#ifdef HAVE_DIRECTFB
        std::cerr << ", dfb, dfb-pure";
#endif
        std::cerr << std::endl;
        return -1;
    }

    Loader loader;
    std::unique_ptr<NDIReceiver> ndiReceiver;

    if (!renderer->init(WIDTH, HEIGHT, "Display", fullscreen, monitorIndex)) {
        return -1;
    }

    // Load default texture for image mode
    Texture displayTexture = loader.LoadTexture("safety_cat_ears.png", ColorFormat::UYVA);

    if (ndiMode) {
        ndiReceiver = std::make_unique<NDIReceiver>();
        ndiReceiver->start();
    }

    // Main loop
    while (!renderer->shouldClose()) {
        renderer->processInput();
        
        if (ndiMode && ndiReceiver) {
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