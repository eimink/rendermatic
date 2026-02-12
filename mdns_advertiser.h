#pragma once
#include <string>
#include <cstdint>
#include <memory>

struct AvahiContext;

class MDNSAdvertiser {
public:
    // Constructor takes instance name and WebSocket port
    MDNSAdvertiser(const std::string& instanceName, uint16_t port);
    ~MDNSAdvertiser();
    
    // Publish service via Avahi (mDNS)
    // Warns and continues if Avahi daemon unavailable
    bool publish();
    
    // Unpublish service from Avahi
    void unpublish();
    
    // Update instance name and re-publish
    // Safe to call at any time; updates mDNS if available
    bool updateInstanceName(const std::string& newName);
    
    // Get current instance name
    std::string getInstanceName() const { return m_instanceName; }
    
    // Get WebSocket port
    uint16_t getPort() const { return m_port; }

private:
    std::string m_instanceName;
    uint16_t m_port;
    std::unique_ptr<AvahiContext> m_avahi;
    bool m_published = false;
};
