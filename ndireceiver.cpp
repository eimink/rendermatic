#include "ndireceiver.h"
#include <iostream>
#include <dlfcn.h>

// Search paths for NDI runtime library
static const char* NDI_LIB_PATHS[] = {
#ifdef __APPLE__
    "/Library/NDI SDK for Apple/lib/macOS/libndi.dylib",
    "/usr/local/lib/libndi.dylib",
    "libndi.dylib",
#else
    "/data/lib/libndi.so",          // Rendermatic data partition
    "/data/lib/libndi.so.6",
    "/usr/local/lib/libndi.so.6",   // Standard install
    "/usr/local/lib/libndi.so",
    "/usr/lib/libndi.so.6",
    "libndi.so.6",                  // System LD path
    "libndi.so",
#endif
    nullptr
};

NDIReceiver::NDIReceiver() {}

NDIReceiver::~NDIReceiver() {
    stop();

    if (m_ndiLib) {
        if (m_ndiRecv) {
            m_ndiLib->recv_destroy(static_cast<NDIlib_recv_instance_t>(m_ndiRecv));
        }
        if (m_ndiFind) {
            m_ndiLib->find_destroy(static_cast<NDIlib_find_instance_t>(m_ndiFind));
        }
        m_ndiLib->destroy();
    }

    if (m_libHandle) {
        dlclose(m_libHandle);
    }
}

bool NDIReceiver::loadRuntime() {
    if (m_ndiLib) return true;

    // Try each search path
    for (const char** path = NDI_LIB_PATHS; *path; ++path) {
        m_libHandle = dlopen(*path, RTLD_LOCAL | RTLD_LAZY);
        if (m_libHandle) {
            std::cout << "NDI: Loaded runtime from " << *path << std::endl;
            break;
        }
    }

    if (!m_libHandle) {
        std::cout << "NDI: Runtime not found (not installed)" << std::endl;
        return false;
    }

    // Resolve the NDI function table
    auto loadFunc = reinterpret_cast<const NDIlib_v6* (*)()>(
        dlsym(m_libHandle, "NDIlib_v6_load"));

    if (!loadFunc) {
        // Try v5 as fallback
        auto loadFuncV5 = reinterpret_cast<const NDIlib_v5* (*)()>(
            dlsym(m_libHandle, "NDIlib_v5_load"));
        if (loadFuncV5) {
            // v5 is a subset of v6, safe to cast
            m_ndiLib = reinterpret_cast<const NDIlib_v6*>(loadFuncV5());
        }
    } else {
        m_ndiLib = loadFunc();
    }

    if (!m_ndiLib) {
        std::cerr << "NDI: Failed to load function table" << std::endl;
        dlclose(m_libHandle);
        m_libHandle = nullptr;
        return false;
    }

    if (!m_ndiLib->initialize()) {
        std::cerr << "NDI: Failed to initialize" << std::endl;
        m_ndiLib = nullptr;
        dlclose(m_libHandle);
        m_libHandle = nullptr;
        return false;
    }

    std::cout << "NDI: Initialized (" << m_ndiLib->version() << ")" << std::endl;
    return true;
}

void NDIReceiver::start() {
    if (m_running) return;
    if (!m_ndiLib) {
        std::cerr << "NDI: Runtime not loaded, call loadRuntime() first" << std::endl;
        return;
    }
    m_running = true;
    m_thread = std::thread(&NDIReceiver::receiverLoop, this);
}

void NDIReceiver::stop() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_connected = false;
}

bool NDIReceiver::getLatestFrame(Texture& outTexture) {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (m_currentFrame.isValid()) {
        outTexture = m_currentFrame;
        return true;
    }
    return false;
}

std::vector<std::string> NDIReceiver::getAvailableSources() {
    std::vector<std::string> sources;
    if (!m_ndiLib) return sources;

    // Create finder if not exists
    if (!m_ndiFind) {
        NDIlib_find_create_t findDesc = {};
        findDesc.show_local_sources = true;
        m_ndiFind = m_ndiLib->find_create_v2(&findDesc);
    }

    if (!m_ndiFind) return sources;

    auto* pFind = static_cast<NDIlib_find_instance_t>(m_ndiFind);

    // Wait briefly for sources
    m_ndiLib->find_wait_for_sources(pFind, 2000);

    uint32_t numSources = 0;
    const NDIlib_source_t* pSources = m_ndiLib->find_get_current_sources(pFind, &numSources);

    for (uint32_t i = 0; i < numSources; i++) {
        sources.push_back(pSources[i].p_ndi_name);
    }
    return sources;
}

