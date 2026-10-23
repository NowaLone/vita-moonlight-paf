#include "moonlight/gamestream_xml.h"

#include <expat.h>
#include <limits.h>
#include <string.h>

enum { FIELD_NONE, FIELD_TITLE, FIELD_ID, FIELD_SESSION, FIELD_URL };

typedef struct ParseContext {
    XML_Parser parser;
    MoonlightGameStreamResponse *response;
    MoonlightApplication *apps;
    MoonlightApplication app;
    int capacity;
    int count;
    int list_mode;
    const char *result_field;
    int depth;
    int invalid;
    int root_seen;
    int app_open;
    int have_id;
    int have_title;
    int have_session;
    int field;
    int field_depth;
    size_t field_length;
    char text[256];
} ParseContext;

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int parse_number(const char *text, int *value)
{
    int result = 0;
    int digits = 0;
    while (is_space(*text)) ++text;
    while (*text >= '0' && *text <= '9') {
        int digit = *text++ - '0';
        if (result > (INT_MAX - digit) / 10) return 0;
        result = result * 10 + digit;
        ++digits;
    }
    while (is_space(*text)) ++text;
    if (!digits || *text) return 0;
    *value = result;
    return 1;
}

static void reject(ParseContext *ctx)
{
    ctx->invalid = 1;
    XML_StopParser(ctx->parser, XML_FALSE);
}

static void begin_field(ParseContext *ctx, int field)
{
    ctx->field = field;
    ctx->field_depth = ctx->depth;
    ctx->field_length = 0;
    ctx->text[0] = '\0';
}

static void XMLCALL start_element(void *userdata, const char *name, const char **attrs)
{
    ParseContext *ctx = (ParseContext *)userdata;
    if (ctx->invalid) return;
    ++ctx->depth;
    if (ctx->depth == 1) {
        if (ctx->root_seen || strcmp(name, "root") != 0) {
            reject(ctx);
            return;
        }
        ctx->root_seen = 1;
        for (int i = 0; attrs[i]; i += 2) {
            if (strcmp(attrs[i], "status_code") == 0) {
                if (!parse_number(attrs[i + 1], &ctx->response->status_code)) reject(ctx);
            } else if (strcmp(attrs[i], "status_message") == 0) {
                strncpy(ctx->response->status_message, attrs[i + 1],
                        sizeof(ctx->response->status_message) - 1);
            }
        }
        if (!ctx->response->status_code) reject(ctx);
        return;
    }
    if (ctx->response->status_code != 200) return;
    if (ctx->field != FIELD_NONE) {
        reject(ctx); /* Recognized scalar fields may not contain nested markup. */
        return;
    }
    if (ctx->list_mode) {
        if (strcmp(name, "App") == 0) {
            if (ctx->depth != 2 || ctx->app_open) {
                reject(ctx);
                return;
            }
            memset(&ctx->app, 0, sizeof(ctx->app));
            ctx->have_id = ctx->have_title = 0;
            ctx->app_open = 1;
        } else if (ctx->app_open && ctx->depth == 3) {
            if (strcmp(name, "ID") == 0) {
                if (ctx->have_id) reject(ctx);
                else begin_field(ctx, FIELD_ID);
            } else if (strcmp(name, "AppTitle") == 0) {
                if (ctx->have_title) reject(ctx);
                else begin_field(ctx, FIELD_TITLE);
            }
        }
    } else if (ctx->depth == 2) {
        if (strcmp(name, ctx->result_field) == 0) {
            if (ctx->have_session) reject(ctx);
            else begin_field(ctx, FIELD_SESSION);
        } else if (strcmp(name, "sessionUrl0") == 0) {
            if (ctx->response->session_url_present) reject(ctx);
            else begin_field(ctx, FIELD_URL);
        }
    }
}

static void XMLCALL character_data(void *userdata, const XML_Char *data, int length)
{
    ParseContext *ctx = (ParseContext *)userdata;
    if (ctx->invalid || ctx->field == FIELD_NONE) return;
    size_t available = sizeof(ctx->text) - 1 - ctx->field_length;
    size_t copy = (size_t)length < available ? (size_t)length : available;
    if ((size_t)length > available && ctx->field != FIELD_TITLE) {
        reject(ctx);
        return;
    }
    memcpy(ctx->text + ctx->field_length, data, copy);
    ctx->field_length += copy;
    ctx->text[ctx->field_length] = '\0';
}

/* Expat validates UTF-8; only a clipped last code point needs removing. */
static void finish_title(ParseContext *ctx)
{
    size_t length = ctx->field_length;
    if (length) {
        size_t start = length - 1;
        while (start && ((unsigned char)ctx->text[start] & 0xc0) == 0x80) --start;
        unsigned char lead = (unsigned char)ctx->text[start];
        size_t width = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        if (start + width > length) ctx->text[start] = '\0';
    }
    memcpy(ctx->app.name, ctx->text, sizeof(ctx->app.name));
    ctx->have_title = 1;
}

