#pragma once
#include <thread>
#include <atomic>
#include "texture.h"

class NDIReceiver {
public:
    NDIReceiver();
    ~NDIReceiver();
    
    void start();
    void stop();
    bool getLatestFrame(Texture& outTexture);

private:
    void receiverLoop();
    
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    Texture m_currentFrame;
    std::mutex m_frameMutex;
};
