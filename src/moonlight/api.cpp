extern "C" int moonlight_launch_request(
    void *curl,
    const char *address,
    unsigned short https_port,
    const char *unique_path,
    int app_id,
    char *session_url,
    size_t session_url_size);

int moonlight_api_start_application(int application_id)
{
    char session_url[256];
    char unique_path[512];
    const char *name;
    int written;
    int result;

    if (!s_has_current_host || !s_game_stream_initialized || !s_server.curl ||
        s_connection_state != MOONLIGHT_CONNECTION_PAIRED) {
        return -1;
    }

    name = s_current_host.name[0] ? s_current_host.name : s_current_host.internal;
    written = snprintf(unique_path, sizeof(unique_path), "%s%s/uniqueid.dat", config.key_dir, name);
    if (written < 0 || (size_t)written >= sizeof(unique_path)) {
        return -1;
    }

    session_url[0] = '\0';
    result = moonlight_launch_request(
        s_server.curl,
        s_server.address,
        s_server.https_port,
        unique_path,
        application_id,
        session_url,
        sizeof(session_url));
    if (result != 0) {
        vita_debug_log("[GameStream] launch failed for app %d: %d", application_id, result);
        emit(MOONLIGHT_EVENT_STREAM_FAILED, result, s_current_host.id, application_id, s_current_host.internal);
        return result;
    }

    s_connection_state = MOONLIGHT_CONNECTION_STREAMING;
    vita_debug_log("[GameStream] launch accepted, session %s", session_url);
    emit(MOONLIGHT_EVENT_STREAM_STARTED, 0, s_current_host.id, application_id, session_url[0] ? session_url : s_current_host.internal);
    return 0;
}
