#pragma once

/* Test-only transfer scope. Does not contain request/response bytes or key material. */
typedef struct MoonlightHttpTestTransfer {
    void *handle;
    int application_id;
    int active;
    int matched_request;
    int entered;
    int response_seen;
} MoonlightHttpTestTransfer;

#ifdef __cplusplus
extern "C" {
#endif

void disconnect_http_test_arm(int application_id);
int disconnect_http_test_begin(void *handle, const char *url, MoonlightHttpTestTransfer *transfer);
void disconnect_http_test_end(void *handle, MoonlightHttpTestTransfer *transfer, int result);
int disconnect_test_pending_checkpoint_available(void);
void disconnect_test_pending_checkpoint(int application_id);

#ifdef __cplusplus
}
#endif
