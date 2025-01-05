#pragma once
#define ASIO_STANDALONE
#define _WEBSOCKETPP_CPP11_INTERNAL_
#include <asio.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <thread>
#include <functional>
#include "texture_manager.h"

class WebSocketServer {
public:
    using wsserver = websocketpp::server<websocketpp::config::asio>;
    
    WebSocketServer(TextureManager& textureManager, uint16_t port = 9002);  // Add port parameter
    ~WebSocketServer();
    
    void start();
    void stop();

private:
    void run();
    void onMessage(websocketpp::connection_hdl hdl, wsserver::message_ptr msg);
    
    wsserver server;
    std::thread serverThread;
    bool running;
    TextureManager& textureManager;
    uint16_t port;  // Add port member
};
