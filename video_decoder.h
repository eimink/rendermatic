#pragma once
#ifdef HAVE_FFMPEG

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>
#include <array>
#include <condition_variable>
#include <unistd.h>
#include "log.h"
#include "texture.h"

// Thread-safe ring buffer for decoded frames with PTS timestamps
class FrameQueue {
public:
    static constexpr int CAPACITY = 120; // ~2s at 60fps

    struct Entry {
        double pts = 0.0;
        Texture frame;
        bool valid = false;
    };

    // Push a frame. If blocking=true, waits when full (for file playback).
    // If blocking=false, drops oldest when full (for live streams).
    void push(double pts, Texture&& frame, bool blocking = false) {
        std::unique_lock<std::mutex> lock(m_mutex);

        if (blocking && m_count >= CAPACITY) {
            LOG_DEBUG("Queue BLOCKING (count=" << m_count << ")");
            m_notFull.wait(lock, [this] { return m_count < CAPACITY || m_stopped; });
            if (m_stopped) return;
        } else if (!blocking && m_count >= CAPACITY) {
            int oldestIdx = (m_writeIdx - m_count + CAPACITY) % CAPACITY;
            auto& old = m_buffer[oldestIdx];
            if (old.valid && old.frame.dmaFd >= 0)
                ::close(old.frame.dmaFd);
            old.valid = false;
            m_count--;
        }

        auto& slot = m_buffer[m_writeIdx];
        if (slot.valid && slot.frame.dmaFd >= 0)
            ::close(slot.frame.dmaFd);
        slot.pts = pts;
        slot.frame = std::move(frame);
        slot.valid = true;
        m_writeIdx = (m_writeIdx + 1) % CAPACITY;
        if (m_count < CAPACITY) m_count++;
        m_totalPushed++;
        if (m_totalPushed <= 10 || m_totalPushed % 1000 == 0)
            LOG_DEBUG("Queue push #" << m_totalPushed << " pts=" << pts << " count=" << m_count << " blocking=" << blocking);
    }

    void stop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
        m_notFull.notify_all();
    }

    // Get the frame with PTS closest to but not after `time`.
    // Drops older frames. Returns false if no frame available.
    bool getFrameForTime(double time, Texture& out) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_count == 0) return false;

        // Find the best frame: the one with PTS closest to but not after `time`
        int bestIdx = -1;
        double bestPts = -1e30;
        int startIdx = (m_writeIdx - m_count + CAPACITY) % CAPACITY;

        for (int i = 0; i < m_count; i++) {
            int idx = (startIdx + i) % CAPACITY;
            if (m_buffer[idx].valid && m_buffer[idx].pts <= time + 0.001 && m_buffer[idx].pts > bestPts) {
                bestIdx = idx;
                bestPts = m_buffer[idx].pts;
            }
        }

        if (bestIdx < 0) return false;  // all frames in the future

        out = m_buffer[bestIdx].frame;

        // Drop all frames up to and including the one we picked
        int dropped = 0;
        while (m_count > 0) {
            int oldestIdx = (m_writeIdx - m_count + CAPACITY) % CAPACITY;
            auto& old = m_buffer[oldestIdx];
            if (old.valid && old.frame.dmaFd >= 0)
                ::close(old.frame.dmaFd);
            old.valid = false;
            m_count--;
            dropped++;
            if (oldestIdx == bestIdx) break;
        }

        if (dropped > 0) m_notFull.notify_one();
        return true;
    }

    // Consume the next frame in order (for file playback with blocking queue)
    bool getNext(Texture& out, double* outPts = nullptr) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_count == 0) return false;

        int oldestIdx = (m_writeIdx - m_count + CAPACITY) % CAPACITY;
        if (outPts) *outPts = m_buffer[oldestIdx].pts;
        out = m_buffer[oldestIdx].frame;
        m_buffer[oldestIdx].valid = false;
        m_count--;
        m_notFull.notify_one();
        return true;
    }

    // Get the most recent frame (for live streams - always latest, drop rest)
    bool getLatest(Texture& out) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_count == 0) return false;

        int newestIdx = (m_writeIdx - 1 + CAPACITY) % CAPACITY;
        out = m_buffer[newestIdx].frame;

        int dropped = 0;
        while (m_count > 1) {
            int oldestIdx = (m_writeIdx - m_count + CAPACITY) % CAPACITY;
            auto& old = m_buffer[oldestIdx];
            if (old.valid && old.frame.dmaFd >= 0)
                ::close(old.frame.dmaFd);
            old.valid = false;
            m_count--;
            dropped++;
        }

        if (dropped > 0)
            m_notFull.notify_one();

        return true;
    }

    bool full() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_count >= CAPACITY;
    }

    // Block until there's space in the queue (for throttling the decoder)
    void waitForSpace(std::atomic<bool>& running) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_notFull.wait(lock, [this, &running] { return m_count < CAPACITY / 2 || m_stopped || !running; });
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_count == 0;
    }

    int size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_count;
    }

    // Get PTS of the newest frame (for media clock sync)
    double newestPts() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_count == 0) return 0.0;
        int idx = (m_writeIdx - 1 + CAPACITY) % CAPACITY;
        return m_buffer[idx].pts;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_notFull;
    std::array<Entry, CAPACITY> m_buffer;
    int m_writeIdx = 0;
    int m_count = 0;
    bool m_stopped = false;
    int m_totalPushed = 0;
};

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    bool open(const std::string& source);
    void close();
    void start();
    void stop();

    bool getNext(Texture& outTexture, double* outPts = nullptr) { return m_frameQueue.getNext(outTexture, outPts); }

    // Get frame for a specific media time (file playback)
    bool getFrameForTime(double mediaTime, Texture& outTexture);

    // Get most recent frame (live streams, NDI)
    bool getLatestFrame(Texture& outTexture);

    bool isActive() const;
    bool isStream() const { return m_isStream; }
    void setPlaybackTime(double t) { m_playbackTime.store(t); }

    void setLoop(bool loop) { m_loop = loop; }

    using OnEndCallback = std::function<void()>;
    void setOnEndCallback(OnEndCallback cb) { m_onEndCallback = std::move(cb); }

    struct SourceInfo {
        int width = 0;
        int height = 0;
        double fps = 0;
        double duration = -1;
        std::string codec;
        std::string source;
    };
    SourceInfo getSourceInfo() const;

    // Time base for PTS conversion (seconds per tick)
    double timeBase() const { return m_timeBase; }

private:
    void decoderLoop();

    struct FFmpegContext;
    std::unique_ptr<FFmpegContext> m_ff;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_active{false};

    FrameQueue m_frameQueue;
    double m_timeBase = 0.0;
    bool m_isStream = false;
    std::atomic<double> m_playbackTime{0.0}; // set by render loop

    std::string m_source;
    bool m_loop = true;
    OnEndCallback m_onEndCallback;
};

#endif // HAVE_FFMPEG
