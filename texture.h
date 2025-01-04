#pragma once

enum class ColorFormat {
    RGBA,
    UYVY,
    UYVA
};

struct Texture {
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
    ColorFormat format = ColorFormat::RGBA;
    bool isValid() const { return pixels != nullptr; }
};