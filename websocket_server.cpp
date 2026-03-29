#include "websocket_server.h"
#include "splash_controller.h"
#include "ndireceiver.h"
#include "mdns_advertiser.h"
#include "config.h"
#ifdef HAVE_FFMPEG
#include "video_decoder.h"
#include "playlist_controller.h"
#endif
#include <unistd.h>
#include <algorithm>
#include <filesystem>

namespace {
    // Reject path traversal attempts in filenames
    bool isSafeFilename(const std::string& name) {
        if (name.empty()) return false;
        if (name.find("..") != std::string::npos) return false;
        if (name.find('/') != std::string::npos) return false;
        if (name.find('\\') != std::string::npos) return false;
        if (name[0] == '.') return false;  // No hidden files
        return true;
    }

    // Whitelist allowed URL schemes for video sources
    bool isSafeVideoSource(const std::string& source) {
        if (source.empty()) return false;
        // Allow known streaming protocols
        if (source.rfind("rtmp://", 0) == 0) return true;
        if (source.rfind("rtsp://", 0) == 0) return true;
        if (source.rfind("srt://", 0) == 0) return true;
        if (source.rfind("http://", 0) == 0) return true;
        if (source.rfind("https://", 0) == 0) return true;
        // For local paths, require safe filename (no traversal)
        return isSafeFilename(source);
    }
}

WebSocketServer::WebSocketServer(TextureManager& tm, uint16_t port)
    : textureManager(tm), port(port) {
    server.clear_access_channels(websocketpp::log::alevel::all);
    server.set_max_message_size(64 * 1024);  // 64KB max message size
    server.init_asio();
    server.set_open_handler(bind(&WebSocketServer::onOpen, this, std::placeholders::_1));
    server.set_close_handler(bind(&WebSocketServer::onClose, this, std::placeholders::_1));
    server.set_message_handler(bind(&WebSocketServer::onMessage, this,
        std::placeholders::_1, std::placeholders::_2));
}

WebSocketServer::~WebSocketServer() {
    stop();
}

void WebSocketServer::start() {
    if (!running) {
        running = true;
        server.listen(port);
        serverThread = std::thread(&WebSocketServer::run, this);
    }
}

void WebSocketServer::stop() {
    if (running) {
        running = false;
        server.stop();
        if (serverThread.joinable()) {
            serverThread.join();
        }
    }
}

void WebSocketServer::run() {
    while (running) {
        try {
            server.start_accept();
            server.run();
        } catch (const std::exception& e) {
            // Handle or log error
        }
    }
}

void WebSocketServer::setConfiguration(Configuration* config) {
    m_config = config;
    if (m_config) {
        m_auth.setStoredHash(m_config->authKeyHash);
    }
}

bool WebSocketServer::setInstanceName(const std::string& newName) {
    if (!m_config) {
        return false;
    }

    m_config->instanceName = newName;

    if (!m_config->saveToFile()) {
        return false;
    }

    if (m_advertiser) {
        m_advertiser->updateInstanceName(newName);
    }

    return true;
}

void WebSocketServer::onOpen(websocketpp::connection_hdl hdl) {
    auto con = server.get_con_from_hdl(hdl);
    std::string remoteIp = con->get_remote_endpoint();

    m_auth.onConnectionOpened(hdl, remoteIp);

    Json::Value notice;
    notice["command"] = "auth_status";
    if (m_auth.isOpenMode()) {
        notice["authRequired"] = false;
        notice["authenticated"] = true;
    } else {
        notice["authRequired"] = true;
        notice["authenticated"] = false;
    }
    sendJson(hdl, notice);
}

void WebSocketServer::onClose(websocketpp::connection_hdl hdl) {
    m_auth.onConnectionClosed(hdl);
}

std::vector<std::string> WebSocketServer::scanVideos() {
    m_availableVideos.clear();
    if (!std::filesystem::exists(MEDIA_PATH)) return m_availableVideos;

    for (const auto& entry : std::filesystem::directory_iterator(MEDIA_PATH)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".mp4" || ext == ".mkv" || ext == ".mov" ||
                ext == ".avi" || ext == ".webm" || ext == ".flv") {
                m_availableVideos.push_back(entry.path().filename().string());
            }
        }
    }
    return m_availableVideos;
}

void WebSocketServer::sendJson(websocketpp::connection_hdl hdl, const Json::Value& response) {
    Json::FastWriter writer;
    server.send(hdl, writer.write(response), websocketpp::frame::opcode::text);
}

