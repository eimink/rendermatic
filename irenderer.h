#pragma once
#include "texture.h"

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool init(int width, int height, const char* title, bool fullscreen, int monitorIndex) = 0;
    virtual void processInput() = 0;
    virtual void render(const Texture& texture) = 0;
    virtual bool shouldClose() const = 0;
};
