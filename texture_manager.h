#pragma once
#include "texture.h"
#include <map>
#include <string>
#include <vector>
#include <memory>

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

    // Set current texture by name
    bool setCurrentTexture(const std::string& name);
    
    // Get list of available texture names
    std::vector<std::string> getAvailableTextures() const;
    
    // Unload a specific texture
    void unloadTexture(const std::string& name);
    
    // Unload all textures
    void unloadAll();

private:
    std::map<std::string, Texture> textures;
    std::vector<std::string> availableTextures;
    Texture* currentTexture;
    std::string currentTextureName;
    Texture* getTexture(const std::string& name);
};
