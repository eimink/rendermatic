#include "mdns_advertiser.h"
#include <iostream>

#ifdef HAVE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/thread-watch.h>
#include <avahi-common/timeval.h>

struct MDNSContext {
    AvahiClient* client = nullptr;
    AvahiEntryGroup* group = nullptr;
    AvahiThreadedPoll* threadedPoll = nullptr;
    std::string instanceName;
    uint16_t port = 0;
    bool published = false;
    int retryCount = 0;
    static const int maxRetries = 5;
};

static void create_services(MDNSContext* ctx);

static void retry_callback(AvahiTimeout* t, void* userdata) {
    auto* ctx = static_cast<MDNSContext*>(userdata);
    (void)t;
    std::cout << "mDNS: Retry attempt " << ctx->retryCount << std::endl;
    create_services(ctx);
}

static void schedule_retry(MDNSContext* ctx) {
    if (ctx->retryCount >= MDNSContext::maxRetries) {
        std::cerr << "mDNS: Max retries reached, giving up" << std::endl;
        return;
    }
    ctx->retryCount++;
    struct timeval tv;
    avahi_elapse_time(&tv, 3000 * ctx->retryCount, 0);  // increasing delay
    const AvahiPoll* poll = avahi_threaded_poll_get(ctx->threadedPoll);
    poll->timeout_new(poll, &tv, retry_callback, ctx);
}

static void avahi_entry_group_callback(AvahiEntryGroup* g, AvahiEntryGroupState state, void* userdata) {
    (void)g;
    auto* ctx = static_cast<MDNSContext*>(userdata);
    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED:
            std::cout << "Published mDNS service: " << ctx->instanceName
                      << "._rendermatic._tcp on port " << ctx->port << std::endl;
            ctx->published = true;
            break;
        case AVAHI_ENTRY_GROUP_COLLISION:
            std::cerr << "Warning: Avahi service collision" << std::endl;
            break;
        case AVAHI_ENTRY_GROUP_FAILURE:
            std::cerr << "Warning: Avahi entry group failure" << std::endl;
            break;
        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
            break;
    }
}

static void avahi_client_callback(AvahiClient* c, AvahiClientState state, void* userdata) {
    auto* ctx = static_cast<MDNSContext*>(userdata);
    switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            std::cout << "mDNS: Avahi daemon ready, registering service" << std::endl;
            ctx->client = c;  // may fire before avahi_client_new returns
            create_services(ctx);
            break;
        case AVAHI_CLIENT_FAILURE:
            std::cerr << "mDNS: Avahi client failure: " << avahi_strerror(avahi_client_errno(c)) << std::endl;
            break;
        case AVAHI_CLIENT_S_COLLISION:
        case AVAHI_CLIENT_S_REGISTERING:
            std::cout << "mDNS: Avahi registering/collision, resetting group" << std::endl;
            if (ctx->group) {
                avahi_entry_group_reset(ctx->group);
            }
            break;
        case AVAHI_CLIENT_CONNECTING:
            std::cout << "mDNS: Waiting for Avahi daemon..." << std::endl;
            break;
    }
}

