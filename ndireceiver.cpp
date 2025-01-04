#include "ndireceiver.h"

NDIReceiver::NDIReceiver() {}

NDIReceiver::~NDIReceiver() {
    stop();
}

void NDIReceiver::start() {
    m_running = true;
    m_thread = std::thread(&NDIReceiver::receiverLoop, this);
}

void NDIReceiver::stop() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool NDIReceiver::getLatestFrame(Texture& outTexture) {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (m_currentFrame.isValid()) {
        outTexture = m_currentFrame;
        return true;
    }
    return false;
}

void NDIReceiver::receiverLoop() {
    while (m_running) {
        // TODO: Implement NDI frame receiving
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
