#pragma once
#ifdef HAVE_FFMPEG

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include "texture.h"

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    // Open a video file or stream URL (rtmp://, rtsp://, srt://, or file path)
    bool open(const std::string& source);

    // Close current source
    void close();

    // Start decoding in background thread
    void start();

    // Stop decoding
    void stop();

    // Get the latest decoded frame as a Texture
    // Returns true if a new frame is available since last call
    bool getLatestFrame(Texture& outTexture);

    // Check if source is still active (not EOF for non-looping, not disconnected)
    bool isActive() const;

    // Set whether to loop video files (ignored for streams)
    void setLoop(bool loop) { m_loop = loop; }

    struct SourceInfo {
        int width = 0;
        int height = 0;
        double fps = 0;
        double duration = -1;  // -1 for live streams
        std::string codec;
        std::string source;
    };
    SourceInfo getSourceInfo() const;

private:
    void decoderLoop();

    struct FFmpegContext;
    std::unique_ptr<FFmpegContext> m_ff;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_active{false};

    Texture m_currentFrame;
    std::mutex m_frameMutex;
    bool m_newFrameAvailable = false;

    std::string m_source;
    bool m_loop = true;
};

#endif // HAVE_FFMPEG