void WebSocketServer::onMessage(websocketpp::connection_hdl hdl, wsserver::message_ptr msg) {
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(msg->get_payload(), root)) {
        return;
    }

    std::string command = root["command"].asString();
    Json::Value response;

    // --- Authentication gate ---
    if (!m_auth.isOpenMode()) {
        // Always allow authenticate command
        if (command == "authenticate") {
            response["command"] = "auth_response";

            if (m_auth.isRateLimited(hdl)) {
                response["success"] = false;
                response["message"] = "Too many failed attempts. Try again later.";
                response["retryAfterSeconds"] = m_auth.getRemainingLockoutSeconds(hdl);
                sendJson(hdl, response);
                return;
            }

            std::string key = root.get("key", "").asString();
            if (m_auth.tryAuthenticate(hdl, key)) {
                response["success"] = true;
                response["message"] = "Authenticated successfully";
            } else {
                response["success"] = false;
                response["message"] = "Invalid authentication key";
            }
            sendJson(hdl, response);
            return;
        }

        // Allow limited get_device_info for discovery
        if (command == "get_device_info" && !m_auth.isAuthenticated(hdl)) {
            response["command"] = "device_info";
            if (m_config) {
                response["instanceName"] = m_config->instanceName;
            }
            response["authRequired"] = true;
            response["authenticated"] = false;
            response["success"] = true;
            sendJson(hdl, response);
            return;
        }

        // Allow identify without auth (physical operation for device location)
        if (command == "identify" && !m_auth.isAuthenticated(hdl)) {
            response["command"] = "identify_response";
            if (m_splashController) {
                int duration = root.get("duration", 10).asInt();
                duration = std::clamp(duration, 1, 60);
                m_splashController->trigger(duration);
                response["success"] = true;
                response["duration"] = duration;
            } else {
                response["success"] = false;
                response["message"] = "Splash controller not available";
            }
            sendJson(hdl, response);
            return;
        }

        // Block all other commands for unauthenticated connections
        if (!m_auth.isAuthenticated(hdl)) {
            response["command"] = "auth_required";
            response["message"] = "Authentication required";
            response["success"] = false;
            sendJson(hdl, response);
            return;
        }
    }

    // --- Commands (requires authentication when auth is enabled) ---

    if (command == "scan_textures") {
        textureManager.scanTextureDirectory();
        response["command"] = "scan_textures_response";
        response["textures"] = Json::arrayValue;
        for (const auto& texture : textureManager.getAvailableTextures()) {
            response["textures"].append(texture);
        }
        response["success"] = true;
    }
    else if (command == "list_textures") {
        response["command"] = "texture_list";
        response["textures"] = Json::arrayValue;
        for (const auto& texture : textureManager.getAvailableTextures()) {
            response["textures"].append(texture);
        }
        response["success"] = true;
    }
    else if (command == "scan_videos") {
        auto videos = scanVideos();
        response["command"] = "scan_videos_response";
        response["videos"] = Json::arrayValue;
        for (const auto& video : videos) {
            response["videos"].append(video);
        }
        response["success"] = true;
    }
    else if (command == "list_videos") {
        response["command"] = "video_list";
        response["videos"] = Json::arrayValue;
        for (const auto& video : m_availableVideos) {
            response["videos"].append(video);
        }
        response["success"] = true;
    }
    else if (command == "load_texture") {
        std::string textureName = root["texture"].asString();
        response["command"] = "load_texture_response";
        if (!isSafeFilename(textureName)) {
            response["success"] = false;
            response["message"] = "Invalid texture filename";
        } else {
            response["success"] = textureManager.loadTexture(textureName);
        }
    }
    else if (command == "set_texture") {
        std::string textureName = root["texture"].asString();
        response["command"] = "set_texture_response";
        if (!isSafeFilename(textureName)) {
            response["success"] = false;
            response["message"] = "Invalid texture filename";
        } else {
            response["success"] = textureManager.setCurrentTexture(textureName);
        }
    }
    else if (command == "get_device_info") {
        response["command"] = "device_info";

        if (m_config) {
            response["instanceName"] = m_config->instanceName;
        }

        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            response["hostname"] = std::string(hostname);
        }

        response["wsPort"] = port;
        response["currentTexture"] = textureManager.getCurrentTextureName();
        response["authEnabled"] = m_auth.isAuthEnabled();
        response["success"] = true;
    }
    else if (command == "set_device_name") {
        response["command"] = "device_name_response";

        if (!root.isMember("name") || root["name"].isNull()) {
            response["success"] = false;
            response["message"] = "Missing 'name' field";
        } else {
            std::string newName = root["name"].asString();
            if (setInstanceName(newName)) {
                response["success"] = true;
                response["instanceName"] = newName;
            } else {
                response["success"] = false;
                response["message"] = "Failed to update device name";
            }
        }
    }
    else if (command == "set_rotation") {
        response["command"] = "set_rotation_response";
        int angle = root.get("angle", 0).asInt();
        if (angle != 0 && angle != 90 && angle != 180 && angle != 270) {
            response["success"] = false;
            response["message"] = "Invalid angle. Must be 0, 90, 180, or 270.";
        } else {
            if (m_renderer) {
                m_renderer->setRotation(angle);
            }
            if (m_config) {
                m_config->displayRotation = angle;
                m_config->saveToFile();
            }
            response["success"] = true;
            response["angle"] = angle;
        }
    }
    // --- Auth management commands ---
    else if (command == "set_auth_key") {
        response["command"] = "set_auth_key_response";
        std::string newKey = root.get("key", "").asString();
        if (newKey.empty()) {
            response["success"] = false;
            response["message"] = "Key cannot be empty. Use 'clear_auth_key' to disable auth.";
        } else if (newKey.length() < 8) {
            response["success"] = false;
            response["message"] = "Key must be at least 8 characters";
        } else {
            bool wasOpenMode = m_auth.isOpenMode();
            m_auth.setKey(newKey);
            if (m_config) {
                m_config->authKeyHash = m_auth.getStoredHash();
                m_config->saveToFile();
            }
            // If enabling auth for the first time, mark this connection as authenticated
            if (wasOpenMode) {
                m_auth.markAuthenticated(hdl);
            }
            response["success"] = true;
            response["message"] = "Authentication key updated";
        }
    }
    else if (command == "clear_auth_key") {
        response["command"] = "clear_auth_key_response";
        m_auth.clearKey();
        if (m_config) {
            m_config->authKeyHash = "";
            m_config->saveToFile();
        }
        response["success"] = true;
        response["message"] = "Authentication disabled. All connections are now open.";
    }
    else if (command == "identify") {
        response["command"] = "identify_response";
        if (m_splashController) {
            int duration = root.get("duration", 10).asInt();
            duration = std::clamp(duration, 1, 60);
            m_splashController->trigger(duration);
            response["success"] = true;
            response["duration"] = duration;
        } else {
            response["success"] = false;
            response["message"] = "Splash controller not available";
        }
    }
    else if (command == "get_auth_status") {
        response["command"] = "auth_status";
        response["authEnabled"] = m_auth.isAuthEnabled();
        response["authenticated"] = m_auth.isAuthenticated(hdl) || m_auth.isOpenMode();
        response["success"] = true;
    }
