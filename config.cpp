#include "config.h"
#include <json/json.h>
#include <fstream>
#include <iostream>

Configuration Configuration::loadFromFile(const std::string& path) {
    Configuration config;
    try {
        std::ifstream file(path);
        if (file.is_open()) {
            Json::Value root;
            Json::Reader reader;
            if (reader.parse(file, root)) {
                config.fullscreen = root.get("fullscreen", true).asBool();
                config.fullscreenScaling = root.get("fullscreenScaling", false).asBool();
                config.monitorIndex = root.get("monitorIndex", 0).asInt();
                config.ndiMode = root.get("ndiMode", false).asBool();
                config.backend = root.get("backend", "glfw").asString();
                config.width = root.get("width", 1920).asInt();
                config.height = root.get("height", 1080).asInt();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        // Use defaults if config loading fails
        config.fullscreen = true;
        config.fullscreenScaling = false;
        config.monitorIndex = 0;
        config.ndiMode = false;
        config.backend = "glfw";
        config.width = 1920;
        config.height = 1080;
    }
    return config;
}

void Configuration::overrideFromCommandLine(int argc, char* argv[]) {
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
        } else if (arg == "-r" && i + 2 < argc) {
            width = std::atoi(argv[++i]);
            height = std::atoi(argv[++i]);
            if (width <= 0 || height <= 0) {
                std::cerr << "Invalid resolution: " << width << "x" << height << std::endl;
                exit(-1);
            }
        } else if (arg == "-f") {
            fullscreenScaling = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            exit(0);
        }
    }
}

void Configuration::printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [-w] [-m monitor_index] [-n] [-b backend] [-r width height] [-f]\n"
              << "  -w            : Launch in windowed mode\n"
              << "  -m <index>    : Specify monitor index (default: " << monitorIndex << ")\n"
              << "  -n            : Enable NDI mode\n"
              << "  -b <backend>  : Rendering backend (glfw/dfb/dfb-pure, default: " << backend << ")\n"
              << "  -r <w> <h>    : Set resolution (default: " << width << "x" << height << ")\n"
              << "  -f            : Enable fullscreen scaling\n";
}
