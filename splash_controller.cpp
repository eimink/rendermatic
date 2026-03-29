#include "splash_controller.h"
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>

SplashController::SplashController(int screenWidth, int screenHeight, uint16_t wsPort)
    : m_screenWidth(screenWidth), m_screenHeight(screenHeight), m_wsPort(wsPort) {}

void SplashController::trigger(int durationSeconds) {
    auto info = gatherSystemInfo(m_wsPort);
    Texture overlay = SplashScreen::generateOverlay(m_screenWidth, m_screenHeight, info);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_overlayTexture = std::move(overlay);
        m_expiry = std::chrono::steady_clock::now() + std::chrono::seconds(durationSeconds);
    }
    m_active = true;
}

void SplashController::dismiss() {
    m_active = false;
}

bool SplashController::isActive() const {
    if (!m_active) return false;
    if (std::chrono::steady_clock::now() >= m_expiry) {
        m_active = false;
        return false;
    }
    return true;
}

Texture SplashController::getOverlayTexture() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_overlayTexture;
}

SplashScreen::Info SplashController::gatherSystemInfo(uint16_t wsPort) {
    SplashScreen::Info info;
    info.wsPort = wsPort;

    // Hostname
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        info.instanceName = hostname;
    } else {
        info.instanceName = "rendermatic";
    }

    // IP address — first non-loopback IPv4
    struct ifaddrs* iflist = nullptr;
    if (getifaddrs(&iflist) == 0) {
        for (auto* ifa = iflist; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) continue;
            if (ifa->ifa_addr->sa_family != AF_INET) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;

            char buf[INET_ADDRSTRLEN];
            auto* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
            if (inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf))) {
                info.ipAddress = buf;
                break;
            }
        }
        freeifaddrs(iflist);
    }

    if (info.ipAddress.empty()) {
        info.ipAddress = "no network";
    }

    return info;
}
