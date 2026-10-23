#ifndef MOONLIGHT_GAMESTREAM_XML_H
#define MOONLIGHT_GAMESTREAM_XML_H

#include <stddef.h>
#include "moonlight/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MOONLIGHT_XML_OK = 0,
    MOONLIGHT_XML_INVALID = -1,
    MOONLIGHT_XML_SERVER_ERROR = -2,
    MOONLIGHT_XML_OUT_OF_MEMORY = -3
};

typedef struct MoonlightGameStreamResponse {
    int status_code;
    char status_message[256];
    char session_url[256];
    int session_url_present;
    int total_applications;
} MoonlightGameStreamResponse;

/* Outputs are usable only on OK. Excess apps are counted but not stored. */
int moonlight_xml_parse_applications(const char *data, size_t length,
    MoonlightApplication *apps, int capacity, int *count,
    MoonlightGameStreamResponse *response);

/* A valid legacy response may omit sessionUrl0; an empty/invalid URL is rejected. */
int moonlight_xml_parse_launch(const char *data, size_t length, int resume,
    MoonlightGameStreamResponse *response);

/* Remote Quit succeeds only with a valid root status 200 and one cancel=1 field. */
int moonlight_xml_parse_cancel(const char *data, size_t length,
    MoonlightGameStreamResponse *response);

#ifdef __cplusplus
}
#endif

#endif
