#pragma once
#include "texture.h"
#include <string>
#include <cstdint>

class SplashScreen {
public:
    struct Info {
        std::string instanceName;
        std::string ipAddress;
        uint16_t wsPort = 9002;
    };

    // Generate a lower-third overlay texture with device info
    // The texture is full-screen sized with alpha — only the lower-third bar
    // has content, the rest is transparent. Respects TV safe zones.
    static Texture generateOverlay(int screenWidth, int screenHeight, const Info& info);

private:
    static void renderText(std::vector<unsigned char>& rgba, int bufWidth, int bufHeight,
                          const std::string& text, float fontSize,
                          int x, int y,
                          unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255);

    static int measureText(const std::string& text, float fontSize);
};
