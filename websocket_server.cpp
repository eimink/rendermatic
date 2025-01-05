#include "websocket_server.h"
#include <json/json.h>

WebSocketServer::WebSocketServer(TextureManager& tm) 
    : textureManager(tm), running(false) {
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
        server.listen(9002);
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

void WebSocketServer::onMessage(websocketpp::connection_hdl hdl, wsserver::message_ptr msg) {
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(msg->get_payload(), root)) {
        return;
    }

    std::string command = root["command"].asString();
    Json::Value response;

    if (command == "list_textures") {
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
    else {
        response["command"] = "error";
        response["message"] = "Unknown command";
        response["success"] = false;
    }

    Json::FastWriter writer;
    server.send(hdl, writer.write(response), websocketpp::frame::opcode::text);
}
