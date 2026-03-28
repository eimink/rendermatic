#pragma once
#include <string>
#include <map>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

struct ConnectionAuthState {
    bool authenticated = false;
    int failedAttempts = 0;
    std::chrono::steady_clock::time_point lastAttempt;
    std::string remoteIp;
};

struct RateLimitEntry {
    int failedAttempts = 0;
    std::chrono::steady_clock::time_point lastAttempt;
    std::chrono::steady_clock::time_point lockoutUntil;
};

class AuthManager {
public:
    using connection_hdl = websocketpp::connection_hdl;

    AuthManager();

    // Key management
    bool setKey(const std::string& plaintextKey);
    bool clearKey();
    bool isAuthEnabled() const;

    // Load/save hash from/to config fields
    void setStoredHash(const std::string& hash);
    std::string getStoredHash() const;

    // Connection lifecycle
    void onConnectionOpened(connection_hdl hdl, const std::string& remoteIp);
    void onConnectionClosed(connection_hdl hdl);

    // Auth check
    bool isAuthenticated(connection_hdl hdl) const;
    bool isOpenMode() const;

    // Returns true if auth succeeded
    bool tryAuthenticate(connection_hdl hdl, const std::string& key);

    // Rate limiting - returns true if the IP is currently locked out
    bool isRateLimited(connection_hdl hdl) const;
    int getRemainingLockoutSeconds(connection_hdl hdl) const;

    // Mark connection as authenticated (used for first-time key setup)
    void markAuthenticated(connection_hdl hdl);

    static std::string sha256Hex(const std::string& input);

private:
    std::string m_keyHash;
    mutable std::mutex m_mutex;

    std::map<connection_hdl, ConnectionAuthState,
             std::owner_less<connection_hdl>> m_connections;

    mutable std::map<std::string, RateLimitEntry> m_rateLimits;

    static constexpr int MAX_FAILED_ATTEMPTS = 5;
    static constexpr int LOCKOUT_SECONDS = 60;
    static constexpr int RATE_LIMIT_CLEANUP_SECONDS = 300;
};
