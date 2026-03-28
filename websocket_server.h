#pragma once
#include <asio.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <thread>
#include <atomic>
#include <functional>
#include <json/json.h>
#include "texture_manager.h"
#include "auth_manager.h"

class MDNSAdvertiser;
struct Configuration;
#ifdef HAVE_FFMPEG
class VideoDecoder;
#endif

class WebSocketServer {
public:
    using wsserver = websocketpp::server<websocketpp::config::asio>;
    
    WebSocketServer(TextureManager& textureManager, uint16_t port = 9002);
    ~WebSocketServer();
    
    void start();
    void stop();
    
    // Set mDNS advertiser for dynamic name updates
    void setMDNSAdvertiser(MDNSAdvertiser* advertiser) { m_advertiser = advertiser; }
    
    // Set configuration for device info, name persistence, and auth key loading
    void setConfiguration(Configuration* config);

#ifdef HAVE_FFMPEG
    // Set video decoder for playback control
    void setVideoDecoder(VideoDecoder* decoder) { m_videoDecoder = decoder; }
#endif
    
    // Update instance name (updates mDNS and config)
    bool setInstanceName(const std::string& newName);

private:
    void run();
    void onOpen(websocketpp::connection_hdl hdl);
    void onClose(websocketpp::connection_hdl hdl);
    void onMessage(websocketpp::connection_hdl hdl, wsserver::message_ptr msg);
    void sendJson(websocketpp::connection_hdl hdl, const Json::Value& response);

    wsserver server;
    std::thread serverThread;
    TextureManager& textureManager;
    std::atomic<bool> running{false};
    uint16_t port;
    MDNSAdvertiser* m_advertiser = nullptr;
    Configuration* m_config = nullptr;
    AuthManager m_auth;
#ifdef HAVE_FFMPEG
    VideoDecoder* m_videoDecoder = nullptr;
#endif
};
