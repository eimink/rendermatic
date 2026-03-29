#include "playlist_controller.h"

#ifdef HAVE_FFMPEG

#include "video_decoder.h"
#include "config.h"
#include <fstream>
#include <iostream>

PlaylistController::PlaylistController(VideoDecoder* decoder)
    : m_decoder(decoder) {}

void PlaylistController::setPlaylist(const std::vector<std::string>& sources, bool loop) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playlist = sources;
    m_loop = loop;
    m_currentIndex = 0;
}

bool PlaylistController::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::vector<std::string> sources;
    bool loop = true;
    std::string line;

    while (std::getline(file, line)) {
        // Trim whitespace
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();

        if (line.empty()) continue;

        // #EXTM3U header — skip
        // #EXTINF lines — skip (we don't need metadata)
        // #RENDERMATIC:LOOP=false — custom directive
        if (line[0] == '#') {
            if (line.rfind("#RENDERMATIC:LOOP=", 0) == 0) {
                loop = (line.substr(18) != "false");
            }
            continue;
        }

        sources.push_back(line);
    }

    if (sources.empty()) return false;

    setPlaylist(sources, loop);
    std::cout << "Loaded playlist: " << sources.size() << " videos (loop=" << loop << ")" << std::endl;
    return true;
}

bool PlaylistController::saveToFile(const std::string& path) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "#EXTM3U\n";
    file << "#RENDERMATIC:LOOP=" << (m_loop ? "true" : "false") << "\n";
    for (const auto& source : m_playlist) {
        file << source << "\n";
    }
    return file.good();
}

void PlaylistController::start(int fromIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_playlist.empty()) return;

    m_active = true;
    m_decoder->setOnEndCallback([this]() { onVideoEnd(); });
    playIndex(fromIndex);
}

void PlaylistController::stop() {
    m_active = false;
    m_decoder->setOnEndCallback(nullptr);
    m_decoder->stop();
}

void PlaylistController::next() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active || m_playlist.empty()) return;

    int nextIdx = m_currentIndex + 1;
    if (nextIdx >= static_cast<int>(m_playlist.size())) {
        if (m_loop) {
            nextIdx = 0;
        } else {
            return;
        }
    }
    playIndex(nextIdx);
}

void PlaylistController::prev() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active || m_playlist.empty()) return;

    int prevIdx = m_currentIndex - 1;
    if (prevIdx < 0) {
        if (m_loop) {
            prevIdx = static_cast<int>(m_playlist.size()) - 1;
        } else {
            prevIdx = 0;
        }
    }
    playIndex(prevIdx);
}

bool PlaylistController::isActive() const {
    return m_active;
}

int PlaylistController::getCurrentIndex() const {
    return m_currentIndex;
}

int PlaylistController::getPlaylistSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_playlist.size());
}

std::string PlaylistController::getCurrentSource() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    int idx = m_currentIndex;
    if (idx >= 0 && idx < static_cast<int>(m_playlist.size())) {
        return m_playlist[idx];
    }
    return "";
}

std::vector<std::string> PlaylistController::getPlaylist() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_playlist;
}

bool PlaylistController::isLooping() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_loop;
}

void PlaylistController::playIndex(int index) {
    // Must be called with m_mutex held
    if (index < 0 || index >= static_cast<int>(m_playlist.size())) return;

    m_currentIndex = index;
    const std::string& source = m_playlist[index];

    // Resolve local filenames to media path
    std::string fullPath = source;
    if (source.find("://") == std::string::npos) {
        fullPath = MEDIA_PATH + source;
    }

    m_decoder->stop();
    m_decoder->setLoop(false);  // Playlist controls looping, not decoder
    if (m_decoder->open(fullPath)) {
        m_decoder->start();
    } else {
        std::cerr << "Playlist: failed to open " << source << ", skipping" << std::endl;
        // Try next video
        int nextIdx = index + 1;
        if (nextIdx < static_cast<int>(m_playlist.size())) {
            playIndex(nextIdx);
        } else if (m_loop) {
            playIndex(0);
        } else {
            m_active = false;
        }
    }
}

void PlaylistController::onVideoEnd() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active) return;

    int nextIdx = m_currentIndex + 1;
    if (nextIdx >= static_cast<int>(m_playlist.size())) {
        if (m_loop) {
            playIndex(0);
        } else {
            // Playlist finished — freeze on last frame
            m_active = false;
        }
    } else {
        playIndex(nextIdx);
    }
}

#endif // HAVE_FFMPEG
