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

    Texture() = default;

    Texture(const Texture& other)
        : width(other.width), height(other.height), channels(other.channels),
          format(other.format), ownedPixels(other.ownedPixels) {
        pixels = ownedPixels.empty() ? nullptr : ownedPixels.data();
    }

    Texture(Texture&& other) noexcept
        : width(other.width), height(other.height), channels(other.channels),
          format(other.format), ownedPixels(std::move(other.ownedPixels)) {
        pixels = ownedPixels.empty() ? nullptr : ownedPixels.data();
        other.pixels = nullptr;
        other.width = 0;
        other.height = 0;
    }

    Texture& operator=(const Texture& other) {
        if (this != &other) {
            width = other.width;
            height = other.height;
            channels = other.channels;
            format = other.format;
            ownedPixels = other.ownedPixels;
            pixels = ownedPixels.empty() ? nullptr : ownedPixels.data();
        }
        return *this;
    }

    Texture& operator=(Texture&& other) noexcept {
        if (this != &other) {
            width = other.width;
            height = other.height;
            channels = other.channels;
            format = other.format;
            ownedPixels = std::move(other.ownedPixels);
            pixels = ownedPixels.empty() ? nullptr : ownedPixels.data();
            other.pixels = nullptr;
            other.width = 0;
            other.height = 0;
        }
        return *this;
    }

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