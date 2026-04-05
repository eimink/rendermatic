#include "dfb_pure_renderer.h"
#include "drm_egl_renderer.h"
#include "irenderer.h"
#include "loader.h"
#include "config.h"
#include "ndireceiver.h"
#include "texture_manager.h"
#include "websocket_server.h"
#include "splash_controller.h"
#include "mdns_advertiser.h"
#include "log.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <libgen.h>

#ifdef HAVE_DIRECTFB
    #include "dfb_renderer.h"
#endif

#ifdef ENABLE_GLFW
    #include "glfw_renderer.h"
#endif

#ifdef HAVE_FFMPEG
    #include "video_decoder.h"
    #include "playlist_controller.h"
#endif

int main(int argc, char* argv[]) {
    std::cout << "--- rendermatic starting ---" << std::endl;

    // Change working directory to the executable's directory
    // This allows the app to find shaders, textures, and config.json
    std::string exePath = argv[0];
    std::string exeDir = std::filesystem::path(exePath).parent_path().string();
    if (!exeDir.empty()) {
        try {
            std::filesystem::current_path(exeDir);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not change to executable directory: " << e.what() << std::endl;
        }
    }
    
    auto config = Configuration::loadFromFile();
    config.overrideFromCommandLine(argc, argv);
    setLogLevel(parseLogLevel(config.logLevel));

    std::unique_ptr<IRenderer> renderer;

#ifdef DFB_PURE_ONLY
    renderer = std::make_unique<DirectFBPureRenderer>();
#else
#ifdef DFB_ONLY
    renderer = std::make_unique<DrmEglRenderer>();
#else
    if (config.backend == "glfw") {
        renderer = std::make_unique<GLFWRenderer>();
    } else if (config.backend == "dfb") {
#ifdef HAVE_DIRECTFB
        renderer = std::make_unique<DirectFBRenderer>();
#else
        std::cerr << "DirectFB support not compiled in" << std::endl;
        return -1;
#endif
    } else if (config.backend == "dfb-pure") {
#ifdef HAVE_DIRECTFB
        renderer = std::make_unique<DirectFBPureRenderer>();
#else
        std::cerr << "Pure DirectFB support not compiled in" << std::endl;
        return -1;
#endif
    } else {
        std::cerr << "Unknown backend: " << config.backend << std::endl;
        std::cerr << "Available backends: glfw";
#ifdef HAVE_DIRECTFB
        std::cerr << ", dfb, dfb-pure";
#endif
        std::cerr << std::endl;
        return -1;
    }
#endif
#endif

    NDIReceiver ndiReceiver;

    TextureManager textureManager;
    std::string textureName;
    textureManager.scanTextureDirectory();  // Scan current directory for textures

    // Initialize mDNS advertiser
    MDNSAdvertiser mdnsAdvertiser(config.instanceName, config.wsPort);

    // Initialize WebSocket server
    WebSocketServer wsServer(textureManager, config.wsPort);
    wsServer.setMDNSAdvertiser(&mdnsAdvertiser);
    wsServer.setConfiguration(&config);
    wsServer.start();
    
    // Publish mDNS service (warns and continues if Avahi unavailable)
    mdnsAdvertiser.publish();
    
    if (!renderer->init(config.width, config.height, "Display", config.fullscreen, config.monitorIndex)) {
#ifdef DFB_ONLY
        std::cerr << "DRM/EGL failed, falling back to software renderer" << std::endl;
        renderer = std::make_unique<DirectFBPureRenderer>();
        if (!renderer->init(config.width, config.height, "Display", config.fullscreen, config.monitorIndex)) {
            std::cerr << "Software renderer also failed" << std::endl;
            return -1;
        }
#else
        return -1;
#endif
    }
    renderer->setFullscreenScaling(config.fullscreenScaling);
    renderer->setRotation(config.displayRotation);
    wsServer.setRenderer(renderer.get());

    // Load default texture for image mode
    if (!textureManager.loadTexture("default.jpg")) {
        std::cerr << "Failed to load default texture" << std::endl;
        return -1;
    }
    textureManager.setCurrentTexture("default.jpg");
    Texture displayTexture = textureManager.getCurrentTextureCopy();

    ndiReceiver.loadRuntime();  // Loads libndi.so if present, no-op if not
    wsServer.setNDIReceiver(&ndiReceiver);
    if (config.ndiMode && ndiReceiver.isRuntimeLoaded()) {
        if (!config.ndiSourceName.empty()) {
            ndiReceiver.setSource(config.ndiSourceName);
        }
        ndiReceiver.start();
    }

#ifdef HAVE_FFMPEG
    auto videoDecoder = std::make_unique<VideoDecoder>();
    wsServer.setVideoDecoder(videoDecoder.get());

    // Start single video if configured
    if (config.videoMode && !config.videoSource.empty()) {
        videoDecoder->setLoop(config.videoLoop);
        if (videoDecoder->open(config.videoSource)) {
            videoDecoder->start();
        } else {
            std::cerr << "Failed to open video source: " << config.videoSource << std::endl;
        }
    }

    // Playlist controller
    PlaylistController playlistController(videoDecoder.get());
    wsServer.setPlaylistController(&playlistController);

    // Load playlist from file if present
    playlistController.loadFromFile("playlist.m3u");
#endif

    // Splash/identify overlay controller — use actual display resolution
    int displayWidth = renderer->getWidth() > 0 ? renderer->getWidth() : config.width;
    int displayHeight = renderer->getHeight() > 0 ? renderer->getHeight() : config.height;
    SplashController splashController(displayWidth, displayHeight, config.wsPort);
    wsServer.setSplashController(&splashController);

    // Show boot overlay
    if (config.splashDurationSeconds > 0 && !config.videoMode) {
        splashController.trigger(config.splashDurationSeconds);
    }

    // Media clock - tracks playback position for file-based content
    struct MediaClock {
        double startPts = 0.0;
        std::chrono::steady_clock::time_point startWall;
        bool started = false;

        double time() const {
            if (!started) return 0.0;
            auto elapsed = std::chrono::steady_clock::now() - startWall;
            return startPts + std::chrono::duration<double>(elapsed).count();
        }

        void sync(double pts) {
            startPts = pts;
            startWall = std::chrono::steady_clock::now();
            started = true;
        }

        void reset() { started = false; }
    };

    // Main render loop - fixed rate
    Texture videoFrame;
    MediaClock mediaClock;
    double m_nextFramePts = 0.0;
    auto targetFrameTime = std::chrono::microseconds(1000000 / config.targetFps);

    while (!renderer->shouldClose()) {
        auto frameStart = std::chrono::steady_clock::now();

        renderer->processInput();

        std::string currentName = textureManager.getCurrentTextureName();
        if (currentName != textureName) {
            displayTexture = textureManager.getCurrentTextureCopy();
            textureName = currentName;
        }

        bool rendered = false;
        static int frameCount = 0;
        static auto lastLog = std::chrono::steady_clock::now();

#ifdef HAVE_FFMPEG
        if (videoDecoder && videoDecoder->isActive()) {
            bool gotFrame = false;

            if (videoDecoder->isStream()) {
                gotFrame = videoDecoder->getLatestFrame(videoFrame);
            } else {
                if (!mediaClock.started) {
                    Texture firstFrame;
                    if (videoDecoder->getFrameForTime(0.0, firstFrame)) {
                        videoFrame = firstFrame;
                        mediaClock.sync(0.0);
                        gotFrame = true;
                        LOG_INFO("Media clock started");
                    }
                } else {
                    double t = mediaClock.time();
                    gotFrame = videoDecoder->getFrameForTime(t, videoFrame);
                    videoDecoder->setPlaybackTime(t);
                }
            }

            if (gotFrame || videoFrame.isValid()) {
                renderer->render(videoFrame);
                rendered = true;
                frameCount++;
            }

            // Log stats every 5 seconds
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - lastLog).count() >= 5.0) {
                double elapsed = std::chrono::duration<double>(now - lastLog).count();
                double actualFps = frameCount / elapsed;
                LOG_DEBUG("Render: " << actualFps << " fps"
                          << " | clock: " << (mediaClock.started ? mediaClock.time() : -1.0)
                          << "s | frame valid: " << videoFrame.isValid()
                          << " | active: " << videoDecoder->isActive());
                frameCount = 0;
                lastLog = now;
            }
        } else {
            mediaClock.reset();
        }
#endif

        if (!rendered && config.ndiMode && ndiReceiver.isConnected()) {
            Texture currentFrame;
            if (ndiReceiver.getLatestFrame(currentFrame)) {
                renderer->render(currentFrame);
                rendered = true;
            }
        }

        if (!rendered && displayTexture.isValid()) {
            renderer->render(displayTexture);
        }

        if (splashController.isActive()) {
            Texture overlay = splashController.getOverlayTexture();
            if (overlay.isValid()) {
                renderer->renderOverlay(overlay);
            }
        }

        renderer->present();

        // Fixed frame rate cap
        auto frameEnd = std::chrono::steady_clock::now();
        auto elapsed = frameEnd - frameStart;
        if (elapsed < targetFrameTime) {
            std::this_thread::sleep_for(targetFrameTime - elapsed);
        }
    }

    ndiReceiver.stop();

#ifdef HAVE_FFMPEG
    if (videoDecoder) {
        videoDecoder->stop();
    }
#endif

    // Before return, stop WebSocket server
    wsServer.stop();
    return 0;
}