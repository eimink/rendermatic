#include "mdns_advertiser.h"
#include <iostream>

#ifdef HAVE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/thread-watch.h>

static void avahi_client_callback(AvahiClient* c, AvahiClientState state, void* userdata);
static void avahi_entry_group_callback(AvahiEntryGroup* g, AvahiEntryGroupState state, void* userdata);

struct MDNSContext {
    AvahiClient* client = nullptr;
    AvahiEntryGroup* group = nullptr;
    AvahiThreadedPoll* threadedPoll = nullptr;
};

static void avahi_client_callback(AvahiClient* c, AvahiClientState state, void* userdata) {
    (void)userdata;
    switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            break;
        case AVAHI_CLIENT_FAILURE:
            std::cerr << "Warning: Avahi client failure: " << avahi_strerror(avahi_client_errno(c)) << std::endl;
            break;
        case AVAHI_CLIENT_S_COLLISION:
            std::cerr << "Warning: Avahi host name collision" << std::endl;
            break;
        case AVAHI_CLIENT_S_REGISTERING:
            break;
        case AVAHI_CLIENT_CONNECTING:
            break;
    }
}

static void avahi_entry_group_callback(AvahiEntryGroup* g, AvahiEntryGroupState state, void* userdata) {
    (void)g;
    (void)userdata;
    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED:
            break;
        case AVAHI_ENTRY_GROUP_COLLISION:
            std::cerr << "Warning: Avahi service collision" << std::endl;
            break;
        case AVAHI_ENTRY_GROUP_FAILURE:
            std::cerr << "Warning: Avahi entry group failure" << std::endl;
            break;
        case AVAHI_ENTRY_GROUP_UNCOMMITED:
            break;
        case AVAHI_ENTRY_GROUP_REGISTERING:
            break;
    }
}

#elif defined(HAVE_BONJOUR)
#include <dns_sd.h>

struct MDNSContext {
    DNSServiceRef serviceRef = nullptr;
};

#else
struct MDNSContext {};
#endif


MDNSAdvertiser::MDNSAdvertiser(const std::string& instanceName, uint16_t port)
    : m_instanceName(instanceName), m_port(port), m_published(false) {
    m_ctx = std::make_unique<MDNSContext>();
}

MDNSAdvertiser::~MDNSAdvertiser() {
    unpublish();
}