bool NDIReceiver::setSource(const std::string& sourceName) {
    {
        std::lock_guard<std::mutex> lock(m_sourceMutex);
        m_sourceName = sourceName;
    }

    if (m_running && m_ndiRecv && m_ndiLib) {
        m_ndiLib->recv_destroy(static_cast<NDIlib_recv_instance_t>(m_ndiRecv));
        m_ndiRecv = nullptr;
        m_connected = false;
    }
    return true;
}

std::string NDIReceiver::getCurrentSourceName() const {
    std::lock_guard<std::mutex> lock(m_sourceMutex);
    return m_sourceName;
}

bool NDIReceiver::isConnected() const {
    return m_connected;
}

void NDIReceiver::receiverLoop() {
    if (!m_ndiLib) return;

    while (m_running) {
        // Ensure we have a finder
        if (!m_ndiFind) {
            NDIlib_find_create_t findDesc = {};
            findDesc.show_local_sources = true;
            m_ndiFind = m_ndiLib->find_create_v2(&findDesc);
            if (!m_ndiFind) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
        }

        // Find and connect to target source
        if (!m_ndiRecv) {
            auto* pFind = static_cast<NDIlib_find_instance_t>(m_ndiFind);
            m_ndiLib->find_wait_for_sources(pFind, 1000);

            uint32_t numSources = 0;
            const NDIlib_source_t* pSources = m_ndiLib->find_get_current_sources(pFind, &numSources);

            const NDIlib_source_t* target = nullptr;
            std::string wantedName;
            {
                std::lock_guard<std::mutex> lock(m_sourceMutex);
                wantedName = m_sourceName;
            }

            for (uint32_t i = 0; i < numSources; i++) {
                if (wantedName.empty() || wantedName == pSources[i].p_ndi_name) {
                    target = &pSources[i];
                    break;
                }
            }

            if (!target) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            NDIlib_recv_create_v3_t recvDesc = {};
            recvDesc.source_to_connect_to = *target;
            recvDesc.color_format = NDIlib_recv_color_format_UYVY_BGRA;
            recvDesc.bandwidth = NDIlib_recv_bandwidth_highest;
            recvDesc.allow_video_fields = true;
            recvDesc.p_ndi_recv_name = "Rendermatic";

            m_ndiRecv = m_ndiLib->recv_create_v3(&recvDesc);
            if (!m_ndiRecv) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(m_sourceMutex);
                m_sourceName = target->p_ndi_name;
            }
            m_connected = true;
            std::cout << "NDI: Connected to " << target->p_ndi_name << std::endl;
        }

        // Capture frames
        auto* pRecv = static_cast<NDIlib_recv_instance_t>(m_ndiRecv);
        NDIlib_video_frame_v2_t videoFrame;

        auto frameType = m_ndiLib->recv_capture_v3(pRecv, &videoFrame, nullptr, nullptr, 16);

        if (frameType == NDIlib_frame_type_video) {
            int w = videoFrame.xres;
            int h = videoFrame.yres;
            ColorFormat fmt = (videoFrame.FourCC == NDIlib_FourCC_type_UYVY)
                ? ColorFormat::UYVY : ColorFormat::RGBA;

            size_t dataSize = static_cast<size_t>(videoFrame.line_stride_in_bytes) * h;
            std::vector<unsigned char> pixels(videoFrame.p_data, videoFrame.p_data + dataSize);

            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_currentFrame.setOwnedPixels(std::move(pixels), w, h, 4, fmt);
            }

            m_ndiLib->recv_free_video_v2(pRecv, &videoFrame);
        } else if (frameType == NDIlib_frame_type_none) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else if (frameType == NDIlib_frame_type_error) {
            std::cerr << "NDI: Connection lost" << std::endl;
            m_ndiLib->recv_destroy(static_cast<NDIlib_recv_instance_t>(m_ndiRecv));
            m_ndiRecv = nullptr;
            m_connected = false;
        }
    }
}
