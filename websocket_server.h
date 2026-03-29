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
#include "irenderer.h"

class MDNSAdvertiser;
class SplashController;
class NDIReceiver;
struct Configuration;
#ifdef HAVE_FFMPEG
class VideoDecoder;
class PlaylistController;
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
    void setSplashController(SplashController* controller) { m_splashController = controller; }
    void setRenderer(IRenderer* renderer) { m_renderer = renderer; }
    void setNDIReceiver(NDIReceiver* ndi) { m_ndiReceiver = ndi; }
    
    // Set configuration for device info, name persistence, and auth key loading
    void setConfiguration(Configuration* config);

#ifdef HAVE_FFMPEG
    // Set video decoder for playback control
    void setVideoDecoder(VideoDecoder* decoder) { m_videoDecoder = decoder; }
    void setPlaylistController(PlaylistController* controller) { m_playlistController = controller; }
#endif
    
    // Update instance name (updates mDNS and config)
    bool setInstanceName(const std::string& newName);

    // Scan media directory for video files
    std::vector<std::string> scanVideos();

private:
    void run();
    void onOpen(websocketpp::connection_hdl hdl);
    void onClose(websocketpp::connection_hdl hdl);
    void onMessage(websocketpp::connection_hdl hdl, wsserver::message_ptr msg);
    void sendJson(websocketpp::connection_hdl hdl, const Json::Value& response);

    std::vector<std::string> m_availableVideos;

    wsserver server;
    std::thread serverThread;
    TextureManager& textureManager;
    std::atomic<bool> running{false};
    uint16_t port;
    MDNSAdvertiser* m_advertiser = nullptr;
    SplashController* m_splashController = nullptr;
    IRenderer* m_renderer = nullptr;
    NDIReceiver* m_ndiReceiver = nullptr;
    Configuration* m_config = nullptr;
    AuthManager m_auth;
#ifdef HAVE_FFMPEG
    VideoDecoder* m_videoDecoder = nullptr;
    PlaylistController* m_playlistController = nullptr;
#endif
};
