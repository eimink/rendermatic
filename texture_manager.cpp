#include "texture_manager.h"
#include "loader.h"
#include <filesystem>
#include <algorithm>
#include <iostream>
#include "config.h"

TextureManager::TextureManager() {}

TextureManager::~TextureManager() {
    unloadAll();
}

void TextureManager::scanTextureDirectory() {
    std::lock_guard<std::mutex> lock(m_mutex);
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
    std::lock_guard<std::mutex> lock(m_mutex);
    if (textures.find(filename) != textures.end()) {
        lastUsed[filename] = std::chrono::steady_clock::now();
        return true;
    }

    try {
        Loader loader;
        Texture texture = loader.LoadTexture(filename, ColorFormat::RGBA);

        if (texture.pixels != nullptr) {
            textures[filename] = std::move(texture);
            lastUsed[filename] = std::chrono::steady_clock::now();
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to load texture: " << e.what() << std::endl;
    }
    return false;
}

bool TextureManager::setCurrentTexture(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Auto-load if not already in memory
    if (textures.find(name) == textures.end()) {
        // Release lock for I/O, then re-acquire
        m_mutex.unlock();
        Loader loader;
        Texture texture;
        bool loaded = false;
        try {
            texture = loader.LoadTexture(name, ColorFormat::RGBA);
            loaded = texture.pixels != nullptr;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load texture: " << e.what() << std::endl;
        }
        m_mutex.lock();

        if (!loaded) return false;
        textures[name] = std::move(texture);
        lastUsed[name] = std::chrono::steady_clock::now();
    }

    auto it = textures.find(name);
    if (it != textures.end()) {
        currentTexture = &it->second;
        currentTextureName = name;
        lastUsed[name] = std::chrono::steady_clock::now();
        cleanupUnused();
        return true;
    }
    return false;
}

Texture TextureManager::getCurrentTextureCopy() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (currentTexture) {
        return *currentTexture;  // Safe copy thanks to proper copy constructor
    }
    return Texture{};
}

std::string TextureManager::getCurrentTextureName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return currentTextureName;
}

std::vector<std::string> TextureManager::getAvailableTextures() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return availableTextures;
}

void TextureManager::unloadTexture(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (name == currentTextureName) {
        currentTexture = nullptr;
        currentTextureName.clear();
    }
    textures.erase(name);
    lastUsed.erase(name);
}

void TextureManager::unloadAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    textures.clear();
    lastUsed.clear();
    currentTexture = nullptr;
    currentTextureName.clear();
}

void TextureManager::cleanupUnused() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = std::chrono::seconds(TEXTURE_RETAIN_SECONDS);

    for (auto it = textures.begin(); it != textures.end(); ) {
        const std::string& name = it->first;

        // Never unload the active texture
        if (name == currentTextureName) {
            ++it;
            continue;
        }

        auto usedIt = lastUsed.find(name);
        if (usedIt != lastUsed.end() && (now - usedIt->second) > cutoff) {
            usedIt = lastUsed.erase(usedIt);
            it = textures.erase(it);
        } else {
            ++it;
        }
    }
}
