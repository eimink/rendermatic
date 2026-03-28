#include "auth_manager.h"
#include "picosha2.h"
#include <algorithm>

AuthManager::AuthManager() = default;

bool AuthManager::setKey(const std::string& plaintextKey) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keyHash = sha256Hex(plaintextKey);
    return true;
}

bool AuthManager::clearKey() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keyHash.clear();
    return true;
}

bool AuthManager::isAuthEnabled() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_keyHash.empty();
}

void AuthManager::setStoredHash(const std::string& hash) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keyHash = hash;
}

std::string AuthManager::getStoredHash() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_keyHash;
}

void AuthManager::onConnectionOpened(connection_hdl hdl, const std::string& remoteIp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ConnectionAuthState state;
    state.remoteIp = remoteIp;
    m_connections[hdl] = state;
}

void AuthManager::onConnectionClosed(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_connections.erase(hdl);
}

bool AuthManager::isAuthenticated(connection_hdl hdl) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_connections.find(hdl);
    if (it == m_connections.end()) return false;
    return it->second.authenticated;
}

bool AuthManager::isOpenMode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_keyHash.empty();
}

bool AuthManager::tryAuthenticate(connection_hdl hdl, const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto connIt = m_connections.find(hdl);
    if (connIt == m_connections.end()) return false;

    const std::string& ip = connIt->second.remoteIp;
    std::string keyHash = sha256Hex(key);

    // Constant-time comparison to prevent timing side-channel attacks
    bool match = (keyHash.size() == m_keyHash.size());
    volatile unsigned char result = 0;
    size_t len = std::min(keyHash.size(), m_keyHash.size());
    for (size_t i = 0; i < len; i++) {
        result |= keyHash[i] ^ m_keyHash[i];
    }
    match = match && (result == 0);

    if (match) {
        connIt->second.authenticated = true;
        // Reset rate limit on success
        m_rateLimits.erase(ip);
        return true;
    }

    // Record failure for rate limiting
    auto& rl = m_rateLimits[ip];
    rl.failedAttempts++;
    rl.lastAttempt = std::chrono::steady_clock::now();
    if (rl.failedAttempts >= MAX_FAILED_ATTEMPTS) {
        rl.lockoutUntil = rl.lastAttempt + std::chrono::seconds(LOCKOUT_SECONDS);
    }

    return false;
}

bool AuthManager::isRateLimited(connection_hdl hdl) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto connIt = m_connections.find(hdl);
    if (connIt == m_connections.end()) return false;

    const std::string& ip = connIt->second.remoteIp;
    auto rlIt = m_rateLimits.find(ip);
    if (rlIt == m_rateLimits.end()) return false;

    auto now = std::chrono::steady_clock::now();

    // Clean up stale entries
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - rlIt->second.lastAttempt).count();
    if (elapsed > RATE_LIMIT_CLEANUP_SECONDS) {
        m_rateLimits.erase(rlIt);
        return false;
    }

    if (rlIt->second.failedAttempts >= MAX_FAILED_ATTEMPTS && now < rlIt->second.lockoutUntil) {
        return true;
    }

    return false;
}

int AuthManager::getRemainingLockoutSeconds(connection_hdl hdl) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto connIt = m_connections.find(hdl);
    if (connIt == m_connections.end()) return 0;

    const std::string& ip = connIt->second.remoteIp;
    auto rlIt = m_rateLimits.find(ip);
    if (rlIt == m_rateLimits.end()) return 0;

    auto now = std::chrono::steady_clock::now();
    if (now >= rlIt->second.lockoutUntil) return 0;

    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
        rlIt->second.lockoutUntil - now).count());
}

void AuthManager::markAuthenticated(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_connections.find(hdl);
    if (it != m_connections.end()) {
        it->second.authenticated = true;
    }
}

std::string AuthManager::sha256Hex(const std::string& input) {
    return picosha2::hash256_hex_string(input);
}
