#pragma once
#include <asio.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <thread>
#include <functional>
#include "texture_manager.h"

class MDNSAdvertiser;
struct Configuration;

class WebSocketServer {
public:
    using wsserver = websocketpp::server<websocketpp::config::asio>;
    
    WebSocketServer(TextureManager& textureManager, uint16_t port = 9002);
    ~WebSocketServer();
    
    void start();
    void stop();
    
    // Set mDNS advertiser for dynamic name updates
    void setMDNSAdvertiser(MDNSAdvertiser* advertiser) { m_advertiser = advertiser; }
    
    // Set configuration for device info and name persistence
    void setConfiguration(Configuration* config) { m_config = config; }
    
    // Update instance name (updates mDNS and config)
    bool setInstanceName(const std::string& newName);

private:
    void run();
    void onMessage(websocketpp::connection_hdl hdl, wsserver::message_ptr msg);
    
    wsserver server;
    std::thread serverThread;
    bool running;
    TextureManager& textureManager;
    uint16_t port;
    MDNSAdvertiser* m_advertiser = nullptr;
    Configuration* m_config = nullptr;
};
