#pragma once
#include <string>
#include <cstdint>

struct Configuration {
    bool fullscreen = true;
    bool fullscreenScaling = false;
    int monitorIndex = 0;
    bool ndiMode = false;
    std::string backend = "glfw";
    int width = 1920;
    int height = 1080;
    uint16_t wsPort = 9002;
    std::string instanceName = "";  // Empty = auto-generate from hostname
    std::string videoSource = "";   // Video file path or stream URL
    bool videoMode = false;         // Use video decoder instead of static textures
    bool videoLoop = true;          // Loop video files (ignored for streams)
    std::string authKeyHash = "";   // SHA-256 hex of auth key; empty = no auth (open mode)
    int splashDurationSeconds = 5;  // Boot overlay duration; 0 = disabled

    static Configuration loadFromFile(const std::string& path = "config.json");
    void overrideFromCommandLine(int argc, char* argv[]);
    void printUsage(const char* programName);
    bool saveToFile(const std::string& path = "config.json") const;
};

const std::string SHADER_PATH = "shaders/";
const std::string MEDIA_PATH = "media/";

// Default display configuration
constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 800;

constexpr int MAX_FRAMES_IN_FLIGHT = 2;