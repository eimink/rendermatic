#pragma once
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include "texture.h"
#include "ndi/Processing.NDI.Lib.h"
#include "ndi/Processing.NDI.DynamicLoad.h"

class NDIReceiver {
public:
    NDIReceiver();
    ~NDIReceiver();

    // Try to load libndi.so from known paths. Returns true if NDI is available.
    bool loadRuntime();
    bool isRuntimeLoaded() const { return m_ndiLib != nullptr; }

    void start();
    void stop();          // Blocking — waits for thread to finish
    void requestStop();   // Non-blocking — signals stop, thread cleans up async
    bool getLatestFrame(Texture& outTexture);

    // Source discovery and selection
    std::vector<std::string> getAvailableSources();
    bool setSource(const std::string& sourceName);
    std::string getCurrentSourceName() const;
    bool isConnected() const;

private:
    void receiverLoop();

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_sourceChanged{false};
    Texture m_currentFrame;
    mutable std::mutex m_frameMutex;
    std::string m_sourceName;
    mutable std::mutex m_sourceMutex;

    // Dynamic loading
    void* m_libHandle = nullptr;
    const NDIlib_v6* m_ndiLib = nullptr;

    // NDI instances — owned exclusively by the receiver thread
    void* m_ndiFind = nullptr;
    void* m_ndiRecv = nullptr;
};
