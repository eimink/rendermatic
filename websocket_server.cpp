#include "websocket_server.h"
#include "mdns_advertiser.h"
#include "config.h"
#ifdef HAVE_FFMPEG
#include "video_decoder.h"
#endif
#include <unistd.h>

WebSocketServer::WebSocketServer(TextureManager& tm, uint16_t port)
    : textureManager(tm), running(false), port(port) {
    server.clear_access_channels(websocketpp::log::alevel::all);
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
    else if (command == "load_texture") {
        std::string textureName = root["texture"].asString();
        bool success = textureManager.loadTexture(textureName);
        response["command"] = "load_texture_response";
        response["success"] = success;
    }
    else if (command == "set_texture") {
        std::string textureName = root["texture"].asString();
        bool success = textureManager.setCurrentTexture(textureName);
        response["command"] = "set_texture_response";
        response["success"] = success;
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
            bool loop = root.get("loop", true).asBool();
            m_videoDecoder->stop();
            m_videoDecoder->setLoop(loop);
            if (m_videoDecoder->open(source)) {
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
#endif
    else {
        response["command"] = "error";
        response["message"] = "Unknown command";
        response["success"] = false;
    }

    sendJson(hdl, response);
}
