#pragma once
#include <string>
#include <vector>
#include "texture.h"

class Loader {
public:
    std::vector<char> LoadShader(std::string shaderFilename);
    Texture LoadTexture(std::string textureFilename, ColorFormat format = ColorFormat::RGBA);

private:
    std::vector<char> readFile(const std::string& filename);
    std::string getShaderInPath(std::string shaderFilename);
    std::string getTextureInPath(std::string textureFilename);
};
