#include "config.h"
#include <json/json.h>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <cstdio>

#ifdef __APPLE__
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <net/if.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if_arp.h>
#include <cstring>
#include <dirent.h>
#endif

// Returns last 3 bytes of the MAC address as a hex string (e.g. "a1b2c3").
// Falls back to hostname if no usable interface is found.
static std::string getDeviceId() {
#ifdef __APPLE__
    struct ifaddrs* iflist = nullptr;
    if (getifaddrs(&iflist) == 0) {
        for (auto* ifa = iflist; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_LINK &&
                (ifa->ifa_flags & IFF_LOOPBACK) == 0 &&
                (ifa->ifa_flags & IFF_UP)) {
                auto* sdl = reinterpret_cast<struct sockaddr_dl*>(ifa->ifa_addr);
                if (sdl->sdl_alen == 6) {
                    auto* mac = reinterpret_cast<unsigned char*>(LLADDR(sdl));
                    char buf[7];
                    snprintf(buf, sizeof(buf), "%02x%02x%02x", mac[3], mac[4], mac[5]);
                    freeifaddrs(iflist);
                    return std::string(buf);
                }
            }
        }
        freeifaddrs(iflist);
    }
#else
    // Linux: read MAC from /sys/class/net/*/address, skip lo
    DIR* dir = opendir("/sys/class/net");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string iface = entry->d_name;
            if (iface == "." || iface == ".." || iface == "lo") continue;

            std::string path = "/sys/class/net/" + iface + "/address";
            std::ifstream f(path);
            std::string mac;
            if (f.is_open() && std::getline(f, mac) && mac.size() >= 17) {
                // mac format: "aa:bb:cc:dd:ee:ff" — use last 3 octets
                char buf[7];
                unsigned int d, e, ff;
                if (sscanf(mac.c_str() + 9, "%02x:%02x:%02x", &d, &e, &ff) == 3) {
                    snprintf(buf, sizeof(buf), "%02x%02x%02x", d, e, ff);
                    closedir(dir);
                    return std::string(buf);
                }
            }
        }
        closedir(dir);
    }
#endif

    // Fallback to hostname
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "000000";
}

Configuration Configuration::loadFromFile(const std::string& path) {
    Configuration config;
    try {
        std::ifstream file(path);
        if (file.is_open()) {
            Json::Value root;
            Json::Reader reader;
            if (reader.parse(file, root)) {
                config.fullscreen = root.get("fullscreen", true).asBool();
                config.fullscreenScaling = root.get("fullscreenScaling", false).asBool();
                config.monitorIndex = root.get("monitorIndex", 0).asInt();
                config.ndiMode = root.get("ndiMode", false).asBool();
                config.backend = root.get("backend", "glfw").asString();
                config.width = root.get("width", 1920).asInt();
                config.height = root.get("height", 1080).asInt();
                config.wsPort = root.get("wsPort", 9002).asUInt();
                config.instanceName = root.get("instanceName", "").asString();
                config.videoSource = root.get("videoSource", "").asString();
                config.videoMode = root.get("videoMode", false).asBool();
                config.videoLoop = root.get("videoLoop", true).asBool();
                config.authKeyHash = root.get("authKeyHash", "").asString();
                config.splashDurationSeconds = root.get("splashDurationSeconds", 5).asInt();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        // Use defaults if config loading fails
        config.fullscreen = true;
        config.fullscreenScaling = false;
        config.monitorIndex = 0;
        config.ndiMode = false;
        config.backend = "glfw";
        config.width = 1920;
        config.height = 1080;
    }
    
    // Auto-generate instance name if not set
    if (config.instanceName.empty()) {
        config.instanceName = "rendermatic-" + getDeviceId();
        // Persist auto-generated name back to config file
        config.saveToFile(path);
    }
    
    return config;
}

bool Configuration::saveToFile(const std::string& path) const {
    try {
        Json::Value root;
        root["fullscreen"] = fullscreen;
        root["fullscreenScaling"] = fullscreenScaling;
        root["monitorIndex"] = monitorIndex;
        root["ndiMode"] = ndiMode;
        root["backend"] = backend;
        root["width"] = width;
        root["height"] = height;
        root["wsPort"] = wsPort;
        root["instanceName"] = instanceName;
        root["videoSource"] = videoSource;
        root["videoMode"] = videoMode;
        root["videoLoop"] = videoLoop;
        root["authKeyHash"] = authKeyHash;
        root["splashDurationSeconds"] = splashDurationSeconds;
        
        std::ofstream file(path);
        if (!file.is_open()) {
            std::cerr << "Failed to open config file for writing: " << path << std::endl;
            return false;
        }
        
        Json::StyledWriter writer;
        file << writer.write(root);
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error saving config: " << e.what() << std::endl;
        return false;
    }
}

void Configuration::overrideFromCommandLine(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-w") {
            fullscreen = false;
        } else if (arg == "-m" && i + 1 < argc) {
            monitorIndex = std::atoi(argv[++i]);
        } else if (arg == "-n") {
            ndiMode = true;
        } else if (arg == "-b" && i + 1 < argc) {
            backend = argv[++i];
        } else if (arg == "-r" && i + 2 < argc) {
            width = std::atoi(argv[++i]);
            height = std::atoi(argv[++i]);
            if (width <= 0 || height <= 0) {
                std::cerr << "Invalid resolution: " << width << "x" << height << std::endl;
                exit(-1);
            }
        } else if (arg == "-f") {
            fullscreenScaling = true;
        } else if (arg == "-i" && i + 1 < argc) {
            instanceName = argv[++i];
        } else if (arg == "-v" && i + 1 < argc) {
            videoSource = argv[++i];
            videoMode = true;
        } else if (arg == "--no-loop") {
            videoLoop = false;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            exit(0);
        }
    }
}

void Configuration::printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [-w] [-m monitor_index] [-n] [-b backend] [-r width height] [-f] [-i instance_name]\n"
              << "  -w                : Launch in windowed mode\n"
              << "  -m <index>        : Specify monitor index (default: " << monitorIndex << ")\n"
              << "  -n                : Enable NDI mode\n"
              << "  -b <backend>      : Rendering backend (glfw/dfb/dfb-pure, default: " << backend << ")\n"
              << "  -r <w> <h>        : Set resolution (default: " << width << "x" << height << ")\n"
              << "  -f                : Enable fullscreen scaling\n"
              << "  -i <instance_name>: Set instance name (default: rendermatic-{hostname})\n"
              << "  -v <source>       : Play video file or stream (rtmp://, rtsp://, srt://, or file path)\n"
              << "  --no-loop         : Disable video file looping\n";
}
