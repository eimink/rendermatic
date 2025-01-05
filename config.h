#pragma once
#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

class Configuration {
public:
    bool fullscreen;
    bool fullscreenScaling;
    int monitorIndex;
    bool ndiMode;
    std::string backend;
    int width;
    int height;

    static Configuration loadFromFile(const std::string& path = "config.json");
    void overrideFromCommandLine(int argc, char* argv[]);
    
private:
    void printUsage(const char* programName);
};

const std::string SHADER_PATH = "shaders/";
const std::string TEXTURE_PATH = "textures/";

// Default display configuration
constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 800;

constexpr int MAX_FRAMES_IN_FLIGHT = 2;