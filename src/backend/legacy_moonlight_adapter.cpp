#include <stddef.h>

#include "backend/legacy_moonlight_adapter.h"

#include "moonlight/api.h"
#include "moonlight/settings.h"
#include "backend/legacy_device_store.h"

LegacyMoonlightAdapter::LegacyMoonlightAdapter()
    : m_connection_state(MOONLIGHT_CONNECTION_DISCONNECTED),
      m_has_current_host(false),
      m_event_callback(NULL),
      m_event_userdata(NULL)
{
    m_current_host = MoonlightHost();
}

LegacyMoonlightAdapter::~LegacyMoonlightAdapter()
{
}

int LegacyMoonlightAdapter::Initialize()
{
    m_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    m_has_current_host = false;

    int result = legacy_device_store_init();
    if (result != 0) {
        return result;
    }

    return moonlight_api_init();
}

void LegacyMoonlightAdapter::Shutdown()
{
    SetEventCallback(NULL, NULL);
    moonlight_settings_shutdown();
    moonlight_api_shutdown();

    m_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    m_has_current_host = false;
}

int LegacyMoonlightAdapter::OpenSettings()
{
    if (moonlight_settings_init() != 0) {
        return -1;
    }
    return moonlight_settings_open();
}

int LegacyMoonlightAdapter::GetSettings(MoonlightSettings *out)
{
    if (moonlight_settings_init() != 0) {
        return -1;
    }
    return moonlight_settings_get_all(out);
}

int LegacyMoonlightAdapter::GetSettingValue(MoonlightSettingKey key, int *out_value)
{
    if (moonlight_settings_init() != 0) {
        return -1;
    }
    return moonlight_settings_get_value(key, out_value);
}

int LegacyMoonlightAdapter::SetSettingValue(MoonlightSettingKey key, int value)
{
    if (moonlight_settings_init() != 0) {
        return -1;
    }
    return moonlight_settings_set_value(key, value);
}

int LegacyMoonlightAdapter::SetEventCallback(MoonlightEventCallback callback, void *userdata)
{
    int result = moonlight_api_set_event_callback(OnLegacyEvent, this);
    if (result != 0) {
        return result;
    }

    result = moonlight_settings_set_event_callback(OnLegacyEvent, this);
    if (result != 0) {
        moonlight_api_set_event_callback(NULL, NULL);
        return result;
    }

    m_event_callback = callback;
    m_event_userdata = userdata;

    if (callback == NULL) {
        moonlight_api_set_event_callback(NULL, NULL);
        moonlight_settings_set_event_callback(NULL, NULL);
    }

    return 0;
}

int LegacyMoonlightAdapter::GetHosts(MoonlightHost *out, int capacity)
{
    return legacy_device_store_get_hosts(out, capacity);
}

int LegacyMoonlightAdapter::GetDiscoveredHosts(MoonlightHost *out, int capacity)
{
    return moonlight_api_get_discovered_hosts(out, capacity);
}

int LegacyMoonlightAdapter::SearchHosts()
{
    return moonlight_api_search_hosts();
}

int LegacyMoonlightAdapter::StopHostSearch()
{
    return moonlight_api_stop_host_search();
}

int LegacyMoonlightAdapter::AddHost(const char *address, uint16_t port, const char *name)
{
    return moonlight_api_add_host(address, port, name);
}

int LegacyMoonlightAdapter::MarkHostPaired(const MoonlightHost &host)
{
    return legacy_device_store_mark_paired(&host);
}

int LegacyMoonlightAdapter::ConnectHost(const MoonlightHost &host)
{
    if (!host.internal[0]) {
        return -1;
    }

    int result = moonlight_api_connect_host(&host);
    if (result != 0) {
        return result;
    }

    m_current_host = host;
    m_has_current_host = true;
    m_connection_state = moonlight_api_get_connection_state();
    return 0;
}

int LegacyMoonlightAdapter::PreparePairing(char out_pin[5])
{
    if (!m_has_current_host || !out_pin) {
        return -1;
    }

    return moonlight_api_prepare_pairing(out_pin);
}

int LegacyMoonlightAdapter::PairCurrentHost(const char pin[5])
{
    if (!m_has_current_host || !pin) {
        return -1;
    }

    int result = moonlight_api_pair_current_host(pin);
    if (result != 0) {
        return result;
    }

    m_connection_state = MOONLIGHT_CONNECTION_PAIRED;
    return 0;
}

int LegacyMoonlightAdapter::GetApplications(MoonlightApplication *out, int capacity)
{
    return moonlight_api_get_applications(out, capacity);
}

int LegacyMoonlightAdapter::StartApplication(int application_id)
{
    if (!m_has_current_host) {
        return -1;
    }

    int result = moonlight_api_start_application(application_id);
    if (result != 0) {
        return result;
    }

    m_connection_state = MOONLIGHT_CONNECTION_STREAMING;
    return 0;
}

int LegacyMoonlightAdapter::StopApplication()
{
    if (!m_has_current_host) {
        return -1;
    }

    int result = moonlight_api_stop_application();
    if (result != 0) {
        return result;
    }

    m_connection_state = MOONLIGHT_CONNECTION_PAIRED;
    return 0;
}

int LegacyMoonlightAdapter::DisconnectHost()
{
    int result = moonlight_api_disconnect_host();
    if (result != 0) {
        return result;
    }

    m_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    m_has_current_host = false;
    return 0;
}

MoonlightConnectionState LegacyMoonlightAdapter::GetConnectionState() const
{
    return m_connection_state;
}

void LegacyMoonlightAdapter::OnLegacyEvent(const MoonlightEvent *event, void *userdata)
{
    LegacyMoonlightAdapter *adapter = (LegacyMoonlightAdapter *)userdata;
    if (!adapter || !event) {
        return;
    }

    adapter->ForwardEvent(event);
}

void LegacyMoonlightAdapter::ForwardEvent(const MoonlightEvent *event)
{
    switch (event->type) {
    case MOONLIGHT_EVENT_CONNECTION_READY:
        if (m_connection_state == MOONLIGHT_CONNECTION_DISCONNECTED) {
            m_connection_state = MOONLIGHT_CONNECTION_READY;
        }
        break;

    case MOONLIGHT_EVENT_PAIRING_FINISHED:
        m_connection_state = MOONLIGHT_CONNECTION_PAIRED;
        break;

    case MOONLIGHT_EVENT_STREAM_STARTED:
        m_connection_state = MOONLIGHT_CONNECTION_STREAMING;
        break;

    case MOONLIGHT_EVENT_STREAM_STOPPED:
        m_connection_state = MOONLIGHT_CONNECTION_PAIRED;
        break;

    case MOONLIGHT_EVENT_CONNECTION_CLOSED:
    case MOONLIGHT_EVENT_CONNECTION_FAILED:
        m_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
        m_has_current_host = false;
        break;

    case MOONLIGHT_EVENT_PAIRING_FAILED:
        if (m_has_current_host) {
            m_connection_state = MOONLIGHT_CONNECTION_READY;
        }
        break;

    case MOONLIGHT_EVENT_STREAM_FAILED:
        if (m_has_current_host) {
            m_connection_state = MOONLIGHT_CONNECTION_PAIRED;
        }
        break;

    default:
        break;
    }

    if (m_event_callback) {
        m_event_callback(event, m_event_userdata);
    }
}
