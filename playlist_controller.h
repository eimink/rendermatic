#pragma once
#ifdef HAVE_FFMPEG

#include <string>
#include <vector>
#include <atomic>
#include <mutex>

class VideoDecoder;

class PlaylistController {
public:
    PlaylistController(VideoDecoder* decoder);

    // Set playlist and persist to m3u file
    void setPlaylist(const std::vector<std::string>& sources, bool loop = true);

    // Load playlist from an m3u file
    bool loadFromFile(const std::string& path);

    // Save current playlist to an m3u file
    bool saveToFile(const std::string& path) const;

    // Playback control
    void start(int fromIndex = 0);
    void stop();
    void next();
    void prev();

    // State
    bool isActive() const;
    int getCurrentIndex() const;
    int getPlaylistSize() const;
    std::string getCurrentSource() const;
    std::vector<std::string> getPlaylist() const;
    bool isLooping() const;

private:
    void playIndex(int index);
    void onVideoEnd();

    VideoDecoder* m_decoder;
    std::vector<std::string> m_playlist;
    std::atomic<int> m_currentIndex{0};
    std::atomic<bool> m_active{false};
    bool m_loop = true;
    mutable std::mutex m_mutex;
};

#endif // HAVE_FFMPEG