#ifdef HAVE_FFMPEG
    else if (command == "play_video") {
        response["command"] = "play_video_response";
        if (!m_videoDecoder) {
            response["success"] = false;
            response["message"] = "Video decoder not available";
        } else if (!root.isMember("source") || root["source"].isNull()) {
            response["success"] = false;
            response["message"] = "Missing 'source' field";
        } else {
            std::string source = root["source"].asString();
            if (!isSafeVideoSource(source)) {
                response["success"] = false;
                response["message"] = "Invalid video source. Use a streaming URL (rtmp/rtsp/srt/http) or a local filename.";
                sendJson(hdl, response);
                return;
            }
            // Prefix local filenames with media path
            std::string fullSource = source;
            if (source.find("://") == std::string::npos) {
                fullSource = MEDIA_PATH + source;
            }
            bool loop = root.get("loop", true).asBool();
            m_videoDecoder->stop();
            m_videoDecoder->setLoop(loop);
            if (m_videoDecoder->open(fullSource)) {
                m_videoDecoder->start();
                if (m_config) {
                    m_config->videoSource = source;
                    m_config->videoMode = true;
                    m_config->videoLoop = loop;
                }
                response["success"] = true;
            } else {
                response["success"] = false;
                response["message"] = "Failed to open video source";
            }
        }
    }
    else if (command == "stop_video") {
        response["command"] = "stop_video_response";
        if (m_videoDecoder) {
            m_videoDecoder->stop();
            m_videoDecoder->close();
            if (m_config) {
                m_config->videoMode = false;
            }
            response["success"] = true;
        } else {
            response["success"] = false;
            response["message"] = "Video decoder not available";
        }
    }
    else if (command == "get_video_status") {
        response["command"] = "video_status";
        if (m_videoDecoder) {
            response["active"] = m_videoDecoder->isActive();
            auto info = m_videoDecoder->getSourceInfo();
            response["source"] = info.source;
            response["width"] = info.width;
            response["height"] = info.height;
            response["fps"] = info.fps;
            response["duration"] = info.duration;
            response["codec"] = info.codec;
            response["success"] = true;
        } else {
            response["active"] = false;
            response["success"] = true;
        }
    }
    else if (command == "set_playlist") {
        response["command"] = "set_playlist_response";
        if (!m_playlistController) {
            response["success"] = false;
            response["message"] = "Playlist controller not available";
        } else if (!root.isMember("videos") || !root["videos"].isArray()) {
            response["success"] = false;
            response["message"] = "Missing 'videos' array";
        } else {
            std::vector<std::string> videos;
            for (const auto& v : root["videos"]) {
                std::string name = v.asString();
                if (!name.empty()) videos.push_back(name);
            }
            bool loop = root.get("loop", true).asBool();
            m_playlistController->setPlaylist(videos, loop);
            m_playlistController->saveToFile("playlist.m3u");
            response["success"] = true;
            response["count"] = static_cast<int>(videos.size());
        }
    }
    else if (command == "start_playlist") {
        response["command"] = "start_playlist_response";
        if (!m_playlistController) {
            response["success"] = false;
            response["message"] = "Playlist controller not available";
        } else if (m_playlistController->getPlaylistSize() == 0) {
            response["success"] = false;
            response["message"] = "No playlist set";
        } else {
            int index = root.get("index", 0).asInt();
            m_playlistController->start(index);
            if (m_config) {
                m_config->videoMode = true;
            }
            response["success"] = true;
        }
    }
    else if (command == "stop_playlist") {
        response["command"] = "stop_playlist_response";
        if (m_playlistController) {
            m_playlistController->stop();
            if (m_config) {
                m_config->videoMode = false;
            }
            response["success"] = true;
        } else {
            response["success"] = false;
            response["message"] = "Playlist controller not available";
        }
    }
    else if (command == "next_video") {
        response["command"] = "next_video_response";
        if (m_playlistController && m_playlistController->isActive()) {
            m_playlistController->next();
            response["success"] = true;
            response["currentIndex"] = m_playlistController->getCurrentIndex();
        } else {
            response["success"] = false;
            response["message"] = "No active playlist";
        }
    }
    else if (command == "prev_video") {
        response["command"] = "prev_video_response";
        if (m_playlistController && m_playlistController->isActive()) {
            m_playlistController->prev();
            response["success"] = true;
            response["currentIndex"] = m_playlistController->getCurrentIndex();
        } else {
            response["success"] = false;
            response["message"] = "No active playlist";
        }
    }
    else if (command == "get_playlist_status") {
        response["command"] = "playlist_status";
        if (m_playlistController) {
            response["active"] = m_playlistController->isActive();
            response["currentIndex"] = m_playlistController->getCurrentIndex();
            response["currentSource"] = m_playlistController->getCurrentSource();
            response["loop"] = m_playlistController->isLooping();
            response["videos"] = Json::arrayValue;
            for (const auto& v : m_playlistController->getPlaylist()) {
                response["videos"].append(v);
            }
            response["success"] = true;
        } else {
            response["active"] = false;
            response["success"] = true;
        }
    }
