#pragma once
#include <vector>

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

    // Optional owned buffer for dynamically-generated frames (video, NDI)
    std::vector<unsigned char> ownedPixels;

    bool isValid() const { return pixels != nullptr; }

    // Set pixels from owned buffer with proper RAII
    void setOwnedPixels(std::vector<unsigned char>&& data, int w, int h, int ch, ColorFormat fmt) {
        ownedPixels = std::move(data);
        pixels = ownedPixels.data();
        width = w;
        height = h;
        channels = ch;
        format = fmt;
    }
};