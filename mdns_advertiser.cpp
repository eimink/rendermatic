#include "mdns_advertiser.h"
#include <iostream>

#ifdef HAVE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/thread-watch.h>

// Forward declarations for callbacks
static void avahi_client_callback(AvahiClient* c, AvahiClientState state, void* userdata);
static void avahi_entry_group_callback(AvahiEntryGroup* g, AvahiEntryGroupState state, void* userdata);

// Avahi client wrapper - hold raw Avahi pointers
struct AvahiContext {
    AvahiClient* client = nullptr;
    AvahiEntryGroup* group = nullptr;
    AvahiThreadedPoll* threadedPoll = nullptr;
};

static void avahi_client_callback(AvahiClient* c, AvahiClientState state, void* userdata) {
    (void)userdata;  // Suppress unused parameter warning
    
    // Handle state changes
    switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            // Server has startup successfully and registered its host name on the network
            break;
        case AVAHI_CLIENT_FAILURE:
            std::cerr << "Warning: Avahi client failure: " << avahi_strerror(avahi_client_errno(c)) << std::endl;
            break;
        case AVAHI_CLIENT_S_COLLISION:
            // Host name collision
            std::cerr << "Warning: Avahi host name collision" << std::endl;
            break;
        case AVAHI_CLIENT_S_REGISTERING:
            // Registering
            break;
        case AVAHI_CLIENT_CONNECTING:
            // Connecting
            break;
    }
}

static void avahi_entry_group_callback(AvahiEntryGroup* g, AvahiEntryGroupState state, void* userdata) {
    (void)g;         // Suppress unused parameter warning
    (void)userdata;  // Suppress unused parameter warning
    
    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED:
            // Service successfully published
            break;
        case AVAHI_ENTRY_GROUP_COLLISION:
            std::cerr << "Warning: Avahi service collision" << std::endl;
            break;
        case AVAHI_ENTRY_GROUP_FAILURE:
            std::cerr << "Warning: Avahi entry group failure" << std::endl;
            break;
        case AVAHI_ENTRY_GROUP_UNCOMMITED:
            // Not yet committed
            break;
        case AVAHI_ENTRY_GROUP_REGISTERING:
            // Registering
            break;
    }
}

#endif // HAVE_AVAHI


MDNSAdvertiser::MDNSAdvertiser(const std::string& instanceName, uint16_t port)
    : m_instanceName(instanceName), m_port(port), m_published(false) {
#ifdef HAVE_AVAHI
    m_avahi = std::make_unique<AvahiContext>();
#endif
}

MDNSAdvertiser::~MDNSAdvertiser() {
    unpublish();
}

