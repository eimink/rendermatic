#include "renderer.h"
#include "loader.h"
#include "config.h"
#include "ndireceiver.h"
#include <iostream>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [-w] [-m monitor_index] [-n]\n"
              << "  -w            : Launch in windowed mode\n"
              << "  -m <index>    : Specify monitor index (default: 0)\n"
              << "  -n            : Enable NDI mode\n";
}

int main(int argc, char* argv[]) {
    bool fullscreen = true;
    int monitorIndex = 0;
    bool ndiMode = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-w") {
            fullscreen = false;
        } else if (arg == "-m" && i + 1 < argc) {
            monitorIndex = std::atoi(argv[++i]);
        } else if (arg == "-n") {
            ndiMode = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    Renderer renderer;
    Loader loader;
    std::unique_ptr<NDIReceiver> ndiReceiver;

    if (!renderer.init(WIDTH, HEIGHT, "Display", fullscreen, monitorIndex)) {
        return -1;
    }

    // Load default texture for image mode
    Texture displayTexture = loader.LoadTexture("safety_cat_ears.png", ColorFormat::UYVA);

    if (ndiMode) {
        ndiReceiver = std::make_unique<NDIReceiver>();
        ndiReceiver->start();
    }

    // Main loop
    while (!renderer.shouldClose()) {
        renderer.processInput();
        
        if (ndiMode && ndiReceiver) {
            Texture currentFrame;
            if (ndiReceiver->getLatestFrame(currentFrame)) {
                displayTexture = currentFrame;
            }
        }

        renderer.render(displayTexture);
    }

    if (ndiReceiver) {
        ndiReceiver->stop();
    }

    return 0;
}