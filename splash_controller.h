#pragma once
#include "texture.h"
#include "splash_screen.h"
#include <atomic>
#include <mutex>
#include <chrono>
#include <string>
#include <cstdint>

class SplashController {
public:
    SplashController(int screenWidth, int screenHeight, uint16_t wsPort);

    // Show the overlay for the given duration, gathering fresh system info
    void trigger(int durationSeconds = 10);

    // Dismiss the overlay early
    void dismiss();

    // Check if overlay is active (accounts for timer expiry)
    bool isActive() const;

    // Get the overlay texture (thread-safe copy)
    Texture getOverlayTexture() const;

private:
    static SplashScreen::Info gatherSystemInfo(uint16_t wsPort);

    int m_screenWidth;
    int m_screenHeight;
    uint16_t m_wsPort;
    mutable std::mutex m_mutex;
    Texture m_overlayTexture;
    mutable std::atomic<bool> m_active{false};
    std::chrono::steady_clock::time_point m_expiry;
};
