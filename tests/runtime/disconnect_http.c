#include <string.h>
#include <curl/curl.h>

#include "moonlight/http_disconnect_test.h"
#include "debug.h"

/* Only the stream owner arms/consumes this ID. Other HTTP verbs never inspect it. */
static int s_http_application_id = -1;

void disconnect_http_test_arm(int application_id)
{
    s_http_application_id = application_id;
}

static int http_test_debug(CURL *handle, curl_infotype type, char *data, size_t size, void *userdata)
{
    MoonlightHttpTestTransfer *transfer = (MoonlightHttpTestTransfer *)userdata;
    if (!transfer || handle != transfer->handle) return 0;
    /* Never print/copy curl's trace buffer: request URLs carry session keys. */
    if (type == CURLINFO_HEADER_OUT && !transfer->entered && !transfer->response_seen && data) {
        if (!transfer->matched_request) {
            if (size < 12 || (memcmp(data, "GET /launch?", 12) != 0 &&
                              memcmp(data, "GET /resume?", 12) != 0)) return 0;
            transfer->matched_request = 1;
        }
        if (size >= 4 && memcmp(data + size - 4, "\r\n\r\n", 4) == 0) {
            transfer->entered = 1;
            vita_debug_log("[DisconnectHttpTest] request headers sent; curl active before response app=%d",
                           transfer->application_id);
            disconnect_test_pending_checkpoint(transfer->application_id);
        }
    } else if (type == CURLINFO_HEADER_IN && transfer->matched_request && !transfer->response_seen) {
        transfer->response_seen = 1;
        vita_debug_log("[DisconnectHttpTest] response headers received");
    }
    return 0; /* Observation only: no curl abort or mutation from the callback. */
}

void disconnect_http_test_end(void *handle, MoonlightHttpTestTransfer *transfer, int result)
{
    if (!transfer->active) return;
    /* Disable verbosity before removing the sink, so raw request data cannot reach stderr. */
    curl_easy_setopt((CURL *)handle, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt((CURL *)handle, CURLOPT_DEBUGDATA, NULL);
    curl_easy_setopt((CURL *)handle, CURLOPT_DEBUGFUNCTION, NULL);
    vita_debug_log("[DisconnectHttpTest] transfer end result=%d checkpoint=%d response_headers=%d",
                   result, transfer->entered, transfer->response_seen);
    transfer->active = 0;
    s_http_application_id = -1;
}

int disconnect_http_test_begin(void *handle, const char *url, MoonlightHttpTestTransfer *transfer)
{
    memset(transfer, 0, sizeof(*transfer));
    if (!url || strncmp(url, "https://", 8) != 0 ||
        (!strstr(url, "/launch?") && !strstr(url, "/resume?"))) return CURLE_OK;
    if (s_http_application_id < 0 || !disconnect_test_pending_checkpoint_available()) return CURLE_OK;
    transfer->handle = handle;
    transfer->application_id = s_http_application_id;
    transfer->active = 1;
    CURLcode result = curl_easy_setopt((CURL *)handle, CURLOPT_DEBUGFUNCTION, http_test_debug);
    if (result == CURLE_OK) result = curl_easy_setopt((CURL *)handle, CURLOPT_DEBUGDATA, transfer);
    if (result == CURLE_OK) result = curl_easy_setopt((CURL *)handle, CURLOPT_VERBOSE, 1L);
    if (result != CURLE_OK) disconnect_http_test_end(handle, transfer, (int)result);
    return (int)result;
}
