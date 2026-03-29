#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "splash_screen.h"
#include "embedded_font.h"
#include <cstring>
#include <algorithm>

static stbtt_fontinfo g_font;
static bool g_fontInitialized = false;

static bool ensureFont() {
    if (!g_fontInitialized) {
        if (stbtt_InitFont(&g_font, LIBERATION_MONO_TTF,
                           stbtt_GetFontOffsetForIndex(LIBERATION_MONO_TTF, 0))) {
            g_fontInitialized = true;
        }
    }
    return g_fontInitialized;
}

int SplashScreen::measureText(const std::string& text, float fontSize) {
    if (!ensureFont()) return 0;

    float scale = stbtt_ScaleForPixelHeight(&g_font, fontSize);
    int totalWidth = 0;

    for (size_t i = 0; i < text.size(); i++) {
        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(&g_font, text[i], &advanceWidth, &leftSideBearing);
        totalWidth += static_cast<int>(advanceWidth * scale);

        if (i + 1 < text.size()) {
            int kern = stbtt_GetCodepointKernAdvance(&g_font, text[i], text[i + 1]);
            totalWidth += static_cast<int>(kern * scale);
        }
    }
    return totalWidth;
}

void SplashScreen::renderText(std::vector<unsigned char>& rgba, int bufWidth, int bufHeight,
                              const std::string& text, float fontSize,
                              int x, int y,
                              unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (!ensureFont()) return;

    float scale = stbtt_ScaleForPixelHeight(&g_font, fontSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &lineGap);
    int baseline = y + static_cast<int>(ascent * scale);

    float xPos = static_cast<float>(x);

    for (size_t i = 0; i < text.size(); i++) {
        int cx1, cy1, cx2, cy2;
        stbtt_GetCodepointBitmapBox(&g_font, text[i], scale, scale, &cx1, &cy1, &cx2, &cy2);

        int glyphW = cx2 - cx1;
        int glyphH = cy2 - cy1;
        int glyphX = static_cast<int>(xPos) + cx1;
        int glyphY = baseline + cy1;

        if (glyphW > 0 && glyphH > 0) {
            // Render glyph to temporary bitmap
            std::vector<unsigned char> glyphBitmap(glyphW * glyphH);
            stbtt_MakeCodepointBitmap(&g_font, glyphBitmap.data(), glyphW, glyphH,
                                      glyphW, scale, scale, text[i]);

            // Composite onto RGBA buffer
            for (int gy = 0; gy < glyphH; gy++) {
                int destY = glyphY + gy;
                if (destY < 0 || destY >= bufHeight) continue;

                for (int gx = 0; gx < glyphW; gx++) {
                    int destX = glyphX + gx;
                    if (destX < 0 || destX >= bufWidth) continue;

                    unsigned char glyphAlpha = glyphBitmap[gy * glyphW + gx];
                    if (glyphAlpha == 0) continue;

                    // Blend: src alpha = glyphAlpha * (a/255)
                    int srcAlpha = (glyphAlpha * a) / 255;
                    int idx = (destY * bufWidth + destX) * 4;

                    // Alpha blending over existing pixel
                    int dstAlpha = rgba[idx + 3];
                    int outAlpha = srcAlpha + dstAlpha * (255 - srcAlpha) / 255;

                    if (outAlpha > 0) {
                        rgba[idx + 0] = static_cast<unsigned char>(
                            (r * srcAlpha + rgba[idx + 0] * dstAlpha * (255 - srcAlpha) / 255) / outAlpha);
                        rgba[idx + 1] = static_cast<unsigned char>(
                            (g * srcAlpha + rgba[idx + 1] * dstAlpha * (255 - srcAlpha) / 255) / outAlpha);
                        rgba[idx + 2] = static_cast<unsigned char>(
                            (b * srcAlpha + rgba[idx + 2] * dstAlpha * (255 - srcAlpha) / 255) / outAlpha);
                        rgba[idx + 3] = static_cast<unsigned char>(std::min(outAlpha, 255));
                    }
                }
            }
        }

        // Advance cursor
        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(&g_font, text[i], &advanceWidth, &leftSideBearing);
        xPos += advanceWidth * scale;

        if (i + 1 < text.size()) {
            int kern = stbtt_GetCodepointKernAdvance(&g_font, text[i], text[i + 1]);
            xPos += kern * scale;
        }
    }
}

Texture SplashScreen::generateOverlay(int screenWidth, int screenHeight, const Info& info) {
    // TV safe zones for 1080p: title safe = 90% (5% inset each side)
    int safeInsetX = screenWidth * 5 / 100;
    int safeInsetY = screenHeight * 5 / 100;

    // Font sizes relative to screen height
    float nameSize = screenHeight * 0.028f;    // ~30px at 1080p
    float detailSize = screenHeight * 0.022f;  // ~24px at 1080p
    float lineSpacing = 1.4f;

    // Build text lines
    std::string nameLine = info.instanceName;
    std::string detailLine = info.ipAddress + ":" + std::to_string(info.wsPort);

    // Calculate bar dimensions
    int barHeight = static_cast<int>((nameSize + detailSize) * lineSpacing + detailSize);
    int barTop = screenHeight - safeInsetY - barHeight;
    int barPadding = static_cast<int>(detailSize * 0.6f);

    // Full-screen RGBA buffer, fully transparent
    size_t bufSize = static_cast<size_t>(screenWidth) * screenHeight * 4;
    std::vector<unsigned char> rgba(bufSize, 0);

    // Draw semi-transparent dark bar background
    for (int y = barTop; y < screenHeight - safeInsetY; y++) {
        for (int x = safeInsetX; x < screenWidth - safeInsetX; x++) {
            int idx = (y * screenWidth + x) * 4;
            rgba[idx + 0] = 20;   // R
            rgba[idx + 1] = 20;   // G
            rgba[idx + 2] = 30;   // B
            rgba[idx + 3] = 200;  // A (semi-transparent)
        }
    }

    // Render instance name
    int textX = safeInsetX + barPadding;
    int textY = barTop + barPadding;
    renderText(rgba, screenWidth, screenHeight, nameLine, nameSize,
               textX, textY, 255, 255, 255);

    // Render IP:port below
    textY += static_cast<int>(nameSize * lineSpacing);
    renderText(rgba, screenWidth, screenHeight, detailLine, detailSize,
               textX, textY, 180, 180, 200);

    Texture overlay;
    overlay.setOwnedPixels(std::move(rgba), screenWidth, screenHeight, 4, ColorFormat::RGBA);
    return overlay;
}