#endif
    // --- NDI commands ---
    else if (command == "scan_ndi_sources") {
        response["command"] = "ndi_sources";
        response["sources"] = Json::arrayValue;
        if (m_ndiReceiver && m_ndiReceiver->isRuntimeLoaded()) {
            for (const auto& src : m_ndiReceiver->getAvailableSources()) {
                response["sources"].append(src);
            }
            response["success"] = true;
        } else {
            response["success"] = false;
            response["message"] = "NDI runtime not installed. Place libndi.so in /data/lib/";
        }
    }
    else if (command == "set_ndi_source") {
        response["command"] = "set_ndi_source_response";
        if (!m_ndiReceiver || !m_ndiReceiver->isRuntimeLoaded()) {
            response["success"] = false;
            response["message"] = "NDI runtime not installed. Place libndi.so in /data/lib/";
        } else if (!root.isMember("source")) {
            response["success"] = false;
            response["message"] = "Missing 'source' field";
        } else {
            std::string source = root["source"].asString();
            m_ndiReceiver->setSource(source);
            if (!m_ndiReceiver->isConnected()) {
                m_ndiReceiver->start();
            }
            if (m_config) {
                m_config->ndiSourceName = source;
                m_config->ndiMode = true;
                m_config->saveToFile();
            }
            response["success"] = true;
            response["source"] = source;
        }
    }
    else if (command == "get_ndi_status") {
        response["command"] = "ndi_status";
        if (m_ndiReceiver) {
            response["connected"] = m_ndiReceiver->isConnected();
            response["source"] = m_ndiReceiver->getCurrentSourceName();
        } else {
            response["connected"] = false;
            response["source"] = "";
        }
        response["success"] = true;
    }
    else if (command == "stop_ndi") {
        response["command"] = "stop_ndi_response";
        if (m_ndiReceiver) {
            m_ndiReceiver->stop();
            if (m_config) {
                m_config->ndiMode = false;
                m_config->saveToFile();
            }
            response["success"] = true;
        } else {
            response["success"] = false;
            response["message"] = "NDI receiver not available";
        }
    }
    else {
        response["command"] = "error";
        response["message"] = "Unknown command";
        response["success"] = false;
    }

    sendJson(hdl, response);
}
