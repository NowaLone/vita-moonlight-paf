#include <stddef.h>

#include "backend/legacy_moonlight_adapter.h"

#include "moonlight/api.h"
#include "moonlight/settings.h"
#include "backend/legacy_device_store.h"
#include "debug.h"
#include "legacy/legacy_host_probe.h"

LegacyMoonlightAdapter::LegacyMoonlightAdapter()
    : m_connection_state(MOONLIGHT_CONNECTION_DISCONNECTED),
      m_has_current_host(false),
      m_event_callback(NULL),
      m_event_userdata(NULL)
{
}

LegacyMoonlightAdapter::~LegacyMoonlightAdapter()
{
}

int LegacyMoonlightAdapter::Initialize()
{
    m_connection_state = MOONLIGHT_CONNECTION_DISCONNECTED;
    m_has_current_host = false;

    int result = moonlight_settings_init();
    if (result != 0) {
        return result;
    }

    result = legacy_device_store_init();
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

int LegacyMoonlightAdapter::ReadDiscovery(MoonlightDiscoverySnapshot *snapshot)
{
    return moonlight_api_read_discovery(snapshot);
}

int LegacyMoonlightAdapter::SearchHosts(uint32_t *generation)
{
    return moonlight_api_search_hosts(generation);
}

int LegacyMoonlightAdapter::StopHostSearch()
{
    return moonlight_api_stop_host_search();
}

bool LegacyMoonlightAdapter::IsHostSearchIdle()
{
    return moonlight_api_host_search_idle() != 0;
}

void LegacyMoonlightAdapter::WaitForHostSearchIdle()
{
    moonlight_api_wait_host_search_idle();
}

int LegacyMoonlightAdapter::AddHost(const char *address, uint16_t port, const char *name)
{
    return moonlight_api_add_host(address, port, name);
}

int LegacyMoonlightAdapter::DeleteHost(const MoonlightHost &host)
{
    return legacy_device_store_delete_host(&host);
}

int LegacyMoonlightAdapter::ProbeHost(const MoonlightHost &host, const MoonlightCancellation *cancel)
{
    return legacy_host_probe(&host, cancel);
}

int LegacyMoonlightAdapter::ConnectHost(const MoonlightHost &host)
{
    if (!host.internal[0]) {
        return -1;
    }

    vita_debug_log(
        "[LegacyAdapter] ConnectHost start host=%s:%u id=%s",
        host.internal,
        host.port,
        host.host_id);

    int result = moonlight_api_connect_host(&host);
    if (result != 0) {
        vita_debug_log(
            "[LegacyAdapter] ConnectHost failed result=%d",
            result);
        return result;
    }

    vita_debug_log("[LegacyAdapter] ConnectHost ready");

    /* CONNECTION_READY publishes the host/state before this API returns. */
    return 0;
}

int LegacyMoonlightAdapter::PreparePairing(char out_pin[5])
{
    if (!m_has_current_host || !out_pin) {
        return -1;
    }

    return moonlight_api_prepare_pairing(out_pin);
}

int LegacyMoonlightAdapter::PairCurrentHost(const char pin[5], const MoonlightCancellation *cancel)
{
    if (!m_has_current_host || !pin) {
        return -1;
    }

    /* PAIRING_FINISHED owns this transition, including callbacks before return. */
    return moonlight_api_pair_current_host(pin, cancel);
}

int LegacyMoonlightAdapter::GetApplications(MoonlightApplication *out, int capacity)
{
    return moonlight_api_get_applications(out, capacity);
}

int LegacyMoonlightAdapter::StartApplication(int application_id, int quit_application_id)
{
    vita_debug_log(
        "[LegacyAdapter] StartApplication app=%d has_host=%d state=%d",
        application_id,
        m_has_current_host ? 1 : 0,
        (int)m_connection_state);

    if (!m_has_current_host) {
        vita_debug_log("[LegacyAdapter] StartApplication rejected: no current host");
        return -1;
    }

    /* Success only schedules setup. ForwardEvent owns the STREAM_STARTED/FAILED
     * transition, which may already have arrived before the API call returns. */
    return moonlight_api_start_application(application_id, quit_application_id);
}

int LegacyMoonlightAdapter::StopApplication()
{
    if (!m_has_current_host) {
        return -1;
    }

    /* The session owner reports STREAM_STOPPED after transport cleanup. */
    return moonlight_api_stop_application();
}

int LegacyMoonlightAdapter::DisconnectHost()
{
    /* The API closes admission immediately; CONNECTION_CLOSED publishes the final
     * state only after stream/GameStream cleanup. Do not overwrite an early callback. */
    return moonlight_api_disconnect_host();
}

MoonlightConnectionState LegacyMoonlightAdapter::GetConnectionState() const
{
    return m_connection_state;
}

int LegacyMoonlightAdapter::GetStreamStats(MoonlightStreamStats *out)
{
    return moonlight_api_get_stream_stats(out);
}

int LegacyMoonlightAdapter::GetControllerLight(MoonlightControllerLight *out)
{
    return moonlight_api_get_controller_light(out);
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
        m_connection_state = moonlight_api_get_connection_state();
        m_has_current_host = true;
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