static void XMLCALL end_element(void *userdata, const char *name)
{
    ParseContext *ctx = (ParseContext *)userdata;
    if (ctx->invalid) return;
    if (ctx->field != FIELD_NONE && ctx->depth == ctx->field_depth) {
        int number = 0;
        switch (ctx->field) {
        case FIELD_TITLE:
            finish_title(ctx);
            break;
        case FIELD_ID:
            if (!parse_number(ctx->text, &ctx->app.id)) reject(ctx);
            ctx->have_id = 1;
            break;
        case FIELD_SESSION:
            if (!parse_number(ctx->text, &number) || number != 1) reject(ctx);
            ctx->have_session = 1;
            break;
        case FIELD_URL:
            if ((!strncmp(ctx->text, "rtsp://", 7) && ctx->text[7]) ||
                (!strncmp(ctx->text, "rtspenc://", 10) && ctx->text[10])) {
                if (strpbrk(ctx->text, " \t\r\n")) reject(ctx);
                else memcpy(ctx->response->session_url, ctx->text, sizeof(ctx->text));
            } else reject(ctx);
            ctx->response->session_url_present = 1;
            break;
        }
        ctx->field = FIELD_NONE;
    }
    if (ctx->list_mode && ctx->depth == 2 && strcmp(name, "App") == 0 &&
        ctx->response->status_code == 200 && !ctx->invalid) {
        if (!ctx->have_id || !ctx->have_title ||
            ctx->response->total_applications == INT_MAX) {
            reject(ctx);
            return;
        }
        if (ctx->count < ctx->capacity) ctx->apps[ctx->count++] = ctx->app;
        ++ctx->response->total_applications;
        ctx->app_open = 0;
    }
    --ctx->depth;
}

static void XMLCALL reject_doctype(void *userdata, const XML_Char *name,
    const XML_Char *system_id, const XML_Char *public_id, int has_subset)
{
    (void)name; (void)system_id; (void)public_id; (void)has_subset;
    reject((ParseContext *)userdata);
}

static int parse_response(const char *data, size_t length, ParseContext *ctx)
{
    if (!ctx->response) return MOONLIGHT_XML_INVALID;
    memset(ctx->response, 0, sizeof(*ctx->response));
    if (!data || !length || length > INT_MAX) return MOONLIGHT_XML_INVALID;
    ctx->parser = XML_ParserCreate("UTF-8");
    if (!ctx->parser) return MOONLIGHT_XML_OUT_OF_MEMORY;
    XML_SetUserData(ctx->parser, ctx);
    XML_SetElementHandler(ctx->parser, start_element, end_element);
    XML_SetCharacterDataHandler(ctx->parser, character_data);
    XML_SetStartDoctypeDeclHandler(ctx->parser, reject_doctype);
    int parsed = XML_Parse(ctx->parser, data, (int)length, XML_TRUE);
    XML_ParserFree(ctx->parser);
    if (!parsed || ctx->invalid || !ctx->root_seen || ctx->depth) return MOONLIGHT_XML_INVALID;
    if (ctx->response->status_code != 200) return MOONLIGHT_XML_SERVER_ERROR;
    if (!ctx->list_mode && !ctx->have_session) return MOONLIGHT_XML_INVALID;
    return MOONLIGHT_XML_OK;
}

int moonlight_xml_parse_applications(const char *data, size_t length,
    MoonlightApplication *apps, int capacity, int *count,
    MoonlightGameStreamResponse *response)
{
    if (!count) return MOONLIGHT_XML_INVALID;
    *count = 0;
    if (capacity < 0 || (capacity && !apps)) return MOONLIGHT_XML_INVALID;
    ParseContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.response = response;
    ctx.apps = apps;
    ctx.capacity = capacity;
    ctx.list_mode = 1;
    int result = parse_response(data, length, &ctx);
    if (result == MOONLIGHT_XML_OK) *count = ctx.count;
    return result;
}

int moonlight_xml_parse_launch(const char *data, size_t length, int resume,
    MoonlightGameStreamResponse *response)
{
    ParseContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.response = response;
    ctx.result_field = resume ? "resume" : "gamesession";
    return parse_response(data, length, &ctx);
}

int moonlight_xml_parse_cancel(const char *data, size_t length,
    MoonlightGameStreamResponse *response)
{
    ParseContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.response = response;
    ctx.result_field = "cancel";
    return parse_response(data, length, &ctx);
}
