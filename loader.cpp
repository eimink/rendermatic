#include "loader.h"
#include "config.h"
#include <vector>
#include <fstream>
#include <cstdlib>
#include <format>
#include <iostream>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

std::vector<char> Loader::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    std::cout << "Opening file: " << filename << std::endl;

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

std::string Loader::getShaderInPath(std::string shaderFilename)
{
    // Try different common shader locations
    const std::vector<std::string> paths = {
        SHADER_PATH + shaderFilename,
        "../" + SHADER_PATH + shaderFilename,
        "../../" + SHADER_PATH + shaderFilename,
        "./shaders/" + shaderFilename
    };

    for (const auto& path : paths) {
        std::ifstream file(path);
        if (file.good()) {
            std::cout << "Found shader at: " << path << std::endl;
            return path;
        }
    }
    
    throw std::runtime_error("Could not find shader file: " + shaderFilename);
}

std::string Loader::getTextureInPath(std::string textureFilename)
{
    return std::format("{}{}", TEXTURE_PATH, textureFilename);
}

std::vector<char> Loader::LoadShader(std::string shaderFilename)
{
    try {
        std::vector<char> buffer = readFile(getShaderInPath(shaderFilename));
        buffer.push_back('\0'); // Ensure null termination for shader source
        return buffer;
    } catch (const std::exception& e) {
        std::cerr << "Error loading shader: " << e.what() << std::endl;
        throw;
    }
}

Texture Loader::LoadTexture(std::string textureFilename, ColorFormat format)
{
    Texture texture;
    texture.format = format;
    
    // For UYVY, we need to load raw data
    if (format == ColorFormat::UYVY) {
        std::vector<char> buffer = readFile(getTextureInPath(textureFilename));
        texture.width = 512;
        texture.height = 512;
        texture.channels = 4; // UYVY is 4 channels
        texture.pixels = new unsigned char[buffer.size()];
        std::memcpy(texture.pixels, buffer.data(), buffer.size());
    } else {
        texture.pixels = stbi_load(getTextureInPath(textureFilename).c_str(), 
                                 &texture.width, &texture.height, 
                                 &texture.channels, STBI_rgb_alpha);
    }

    if (!texture.pixels) {
        throw std::runtime_error("failed to load texture image!");
    }
    return texture;
}