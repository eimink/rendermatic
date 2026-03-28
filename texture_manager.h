#pragma once
#include "texture.h"
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <chrono>

class TextureManager {
public:
    TextureManager();
    ~TextureManager();

    // Scan directory for texture files
    void scanTextureDirectory();

    // Load a specific texture by filename
    bool loadTexture(const std::string& filename);

    // Get current texture
    Texture* getCurrentTexture() const { return currentTexture; };

    // Get current texture name
    std::string getCurrentTextureName() const { return currentTextureName; }

    // Set current texture by name (auto-loads if not in memory)
    bool setCurrentTexture(const std::string& name);

    // Get list of available texture names
    std::vector<std::string> getAvailableTextures() const;

    // Unload a specific texture
    void unloadTexture(const std::string& name);

    // Unload all textures
    void unloadAll();

private:
    // Unload textures not used within the retention period (never the active one)
    void cleanupUnused();

    std::map<std::string, Texture> textures;
    std::map<std::string, std::chrono::steady_clock::time_point> lastUsed;
    std::vector<std::string> availableTextures;
    Texture* currentTexture = nullptr;
    std::string currentTextureName;

    static constexpr int TEXTURE_RETAIN_SECONDS = 30;
};
