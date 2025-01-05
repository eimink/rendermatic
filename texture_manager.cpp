#include "texture_manager.h"
#include "loader.h"
#include <filesystem>
#include <algorithm>
#include "config.h"

TextureManager::TextureManager() {}

TextureManager::~TextureManager() {
    unloadAll();
}

void TextureManager::scanTextureDirectory() {
    availableTextures.clear();
    
    for (const auto& entry : std::filesystem::directory_iterator(TEXTURE_PATH)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
                availableTextures.push_back(entry.path().filename().string());
            }
        }
    }
}

bool TextureManager::loadTexture(const std::string& filename) {
    if (textures.find(filename) != textures.end()) {
        return true; // Already loaded
    }

    Loader loader;
    Texture texture = loader.LoadTexture(filename, ColorFormat::RGBA);
    
    if (texture.pixels != nullptr) {
        textures[filename] = std::move(texture);
        return true;
    }
    return false;
}

Texture* TextureManager::getTexture(const std::string& name) {
    auto it = textures.find(name);
    if (it != textures.end()) {
        return &it->second;
    }
    return nullptr;
}

bool TextureManager::setCurrentTexture(const std::string& name) {
    Texture* texture = getTexture(name);
    if (texture) {
        currentTexture = texture;
        currentTextureName = name;
        return true;
    }
    return false;
}

std::vector<std::string> TextureManager::getAvailableTextures() const {
    return availableTextures;
}

void TextureManager::unloadTexture(const std::string& name) {
    textures.erase(name);
}

void TextureManager::unloadAll() {
    textures.clear();
}