static void create_services(MDNSContext* ctx) {
    if (!ctx->client) {
        std::cerr << "mDNS: No client in create_services" << std::endl;
        return;
    }

    std::cout << "mDNS: Creating service '" << ctx->instanceName << "' on port " << ctx->port << std::endl;

    if (!ctx->group) {
        ctx->group = avahi_entry_group_new(ctx->client, avahi_entry_group_callback, ctx);
        if (!ctx->group) {
            std::cerr << "mDNS: Failed to create entry group: "
                      << avahi_strerror(avahi_client_errno(ctx->client)) << std::endl;
            schedule_retry(ctx);
            return;
        }
    }

    if (avahi_entry_group_is_empty(ctx->group)) {
        int ret = avahi_entry_group_add_service(
            ctx->group,
            AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, (AvahiPublishFlags)0,
            ctx->instanceName.c_str(), "_rendermatic._tcp",
            nullptr, nullptr, ctx->port, nullptr);
        if (ret < 0) {
            std::cerr << "mDNS: Failed to add service: " << avahi_strerror(ret) << std::endl;
            avahi_entry_group_reset(ctx->group);
            schedule_retry(ctx);
            return;
        }

        ret = avahi_entry_group_commit(ctx->group);
        if (ret < 0) {
            std::cerr << "mDNS: Failed to commit group: " << avahi_strerror(ret) << std::endl;
            avahi_entry_group_reset(ctx->group);
            schedule_retry(ctx);
            return;
        }
        ctx->published = true;
        ctx->retryCount = 0;
        std::cout << "mDNS: Service registered successfully" << std::endl;
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

    std::cout << "mDNS: Publishing service '" << m_instanceName << "' on port " << m_port << std::endl;
    m_ctx->instanceName = m_instanceName;
    m_ctx->port = m_port;

    m_ctx->threadedPoll = avahi_threaded_poll_new();
    if (!m_ctx->threadedPoll) {
        std::cerr << "Warning: Failed to create Avahi threaded poll, continuing without mDNS" << std::endl;
        return false;
    }

    // Start poll thread BEFORE creating client — the client callback fires
    // on the poll thread, so it must be running to process events
    if (avahi_threaded_poll_start(m_ctx->threadedPoll) < 0) {
        std::cerr << "Warning: Failed to start Avahi poll, continuing without mDNS" << std::endl;
        avahi_threaded_poll_free(m_ctx->threadedPoll);
        m_ctx->threadedPoll = nullptr;
        return false;
    }

    // Lock the poll thread while creating the client (thread safety)
    avahi_threaded_poll_lock(m_ctx->threadedPoll);

    int error = 0;
    m_ctx->client = avahi_client_new(
        avahi_threaded_poll_get(m_ctx->threadedPoll),
        AVAHI_CLIENT_NO_FAIL,
        avahi_client_callback, m_ctx.get(), &error);

    avahi_threaded_poll_unlock(m_ctx->threadedPoll);

    if (!m_ctx->client) {
        std::cerr << "Warning: Failed to create Avahi client: " << avahi_strerror(error) << std::endl;
        avahi_threaded_poll_stop(m_ctx->threadedPoll);
        avahi_threaded_poll_free(m_ctx->threadedPoll);
        m_ctx->threadedPoll = nullptr;
        return false;
    }

    // Service registration happens async in avahi_client_callback when daemon is ready
    m_published = true;
    return true;

#elif defined(HAVE_BONJOUR)
    if (!m_ctx) {
        m_ctx = std::make_unique<MDNSContext>();
    }

    DNSServiceErrorType err = DNSServiceRegister(
        &m_ctx->serviceRef,
        0, 0,
        m_instanceName.c_str(),
        "_rendermatic._tcp",
        nullptr, nullptr,
        htons(m_port),
        0, nullptr, nullptr, nullptr);

    if (err != kDNSServiceErr_NoError) {
        std::cerr << "Warning: Failed to register Bonjour service (error " << err << ")" << std::endl;
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
        if (m_ctx->threadedPoll) {
            avahi_threaded_poll_stop(m_ctx->threadedPoll);
        }
        if (m_ctx->group) {
            avahi_entry_group_free(m_ctx->group);
            m_ctx->group = nullptr;
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
        m_ctx->published = false;
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
    if (m_ctx) {
        // Lock the poll to safely modify state
        if (m_ctx->threadedPoll) {
            avahi_threaded_poll_lock(m_ctx->threadedPoll);
        }

        m_ctx->instanceName = newName;

        if (m_ctx->group) {
            avahi_entry_group_reset(m_ctx->group);
            create_services(m_ctx.get());
        }

        if (m_ctx->threadedPoll) {
            avahi_threaded_poll_unlock(m_ctx->threadedPoll);
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
