/* Maintainer-run parser fixtures; this file does not require Vita headers.
 * Intentionally not wired into the application build. See tests/README.md. */
#include <assert.h>
#include <string.h>
#include "moonlight/gamestream_xml.h"

int main(void)
{
    MoonlightApplication apps[2];
    MoonlightGameStreamResponse reply;
    int count;
    const char *list = "<root status_code='200'><App><ID>42</ID>"
        "<AppTitle>A &amp; B &#x1f3ae;</AppTitle></App></root>";
    assert(moonlight_xml_parse_applications(list, strlen(list), apps, 2, &count, &reply) == 0);
    assert(count == 1 && apps[0].id == 42);
    assert(strcmp(apps[0].name, "A & B \xf0\x9f\x8e\xae") == 0);
    const char *empty = "<root status_code='200'/>";
    assert(moonlight_xml_parse_applications(empty, strlen(empty), apps, 2, &count, &reply) == 0);
    assert(count == 0);
    assert(moonlight_xml_parse_applications(list, strlen(list), NULL, 0, &count, &reply) == 0);
    assert(count == 0 && reply.total_applications == 1);

    const char *bad_lists[] = {
        "<root status_code='200'><App><ID>1</ID></App><App><AppTitle>B</AppTitle></App></root>",
        "<root status_code='200'><App><ID>2147483648</ID><AppTitle>A</AppTitle></App></root>",
        "<root status_code='200'><App><ID>1x</ID><AppTitle>A</AppTitle></App></root>",
        "<root status_code='200'><App><ID>1</ID><AppTitle>A</AppTitle></App>",
        "<root><App><ID>1</ID><AppTitle>A</AppTitle></App></root>",
        "<!DOCTYPE root [<!ENTITY x 'A'>]><root status_code='200'/>",
        "<html status_code='200'/>"
    };
    for (unsigned int i = 0; i < sizeof(bad_lists) / sizeof(bad_lists[0]); ++i) {
        assert(moonlight_xml_parse_applications(bad_lists[i], strlen(bad_lists[i]),
            apps, 2, &count, &reply) != 0);
        assert(count == 0);
    }
    const char *denied = "<root status_code='401' status_message='Not &amp; paired'><gamesession>1</gamesession></root>";
    assert(moonlight_xml_parse_launch(denied, strlen(denied), 0, &reply) == MOONLIGHT_XML_SERVER_ERROR);
    assert(reply.status_code == 401 && strcmp(reply.status_message, "Not & paired") == 0);
    const char *launch = "<root status_code='200'><gamesession>1</gamesession>"
        "<sessionUrl0>rtspenc://192.0.2.1:48020</sessionUrl0></root>";
    assert(moonlight_xml_parse_launch(launch, strlen(launch), 0, &reply) == 0);
    assert(reply.session_url_present && strcmp(reply.session_url, "rtspenc://192.0.2.1:48020") == 0);
    const char *legacy = "<root status_code='200'><resume>1</resume></root>";
    assert(moonlight_xml_parse_launch(legacy, strlen(legacy), 1, &reply) == 0);
    assert(!reply.session_url_present);
    assert(moonlight_xml_parse_launch(legacy, strlen(legacy), 0, &reply) != 0);
    const char *bad_launches[] = {
        "<root status_code='200'><gamesession>0</gamesession></root>",
        "<root status_code='200'/>",
        "<root status_code='200'><gamesession>1</gamesession><sessionUrl0/></root>",
        "<root status_code='200'><gamesession>1</gamesession><sessionUrl0>http://host</sessionUrl0></root>",
        "<root status_code='200'><gamesession>1</gamesession><sessionUrl0>rtsp://host with space</sessionUrl0></root>",
        "<root status_code='200'><gamesession>1</gamesession><gamesession>1</gamesession></root>"
    };
    for (unsigned int i = 0; i < sizeof(bad_launches) / sizeof(bad_launches[0]); ++i)
        assert(moonlight_xml_parse_launch(bad_launches[i], strlen(bad_launches[i]), 0, &reply) != 0);
    return 0;
}
