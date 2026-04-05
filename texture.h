#pragma once
#include <vector>
#include <cstdint>

enum class ColorFormat {
    RGBA,
    UYVY,
    UYVA,
    NV12,
    YUV420P,     // Planar YUV: Y + U + V separate planes
    DMABUF_NV12  // VA-API DMA-BUF: pixels is unused, dmabuf fields carry the fd
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

    // DMA-BUF info for zero-copy VA-API frames
    int dmaFd = -1;           // DMA-BUF file descriptor
    uint32_t dmaOffset[2] = {};  // plane offsets (Y, UV)
    uint32_t dmaPitch[2] = {};   // plane pitches
    uint32_t dmaFourcc = 0;      // DRM fourcc format

    bool isValid() const { return pixels != nullptr || dmaFd >= 0; }

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