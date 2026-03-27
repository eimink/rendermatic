#include "websocket_server.h"
#include "mdns_advertiser.h"
#include "config.h"
#ifdef HAVE_FFMPEG
#include "video_decoder.h"
#endif
#include <json/json.h>
#include <unistd.h>

WebSocketServer::WebSocketServer(TextureManager& tm, uint16_t port) 
    : textureManager(tm), running(false), port(port) {
    server.clear_access_channels(websocketpp::log::alevel::all);
    server.init_asio();
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

bool WebSocketServer::setInstanceName(const std::string& newName) {
    if (!m_config) {
        return false;
    }
    
    // Update config in memory
    m_config->instanceName = newName;
    
    // Persist to file
    if (!m_config->saveToFile()) {
        return false;
    }
    
    // Update mDNS if available
    if (m_advertiser) {
        m_advertiser->updateInstanceName(newName);
    }
    
    return true;
}

void WebSocketServer::onMessage(websocketpp::connection_hdl hdl, wsserver::message_ptr msg) {
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(msg->get_payload(), root)) {
        return;
    }

    std::string command = root["command"].asString();
    Json::Value response;

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
        // Return device information including current texture
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
        response["success"] = true;
    }
    else if (command == "set_device_name") {
        // Update device instance name
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

    Json::FastWriter writer;
    server.send(hdl, writer.write(response), websocketpp::frame::opcode::text);
}