bool MDNSAdvertiser::publish() {
#ifdef HAVE_AVAHI
    if (!m_avahi) {
        m_avahi = std::make_unique<AvahiContext>();
    }
    
    int error = 0;
    
    // Create threaded poll object
    m_avahi->threadedPoll = avahi_threaded_poll_new();
    if (!m_avahi->threadedPoll) {
        std::cerr << "Warning: Failed to create Avahi threaded poll object" << std::endl;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }
    
    // Create Avahi client
    AvahiClientFlags flags = (AvahiClientFlags)0;
    m_avahi->client = avahi_client_new(avahi_threaded_poll_get(m_avahi->threadedPoll), flags, avahi_client_callback, nullptr, &error);
    if (!m_avahi->client) {
        std::cerr << "Warning: Failed to create Avahi client: " << avahi_strerror(error) << std::endl;
        avahi_threaded_poll_free(m_avahi->threadedPoll);
        m_avahi->threadedPoll = nullptr;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }
    
    // Start polling
    if (avahi_threaded_poll_start(m_avahi->threadedPoll) < 0) {
        std::cerr << "Warning: Failed to start Avahi threaded poll" << std::endl;
        avahi_client_free(m_avahi->client);
        avahi_threaded_poll_free(m_avahi->threadedPoll);
        m_avahi->client = nullptr;
        m_avahi->threadedPoll = nullptr;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }
    
    // Create entry group
    m_avahi->group = avahi_entry_group_new(m_avahi->client, avahi_entry_group_callback, nullptr);
    if (!m_avahi->group) {
        std::cerr << "Warning: Failed to create Avahi entry group" << std::endl;
        avahi_client_free(m_avahi->client);
        avahi_threaded_poll_free(m_avahi->threadedPoll);
        m_avahi->client = nullptr;
        m_avahi->threadedPoll = nullptr;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }
    
    // Add service
    if (avahi_entry_group_add_service(
        m_avahi->group,
        AVAHI_IF_UNSPEC,
        AVAHI_PROTO_UNSPEC,
        (AvahiPublishFlags)0,
        m_instanceName.c_str(),
        "_rendermatic._tcp",
        nullptr,
        nullptr,
        m_port,
        nullptr) < 0) {
        
        std::cerr << "Warning: Failed to add Avahi service" << std::endl;
        avahi_entry_group_free(m_avahi->group);
        avahi_client_free(m_avahi->client);
        avahi_threaded_poll_free(m_avahi->threadedPoll);
        m_avahi->group = nullptr;
        m_avahi->client = nullptr;
        m_avahi->threadedPoll = nullptr;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }
    
    // Commit the entry group
    if (avahi_entry_group_commit(m_avahi->group) < 0) {
        std::cerr << "Warning: Failed to commit Avahi entry group" << std::endl;
        avahi_entry_group_free(m_avahi->group);
        avahi_client_free(m_avahi->client);
        avahi_threaded_poll_free(m_avahi->threadedPoll);
        m_avahi->group = nullptr;
        m_avahi->client = nullptr;
        m_avahi->threadedPoll = nullptr;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }
    
    m_published = true;
    std::cout << "Published mDNS service: " << m_instanceName << "._rendermatic._tcp on port " << m_port << std::endl;
    return true;
#else
    std::cerr << "Warning: Avahi support not compiled in, mDNS discovery unavailable" << std::endl;
    return false;
#endif
}

void MDNSAdvertiser::unpublish() {
#ifdef HAVE_AVAHI
    if (m_avahi) {
        if (m_avahi->group) {
            avahi_entry_group_free(m_avahi->group);
            m_avahi->group = nullptr;
        }
        if (m_avahi->threadedPoll) {
            avahi_threaded_poll_stop(m_avahi->threadedPoll);
        }
        if (m_avahi->client) {
            avahi_client_free(m_avahi->client);
            m_avahi->client = nullptr;
        }
        if (m_avahi->threadedPoll) {
            avahi_threaded_poll_free(m_avahi->threadedPoll);
            m_avahi->threadedPoll = nullptr;
        }
        m_published = false;
    }
#endif
}

bool MDNSAdvertiser::updateInstanceName(const std::string& newName) {
    m_instanceName = newName;
    
#ifdef HAVE_AVAHI
    if (m_published && m_avahi && m_avahi->client && m_avahi->group) {
        // Reset entry group
        avahi_entry_group_reset(m_avahi->group);
        
        // Re-add service with new name
        if (avahi_entry_group_add_service(
            m_avahi->group,
            AVAHI_IF_UNSPEC,
            AVAHI_PROTO_UNSPEC,
            (AvahiPublishFlags)0,
            m_instanceName.c_str(),
            "_rendermatic._tcp",
            nullptr,
            nullptr,
            m_port,
            nullptr) < 0) {
            
            std::cerr << "Warning: Failed to add updated Avahi service" << std::endl;
            return true;  // Still succeed for naming purposes
        }
        
        // Commit
        if (avahi_entry_group_commit(m_avahi->group) < 0) {
            std::cerr << "Warning: Failed to commit updated Avahi entry group" << std::endl;
            return true;  // Still succeed for naming purposes
        }
        
        std::cout << "Updated mDNS instance name to: " << newName << std::endl;
    }
#endif
    
    // Name is updated in memory regardless of Avahi success
    return true;
}