bool MDNSAdvertiser::publish() {
#ifdef HAVE_AVAHI
    if (!m_ctx) {
        m_ctx = std::make_unique<MDNSContext>();
    }

    int error = 0;

    m_ctx->threadedPoll = avahi_threaded_poll_new();
    if (!m_ctx->threadedPoll) {
        std::cerr << "Warning: Failed to create Avahi threaded poll object" << std::endl;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }

    AvahiClientFlags flags = (AvahiClientFlags)0;
    m_ctx->client = avahi_client_new(avahi_threaded_poll_get(m_ctx->threadedPoll), flags, avahi_client_callback, nullptr, &error);
    if (!m_ctx->client) {
        std::cerr << "Warning: Failed to create Avahi client: " << avahi_strerror(error) << std::endl;
        avahi_threaded_poll_free(m_ctx->threadedPoll);
        m_ctx->threadedPoll = nullptr;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }

    if (avahi_threaded_poll_start(m_ctx->threadedPoll) < 0) {
        std::cerr << "Warning: Failed to start Avahi threaded poll" << std::endl;
        avahi_client_free(m_ctx->client);
        avahi_threaded_poll_free(m_ctx->threadedPoll);
        m_ctx->client = nullptr;
        m_ctx->threadedPoll = nullptr;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }

    m_ctx->group = avahi_entry_group_new(m_ctx->client, avahi_entry_group_callback, nullptr);
    if (!m_ctx->group) {
        std::cerr << "Warning: Failed to create Avahi entry group" << std::endl;
        avahi_client_free(m_ctx->client);
        avahi_threaded_poll_free(m_ctx->threadedPoll);
        m_ctx->client = nullptr;
        m_ctx->threadedPoll = nullptr;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }

    if (avahi_entry_group_add_service(
        m_ctx->group,
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
        avahi_entry_group_free(m_ctx->group);
        avahi_client_free(m_ctx->client);
        avahi_threaded_poll_free(m_ctx->threadedPoll);
        m_ctx->group = nullptr;
        m_ctx->client = nullptr;
        m_ctx->threadedPoll = nullptr;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }

    if (avahi_entry_group_commit(m_ctx->group) < 0) {
        std::cerr << "Warning: Failed to commit Avahi entry group" << std::endl;
        avahi_entry_group_free(m_ctx->group);
        avahi_client_free(m_ctx->client);
        avahi_threaded_poll_free(m_ctx->threadedPoll);
        m_ctx->group = nullptr;
        m_ctx->client = nullptr;
        m_ctx->threadedPoll = nullptr;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }

    m_published = true;
    std::cout << "Published mDNS service: " << m_instanceName << "._rendermatic._tcp on port " << m_port << std::endl;
    return true;

#elif defined(HAVE_BONJOUR)
    if (!m_ctx) {
        m_ctx = std::make_unique<MDNSContext>();
    }

    DNSServiceErrorType err = DNSServiceRegister(
        &m_ctx->serviceRef,
        0,                          // flags
        0,                          // interface index (all)
        m_instanceName.c_str(),     // service name
        "_rendermatic._tcp",        // service type
        nullptr,                    // domain (default)
        nullptr,                    // host (default)
        htons(m_port),              // port (network byte order)
        0,                          // TXT record length
        nullptr,                    // TXT record
        nullptr,                    // callback
        nullptr                     // context
    );

    if (err != kDNSServiceErr_NoError) {
        std::cerr << "Warning: Failed to register Bonjour service (error " << err << ")" << std::endl;
        std::cerr << "Warning: mDNS publication failed, continuing without discovery" << std::endl;
        return false;
    }

    m_published = true;
    std::cout << "Published mDNS service: " << m_instanceName << "._rendermatic._tcp on port " << m_port << std::endl;
    return true;

#else
    std::cerr << "Warning: mDNS support not compiled in, discovery unavailable" << std::endl;
    return false;
#endif
}

void MDNSAdvertiser::unpublish() {
#ifdef HAVE_AVAHI
    if (m_ctx) {
        if (m_ctx->group) {
            avahi_entry_group_free(m_ctx->group);
            m_ctx->group = nullptr;
        }
        if (m_ctx->threadedPoll) {
            avahi_threaded_poll_stop(m_ctx->threadedPoll);
        }
        if (m_ctx->client) {
            avahi_client_free(m_ctx->client);
            m_ctx->client = nullptr;
        }
        if (m_ctx->threadedPoll) {
            avahi_threaded_poll_free(m_ctx->threadedPoll);
            m_ctx->threadedPoll = nullptr;
        }
        m_published = false;
    }
#elif defined(HAVE_BONJOUR)
    if (m_ctx && m_ctx->serviceRef) {
        DNSServiceRefDeallocate(m_ctx->serviceRef);
        m_ctx->serviceRef = nullptr;
        m_published = false;
    }
#endif
}

bool MDNSAdvertiser::updateInstanceName(const std::string& newName) {
    m_instanceName = newName;

#ifdef HAVE_AVAHI
    if (m_published && m_ctx && m_ctx->client && m_ctx->group) {
        avahi_entry_group_reset(m_ctx->group);

        if (avahi_entry_group_add_service(
            m_ctx->group,
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
            return true;
        }

        if (avahi_entry_group_commit(m_ctx->group) < 0) {
            std::cerr << "Warning: Failed to commit updated Avahi entry group" << std::endl;
            return true;
        }

        std::cout << "Updated mDNS instance name to: " << newName << std::endl;
    }
#elif defined(HAVE_BONJOUR)
    if (m_published) {
        unpublish();
        publish();
        std::cout << "Updated mDNS instance name to: " << newName << std::endl;
    }
#endif

    return true;
}
