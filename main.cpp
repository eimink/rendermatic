#include "renderer.h"
#include "loader.h"
#include "config.h"
#include <iostream>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [-f] [-m monitor_index]\n"
              << "  -w            : Launch in windowed mode\n"
              << "  -m <index>    : Specify monitor index (default: 0)\n";
}

int main(int argc, char* argv[]) {
    bool fullscreen = true;
    int monitorIndex = 0;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-w") {
            fullscreen = false;
        } else if (arg == "-m" && i + 1 < argc) {
            monitorIndex = std::atoi(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    Renderer renderer;
    Loader loader;

    if (!renderer.init(WIDTH, HEIGHT, "Image Buffer Display", fullscreen, monitorIndex)) {
        return -1;
    }

    // Load texture
    Texture loadedTex = loader.LoadTexture("safety_cat_ears.png", ColorFormat::UYVA);

    // Main loop
    while (!renderer.shouldClose()) {
        renderer.processInput();
        renderer.render(loadedTex);
    }

    return 0;
}