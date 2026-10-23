/*
 * This file contains the GameStream pairing transport derived from the
 * libgamestream implementation used by xyzz/vita-moonlight.
 *
 * Copyright (C) 2015-2017 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "legacy_gamestream.h"
#include "moonlight/gamestream_xml.h"
#ifdef MOONLIGHT_HTTP_DISCONNECT_TEST
#include "moonlight/http_disconnect_test.h"
#endif

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/rng.h>
#include <psp2/kernel/threadmgr.h>

#include <curl/curl.h>
#include <expat.h>

#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "debug.h"

#define UNIQUE_FILE_NAME "uniqueid.dat"
#define P12_FILE_NAME "client.p12"
#define CERTIFICATE_FILE_NAME "client.pem"
#define KEY_FILE_NAME "key.pem"

#define UNIQUEID_BYTES 8
#define UNIQUEID_CHARS (UNIQUEID_BYTES * 2)
#define UUID_STRLEN 37
#define SIGNATURE_LEN 256
#define PATH_MAX_LOCAL 1024
#define STATUS_OK 200

#define MIN_SUPPORTED_GFE_VERSION 3
#define MAX_SUPPORTED_GFE_VERSION 7

static char s_error[256];

static X509 *s_cert = NULL;
static EVP_PKEY *s_private_key = NULL;
static char s_cert_hex[8192];
static char s_unique_id[UNIQUEID_CHARS + 1];

static bool s_curl_initialized = false;
static CURLcode s_last_http_result = CURLE_OK;
static pthread_mutex_t s_http_mutex = PTHREAD_MUTEX_INITIALIZER;
/* Pairing uses the client keys between requests and temporarily owns a candidate server pin. */
static pthread_mutex_t s_pair_mutex = PTHREAD_MUTEX_INITIALIZER;

#define GAMESTREAM_WORKER_STACK (256 * 1024)

typedef struct WorkerCall {
    int (*function)(void *);
    void *argument;
    int result;
} WorkerCall;

static void *worker_entry(void *argument)
{
    WorkerCall *call = (WorkerCall *)argument;

    call->result = call->function(call->argument);
    return NULL;
}

/* OpenSSL keygen and libcurl keep large frames. PAF UI/job stacks are too small. */
static int run_on_worker(int (*function)(void *), void *argument)
{
    pthread_t thread;
    pthread_attr_t attr;
    WorkerCall call;
    int create_result;

    call.function = function;
    call.argument = argument;
    call.result = LEGACY_GAMESTREAM_FAILED;

    if (pthread_attr_init(&attr) != 0) {
        return function(argument);
    }

    if (pthread_attr_setstacksize(&attr, GAMESTREAM_WORKER_STACK) != 0) {
        pthread_attr_destroy(&attr);
        return function(argument);
    }

    create_result = pthread_create(&thread, &attr, worker_entry, &call);
    pthread_attr_destroy(&attr);
    if (create_result != 0) {
        vita_debug_log("[GameStream] worker create failed: %d", create_result);
        return function(argument);
    }

    pthread_join(thread, NULL);
    return call.result;
}


typedef struct HttpBuffer {
    char *memory;
    size_t size;
    bool allocation_failed;
} HttpBuffer;

typedef struct XmlFindContext {
    const char *target;
    char *output;
    size_t capacity;
    size_t length;
    int depth;
    bool found;
} XmlFindContext;

typedef struct XmlStatusContext {
    int status;
    char message[256];
} XmlStatusContext;

static void set_error(const char *message)
{
    if (!message) {
        message = "GameStream error";
    }

    strncpy(s_error, message, sizeof(s_error) - 1);
    s_error[sizeof(s_error) - 1] = '\0';
}

const char *legacy_gamestream_error(void)
{
    return s_error;
}

int legacy_gamestream_last_http_error(void)
{
    return (int)s_last_http_result;
}

static void bytes_to_hex(const unsigned char *input, char *output, size_t length)
{
    size_t i;

    for (i = 0; i < length; ++i) {
        sprintf(output + i * 2, "%02x", input[i]);
    }

    output[length * 2] = '\0';
}

static bool is_existing_directory_error(int result)
{
    return result == 0x80010011;
}

static int ensure_directory(const char *path)
{
    int result;

    if (!path || !path[0]) {
        return -1;
    }

    result = sceIoMkdir(path, 0777);
    if (result < 0 && !is_existing_directory_error(result)) {
        return result;
    }

    return 0;
}

static int make_directory_tree(const char *directory)
{
    char *buffer;
    char *cursor;

    if (!directory || !directory[0]) {
        return -1;
    }

    buffer = (char *)malloc(PATH_MAX_LOCAL);
    if (!buffer) {
        return -1;
    }

    strncpy(buffer, directory, PATH_MAX_LOCAL - 1);
    buffer[PATH_MAX_LOCAL - 1] = '\0';

    cursor = buffer;

    while (*cursor != '\0') {
        char saved;

        while (*cursor != '\0' && *cursor != '/') {
            ++cursor;
        }

        saved = *cursor;
        *cursor = '\0';

        if (buffer[0] != '\0' && ensure_directory(buffer) < 0) {
            free(buffer);
            return -1;
        }

        *cursor = saved;

        if (*cursor != '\0') {
            ++cursor;
        }
    }

    free(buffer);
    return 0;
}

static int load_unique_id(const char *client_directory)
{
    char path[PATH_MAX_LOCAL];
    FILE *file;
    size_t read_count;

    snprintf(path, sizeof(path), "%s/%s", client_directory, UNIQUE_FILE_NAME);

    file = fopen(path, "rb");
    if (file) {
        read_count = fread(s_unique_id, 1, UNIQUEID_CHARS, file);
        fclose(file);

        if (read_count == UNIQUEID_CHARS) {
            s_unique_id[UNIQUEID_CHARS] = '\0';
            return LEGACY_GAMESTREAM_OK;
        }
    }

    {
        unsigned char random_bytes[UNIQUEID_BYTES];

        if (sceKernelGetRandomNumber(random_bytes, sizeof(random_bytes)) < 0) {
            set_error("Unable to generate a unique client id");
            return LEGACY_GAMESTREAM_FAILED;
        }

        bytes_to_hex(random_bytes, s_unique_id, sizeof(random_bytes));
    }

    file = fopen(path, "wb");
    if (!file) {
        set_error("Unable to create uniqueid.dat");
        return LEGACY_GAMESTREAM_FAILED;
    }

    read_count = fwrite(s_unique_id, 1, UNIQUEID_CHARS, file);
    fclose(file);

    return read_count == UNIQUEID_CHARS
        ? LEGACY_GAMESTREAM_OK
        : LEGACY_GAMESTREAM_FAILED;
}

static void generate_uuid(char output[UUID_STRLEN])
{
    unsigned char uuid[16];

    memset(uuid, 0, sizeof(uuid));
    sceKernelGetRandomNumber(uuid, sizeof(uuid));

    uuid[6] = (unsigned char)((uuid[6] & 0x0f) | 0x40);
    uuid[8] = (unsigned char)((uuid[8] & 0x3f) | 0x80);

    snprintf(
        output,
        UUID_STRLEN,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3],
        uuid[4], uuid[5], uuid[6], uuid[7],
        uuid[8], uuid[9], uuid[10], uuid[11],
        uuid[12], uuid[13], uuid[14], uuid[15]);
}

static int save_certificate_files(
    const char *certificate_path,
    const char *p12_path,
    const char *key_path)
{
    EVP_PKEY_CTX *key_context = NULL;
    EVP_PKEY *key = NULL;
    X509 *certificate = NULL;
    PKCS12 *p12 = NULL;
    FILE *certificate_file = NULL;
    FILE *key_file = NULL;
    FILE *p12_file = NULL;
    int result = LEGACY_GAMESTREAM_FAILED;

    key_context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!key_context ||
        EVP_PKEY_keygen_init(key_context) != 1 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(key_context, 2048) != 1 ||
        EVP_PKEY_keygen(key_context, &key) != 1) {
        set_error("Unable to generate client key");
        goto cleanup;
    }

    certificate = X509_new();
    if (!certificate) {
        set_error("Unable to allocate client certificate");
        goto cleanup;
    }

    if (X509_set_version(certificate, 2) != 1 ||
        ASN1_INTEGER_set(X509_get_serialNumber(certificate), 0) != 1) {
        set_error("Unable to initialize client certificate");
        goto cleanup;
    }

#if OPENSSL_VERSION_NUMBER < 0x10100000L
    if (!X509_gmtime_adj(X509_get_notBefore(certificate), 0) ||
        !X509_gmtime_adj(X509_get_notAfter(certificate), 60 * 60 * 24 * 365 * 10)) {
        set_error("Unable to set client certificate lifetime");
        goto cleanup;
    }
#else
    {
        ASN1_TIME *before = ASN1_STRING_dup(X509_get0_notBefore(certificate));
        ASN1_TIME *after = ASN1_STRING_dup(X509_get0_notAfter(certificate));

        if (!before || !after ||
            !X509_gmtime_adj(before, 0) ||
            !X509_gmtime_adj(after, 60 * 60 * 24 * 365 * 10) ||
            !X509_set1_notBefore(certificate, before) ||
            !X509_set1_notAfter(certificate, after)) {
            ASN1_STRING_free(before);
            ASN1_STRING_free(after);
            set_error("Unable to set client certificate lifetime");
            goto cleanup;
        }

        ASN1_STRING_free(before);
        ASN1_STRING_free(after);
    }
#endif

    if (!X509_set_pubkey(certificate, key)) {
        set_error("Unable to set client certificate key");
        goto cleanup;
    }

    {
        X509_NAME *name = X509_get_subject_name(certificate);

        if (!name ||
            !X509_NAME_add_entry_by_txt(
                name,
                "CN",
                MBSTRING_ASC,
                (unsigned char *)"NVIDIA GameStream Client",
                -1,
                -1,
                0) ||
            !X509_set_issuer_name(certificate, name) ||
            !X509_sign(certificate, key, EVP_sha256())) {
            set_error("Unable to sign client certificate");
            goto cleanup;
        }
    }

    p12 = PKCS12_create(
        "limelight",
        "GameStream",
        key,
        certificate,
        NULL,
        0,
        0,
        0,
        0,
        0);

    if (!p12) {
        set_error("Unable to create client.p12");
        goto cleanup;
    }

    certificate_file = fopen(certificate_path, "w");
    key_file = fopen(key_path, "w");
    p12_file = fopen(p12_path, "wb");

    if (!certificate_file || !key_file || !p12_file) {
        set_error("Unable to open client credential files");
        goto cleanup;
    }

    if (!PEM_write_X509(certificate_file, certificate) ||
        !PEM_write_PrivateKey(key_file, key, NULL, NULL, 0, NULL, NULL) ||
        !i2d_PKCS12_fp(p12_file, p12)) {
        set_error("Unable to write client credential files");
        goto cleanup;
    }

    result = LEGACY_GAMESTREAM_OK;

cleanup:
    if (certificate_file) fclose(certificate_file);
    if (key_file) fclose(key_file);
    if (p12_file) fclose(p12_file);
    PKCS12_free(p12);
    X509_free(certificate);
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(key_context);

    return result;
}

static int load_certificate(const char *client_directory)
{
    char *certificate_path;
    char *key_path;
    char *p12_path;
    FILE *file;
    size_t length;
    X509 *certificate;
    EVP_PKEY *private_key;
    int result = LEGACY_GAMESTREAM_FAILED;

    certificate_path = (char *)malloc(PATH_MAX_LOCAL);
    key_path = (char *)malloc(PATH_MAX_LOCAL);
    p12_path = (char *)malloc(PATH_MAX_LOCAL);
    if (!certificate_path || !key_path || !p12_path) {
        free(certificate_path);
        free(key_path);
        free(p12_path);
        set_error("Unable to allocate certificate paths");
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }

    snprintf(certificate_path, PATH_MAX_LOCAL, "%s/%s", client_directory, CERTIFICATE_FILE_NAME);
    snprintf(key_path, PATH_MAX_LOCAL, "%s/%s", client_directory, KEY_FILE_NAME);
    snprintf(p12_path, PATH_MAX_LOCAL, "%s/%s", client_directory, P12_FILE_NAME);

    file = fopen(certificate_path, "rb");
    if (!file) {
        if (save_certificate_files(certificate_path, p12_path, key_path) != LEGACY_GAMESTREAM_OK) {
            free(certificate_path);
            free(key_path);
            free(p12_path);
            return LEGACY_GAMESTREAM_FAILED;
        }
        file = fopen(certificate_path, "rb");
    }

    if (!file) {
        free(certificate_path);
        free(key_path);
        free(p12_path);
        set_error("Unable to open client.pem");
        return LEGACY_GAMESTREAM_FAILED;
    }

    certificate = PEM_read_X509(file, NULL, NULL, NULL);
    rewind(file);

    length = fread(s_cert_hex, 1, sizeof(s_cert_hex) - 1, file);
    fclose(file);

    if (!certificate || length == 0 || length * 2 >= sizeof(s_cert_hex)) {
        X509_free(certificate);
        free(certificate_path);
        free(key_path);
        free(p12_path);
        set_error("Unable to load client certificate");
        return LEGACY_GAMESTREAM_FAILED;
    }

    {
        unsigned char *raw_certificate = (unsigned char *)malloc(4096);
        FILE *raw_file;
        size_t raw_size;

        if (!raw_certificate) {
            X509_free(certificate);
            free(certificate_path);
            free(key_path);
            free(p12_path);
            set_error("Unable to allocate certificate buffer");
            return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
        }

        raw_file = fopen(certificate_path, "rb");
        if (!raw_file) {
            free(raw_certificate);
            X509_free(certificate);
            free(certificate_path);
            free(key_path);
            free(p12_path);
            set_error("Unable to reopen client.pem");
            return LEGACY_GAMESTREAM_FAILED;
        }

        raw_size = fread(raw_certificate, 1, 4096, raw_file);
        fclose(raw_file);

        if (raw_size == 0 || raw_size * 2 >= sizeof(s_cert_hex)) {
            free(raw_certificate);
            X509_free(certificate);
            free(certificate_path);
            free(key_path);
            free(p12_path);
            set_error("Client certificate is too large");
            return LEGACY_GAMESTREAM_FAILED;
        }

        bytes_to_hex(raw_certificate, s_cert_hex, raw_size);
        free(raw_certificate);
    }

    file = fopen(key_path, "r");
    if (!file) {
        X509_free(certificate);
        free(certificate_path);
        free(key_path);
        free(p12_path);
        set_error("Unable to open key.pem");
        return LEGACY_GAMESTREAM_FAILED;
    }

    private_key = PEM_read_PrivateKey(file, NULL, NULL, NULL);
    fclose(file);

    if (!private_key) {
        X509_free(certificate);
        free(certificate_path);
        free(key_path);
        free(p12_path);
        set_error("Unable to load key.pem");
        return LEGACY_GAMESTREAM_FAILED;
    }

    X509_free(s_cert);
    EVP_PKEY_free(s_private_key);

    s_cert = certificate;
    s_private_key = private_key;
    result = LEGACY_GAMESTREAM_OK;

    free(certificate_path);
    free(key_path);
    free(p12_path);
    return result;
}

static size_t http_write_callback(void *contents, size_t size, size_t count, void *userdata)
{
    HttpBuffer *buffer = (HttpBuffer *)userdata;
    size_t length = size * count;
    char *new_memory = (char *)realloc(buffer->memory, buffer->size + length + 1);

    if (!new_memory) {
        buffer->allocation_failed = true;
        return 0;
    }

    buffer->memory = new_memory;
    memcpy(buffer->memory + buffer->size, contents, length);
    buffer->size += length;
    buffer->memory[buffer->size] = '\0';

    return length;
}

static void http_buffer_init(HttpBuffer *buffer)
{
    buffer->memory = (char *)malloc(1);
    buffer->size = 0;
    buffer->allocation_failed = !buffer->memory;

    if (buffer->memory) {
        buffer->memory[0] = '\0';
    }
}

static void http_buffer_free(HttpBuffer *buffer)
{
    free(buffer->memory);
    buffer->memory = NULL;
    buffer->size = 0;
    buffer->allocation_failed = false;
}

static int verify_pinned_certificate(X509_STORE_CTX *context, void *expected)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    X509 *peer = context->cert;
#else
    X509 *peer = X509_STORE_CTX_get0_cert(context);
#endif
    int matches = expected && peer && X509_cmp(peer, (X509 *)expected) == 0;
    X509_STORE_CTX_set_error(context, matches ? X509_V_OK : X509_V_ERR_CERT_REJECTED);
    return matches;
}

static CURLcode configure_pinned_tls(CURL *curl, void *ssl_context, void *userdata)
{
    LegacyGameStreamServer *server = (LegacyGameStreamServer *)userdata;
    (void)curl;
    if (!server || !server->server_certificate) return CURLE_PEER_FAILED_VERIFICATION;
    /* Pairing authenticates the complete self-signed leaf, rather than a public CA/DNS name. */
    SSL_CTX_set_cert_verify_callback((SSL_CTX *)ssl_context,
        verify_pinned_certificate, server->server_certificate);
    return CURLE_OK;
}

int legacy_gamestream_set_certificate_path(LegacyGameStreamServer *server, const char *path)
{
    if (!server || !path || strlen(path) >= sizeof(server->server_certificate_path))
        return LEGACY_GAMESTREAM_INVALID;
    strcpy(server->server_certificate_path, path);
    return LEGACY_GAMESTREAM_OK;
}

static int load_pinned_certificate(LegacyGameStreamServer *server)
{
    if (!server->server_certificate_path[0]) return LEGACY_GAMESTREAM_OK;
    FILE *file = fopen(server->server_certificate_path, "rb");
    if (!file) {
        if (errno == ENOENT) return LEGACY_GAMESTREAM_OK; /* Old host: pairing is required. */
        set_error("Unable to read saved server certificate");
        return LEGACY_GAMESTREAM_IO_ERROR;
    }
    server->server_certificate = PEM_read_X509(file, NULL, NULL, NULL);
    fclose(file);
    if (!server->server_certificate) {
        set_error("Invalid saved server certificate; remove the PC and pair again");
        return LEGACY_GAMESTREAM_SECURITY_ERROR;
    }
    return LEGACY_GAMESTREAM_OK;
}

static int save_pinned_certificate(LegacyGameStreamServer *server)
{
    char temporary[sizeof(server->server_certificate_path) + 5];
    if (!server->server_certificate_path[0] || !server->server_certificate)
        return LEGACY_GAMESTREAM_IO_ERROR;
    snprintf(temporary, sizeof(temporary), "%s.tmp", server->server_certificate_path);
    FILE *file = fopen(temporary, "wb");
    if (!file) return LEGACY_GAMESTREAM_IO_ERROR;
    int written = PEM_write_X509(file, (X509 *)server->server_certificate) == 1;
    if (fflush(file) != 0) written = 0;
    if (fclose(file) != 0) written = 0;
    if (!written || rename(temporary, server->server_certificate_path) != 0) {
        remove(temporary);
        return LEGACY_GAMESTREAM_IO_ERROR;
    }
    return LEGACY_GAMESTREAM_OK;
}

static int pairing_progress(void *userdata, curl_off_t download_total, curl_off_t downloaded,
                            curl_off_t upload_total, curl_off_t uploaded)
{
    (void)download_total;
    (void)downloaded;
    (void)upload_total;
    (void)uploaded;
    return moonlight_is_cancelled((const MoonlightCancellation *)userdata);
}

static int http_request(
    LegacyGameStreamServer *server,
    const char *url,
    HttpBuffer *response,
    long timeout_seconds,
    const MoonlightCancellation *cancel)
{
    CURLcode curl_result;

    if (moonlight_is_cancelled(cancel)) {
        set_error("Pairing cancelled locally");
        return LEGACY_GAMESTREAM_CANCELLED;
    }

    if (!server || !server->curl || !url || !response) {
        return LEGACY_GAMESTREAM_INVALID;
    }

    pthread_mutex_lock(&s_http_mutex);
    if (moonlight_is_cancelled(cancel)) {
        set_error("Pairing cancelled locally");
        pthread_mutex_unlock(&s_http_mutex);
        return LEGACY_GAMESTREAM_CANCELLED;
    }
    if (!server->curl) {
        pthread_mutex_unlock(&s_http_mutex);
        return LEGACY_GAMESTREAM_INVALID;
    }
    if (!strncmp(url, "https://", 8) && !server->server_certificate) {
        s_last_http_result = CURLE_PEER_FAILED_VERIFICATION;
        set_error("Server certificate is not paired");
        pthread_mutex_unlock(&s_http_mutex);
        return LEGACY_GAMESTREAM_SECURITY_ERROR;
    }
    curl_easy_setopt((CURL *)server->curl, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt((CURL *)server->curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt((CURL *)server->curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt((CURL *)server->curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt((CURL *)server->curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt((CURL *)server->curl, CURLOPT_URL, url);

    /*
     * GameStream pairing and current Moonlight Qt intentionally avoid
     * persistent HTTP connections. Sunshine closes the pending getservercert
     * response, and reusing that connection is unreliable on Vita libcurl.
     */
    curl_easy_setopt((CURL *)server->curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt((CURL *)server->curl, CURLOPT_FRESH_CONNECT, 1L);
    curl_easy_setopt((CURL *)server->curl, CURLOPT_FORBID_REUSE, 1L);

    http_buffer_free(response);
    http_buffer_init(response);

    if (!response->memory) {
        set_error("Unable to allocate GameStream response");
        pthread_mutex_unlock(&s_http_mutex);
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }

    /* Install only for this transfer; clear before the job/token can be released. */
    CURLcode setup_result = curl_easy_setopt((CURL *)server->curl,
        CURLOPT_XFERINFOFUNCTION, pairing_progress);
    if (setup_result == CURLE_OK) setup_result = curl_easy_setopt((CURL *)server->curl,
        CURLOPT_XFERINFODATA, cancel);
    if (setup_result == CURLE_OK) setup_result = curl_easy_setopt((CURL *)server->curl,
        CURLOPT_NOPROGRESS, cancel ? 0L : 1L);
    if (setup_result != CURLE_OK) {
        curl_easy_setopt((CURL *)server->curl, CURLOPT_XFERINFODATA, NULL);
        curl_easy_setopt((CURL *)server->curl, CURLOPT_NOPROGRESS, 1L);
        curl_easy_setopt((CURL *)server->curl, CURLOPT_XFERINFOFUNCTION, NULL);
        set_error("Unable to configure cancellable GameStream transfer");
        pthread_mutex_unlock(&s_http_mutex);
        return LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }
#ifdef MOONLIGHT_HTTP_DISCONNECT_TEST
    MoonlightHttpTestTransfer http_test;
    setup_result = (CURLcode)disconnect_http_test_begin(server->curl, url, &http_test);
    if (setup_result != CURLE_OK) {
        curl_easy_setopt((CURL *)server->curl, CURLOPT_NOPROGRESS, 1L);
        curl_easy_setopt((CURL *)server->curl, CURLOPT_XFERINFODATA, NULL);
        curl_easy_setopt((CURL *)server->curl, CURLOPT_XFERINFOFUNCTION, NULL);
        set_error("Unable to configure HTTP disconnect test");
        pthread_mutex_unlock(&s_http_mutex);
        return LEGACY_GAMESTREAM_FAILED;
    }
#endif
    s_last_http_result = CURLE_OK;
    curl_result = curl_easy_perform((CURL *)server->curl);
#ifdef MOONLIGHT_HTTP_DISCONNECT_TEST
    disconnect_http_test_end(server->curl, &http_test, (int)curl_result);
#endif
    curl_easy_setopt((CURL *)server->curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt((CURL *)server->curl, CURLOPT_XFERINFODATA, NULL);
    curl_easy_setopt((CURL *)server->curl, CURLOPT_XFERINFOFUNCTION, NULL);
    s_last_http_result = curl_result;
    long http_status = 0;
    curl_easy_getinfo((CURL *)server->curl, CURLINFO_RESPONSE_CODE, &http_status);
    pthread_mutex_unlock(&s_http_mutex);
    if (moonlight_is_cancelled(cancel)) {
        set_error("Pairing cancelled locally");
        return LEGACY_GAMESTREAM_CANCELLED;
    }
    if (curl_result != CURLE_OK) {
        if (response->allocation_failed || curl_result == CURLE_OUT_OF_MEMORY) {
            set_error("Unable to allocate GameStream response");
            return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
        }
        set_error(curl_easy_strerror(curl_result));
        if (curl_result == CURLE_PEER_FAILED_VERIFICATION)
            return LEGACY_GAMESTREAM_SECURITY_ERROR;
        if (curl_result == CURLE_HTTP_RETURNED_ERROR && http_status == 401)
            return LEGACY_GAMESTREAM_NOT_PAIRED;
        return LEGACY_GAMESTREAM_FAILED;
    }

    return LEGACY_GAMESTREAM_OK;
}

int legacy_gamestream_request(
    LegacyGameStreamServer *server, const char *endpoint, const char *query,
    long timeout_seconds, char **body, size_t *length)
{
    char uuid[UUID_STRLEN];
    char *url;
    HttpBuffer response;
    int result;
    if (!server || !endpoint || !body || !length || timeout_seconds <= 0) {
        return LEGACY_GAMESTREAM_INVALID;
    }
    *body = NULL;
    *length = 0;
    generate_uuid(uuid);
    url = (char *)malloc(8192);
    if (!url) return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    int written = snprintf(url, 8192, "https://%s:%u/%s?uniqueid=%s&uuid=%s%s%s",
        server->address, server->https_port ? server->https_port : 47984,
        endpoint, s_unique_id, uuid, query && query[0] ? "&" : "", query ? query : "");
    if (written < 0 || written >= 8192) {
        free(url);
        return LEGACY_GAMESTREAM_INVALID;
    }
    http_buffer_init(&response);
    result = http_request(server, url, &response, timeout_seconds, NULL);
    free(url);
    if (result == LEGACY_GAMESTREAM_OK) {
        *body = response.memory;
        *length = response.size;
    } else {
        http_buffer_free(&response);
    }
    return result;
}

static void XMLCALL xml_find_start(void *userdata, const char *name, const char **attributes)
{
    XmlFindContext *context = (XmlFindContext *)userdata;
    (void)attributes;

    if (context->depth == 0 && strcmp(name, context->target) == 0) {
        context->depth = 1;
        context->length = 0;
        if (context->capacity > 0) {
            context->output[0] = '\0';
        }
    } else if (context->depth > 0) {
        ++context->depth;
    }
}

static void XMLCALL xml_find_end(void *userdata, const char *name)
{
    XmlFindContext *context = (XmlFindContext *)userdata;

    if (context->depth > 0) {
        --context->depth;
        if (context->depth == 0 && strcmp(name, context->target) == 0) {
            context->found = true;
        }
    }
}

static void XMLCALL xml_find_text(void *userdata, const XML_Char *data, int length)
{
    XmlFindContext *context = (XmlFindContext *)userdata;
    size_t available;
    size_t copy_length;

    if (context->depth != 1 || context->found || context->capacity == 0) {
        return;
    }

    available = context->capacity - 1 - context->length;
    copy_length = (size_t)length < available ? (size_t)length : available;

    if (copy_length > 0) {
        memcpy(context->output + context->length, data, copy_length);
        context->length += copy_length;
        context->output[context->length] = '\0';
    }
}

static int xml_find_value(
    const char *data,
    size_t length,
    const char *node,
    char *output,
    size_t output_size)
{
    XML_Parser parser;
    XmlFindContext context;

    if (!data || !node || !output || output_size == 0) {
        return LEGACY_GAMESTREAM_INVALID;
    }

    output[0] = '\0';

    context.target = node;
    context.output = output;
    context.capacity = output_size;
    context.length = 0;
    context.depth = 0;
    context.found = false;

    parser = XML_ParserCreate("UTF-8");
    if (!parser) {
        set_error("Unable to allocate GameStream XML parser");
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }

    XML_SetUserData(parser, &context);
    XML_SetElementHandler(parser, xml_find_start, xml_find_end);
    XML_SetCharacterDataHandler(parser, xml_find_text);

    if (!XML_Parse(parser, data, (int)length, 1)) {
        set_error(XML_ErrorString(XML_GetErrorCode(parser)));
        XML_ParserFree(parser);
        return LEGACY_GAMESTREAM_INVALID;
    }

    XML_ParserFree(parser);
    return context.found ? LEGACY_GAMESTREAM_OK : LEGACY_GAMESTREAM_INVALID;
}

static void XMLCALL xml_status_start(void *userdata, const char *name, const char **attributes)
{
    XmlStatusContext *context = (XmlStatusContext *)userdata;
    int i;

    if (strcmp(name, "root") != 0 || !attributes) {
        return;
    }

    for (i = 0; attributes[i]; i += 2) {
        if (strcmp(attributes[i], "status_code") == 0) {
            context->status = atoi(attributes[i + 1]);
        } else if (strcmp(attributes[i], "status_message") == 0) {
            size_t length = strlen(attributes[i + 1]);
            if (length >= sizeof(context->message)) {
                length = sizeof(context->message) - 1;
                /* Do not split a UTF-8 code point in a bounded UI message. */
                while (length && ((unsigned char)attributes[i + 1][length] & 0xc0) == 0x80) {
                    --length;
                }
            }
            memcpy(context->message, attributes[i + 1], length);
            context->message[length] = '\0';
        }
    }
}

static int xml_status_ok(const char *data, size_t length, LegacyGameStreamPairingError *error)
{
    XML_Parser parser;
    XmlStatusContext context;

    memset(&context, 0, sizeof(context));
    if (error) memset(error, 0, sizeof(*error));

    parser = XML_ParserCreate("UTF-8");
    if (!parser) {
        set_error("Unable to allocate GameStream XML parser");
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }

    XML_SetUserData(parser, &context);
    XML_SetStartElementHandler(parser, xml_status_start);

    if (!XML_Parse(parser, data, (int)length, 1)) {
        set_error(XML_ErrorString(XML_GetErrorCode(parser)));
        XML_ParserFree(parser);
        return LEGACY_GAMESTREAM_INVALID;
    }

    XML_ParserFree(parser);

    if (context.status != STATUS_OK) {
        set_error(context.message[0] ? context.message : "GameStream server returned an error");
        if (error) {
            error->status_code = context.status;
            memcpy(error->status_message, context.message, sizeof(error->status_message));
        }
        return context.status == 401 ? LEGACY_GAMESTREAM_NOT_PAIRED : LEGACY_GAMESTREAM_FAILED;
    }

    return LEGACY_GAMESTREAM_OK;
}

static int load_server_info(LegacyGameStreamServer *server, bool https,
                            const MoonlightCancellation *cancel)
{
    char uuid[UUID_STRLEN];
    char url[2048];
    char current_game[32];
    char paired[16];
    char app_version[64];
    char gfe_version[64];
    char codec_mode_support[32];
    char state[256];
    char https_port[16];
    char mac[64];
    char unique_id[64];
    HttpBuffer response;
    int result;

    generate_uuid(uuid);

    snprintf(
        url,
        sizeof(url),
        "%s://%s:%u/serverinfo?uniqueid=%s&uuid=%s",
        https ? "https" : "http",
        server->address,
        https ? server->https_port : server->http_port,
        s_unique_id,
        uuid);

    http_buffer_init(&response);
    if (!response.memory) {
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }

    result = http_request(server, url, &response, 15L, cancel);
    if (result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        return result;
    }

    result = xml_status_ok(response.memory, response.size, NULL);
    if (result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        return result;
    }

    if (xml_find_value(response.memory, response.size, "currentgame", current_game, sizeof(current_game)) != LEGACY_GAMESTREAM_OK ||
        xml_find_value(response.memory, response.size, "PairStatus", paired, sizeof(paired)) != LEGACY_GAMESTREAM_OK ||
        xml_find_value(response.memory, response.size, "appversion", app_version, sizeof(app_version)) != LEGACY_GAMESTREAM_OK ||
        xml_find_value(response.memory, response.size, "state", state, sizeof(state)) != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        set_error("Incomplete serverinfo response");
        return LEGACY_GAMESTREAM_INVALID;
    }

    if (xml_find_value(response.memory, response.size, "uniqueid",
                       unique_id, sizeof(unique_id)) == LEGACY_GAMESTREAM_OK) {
        strncpy(server->unique_id, unique_id, sizeof(server->unique_id) - 1);
        server->unique_id[sizeof(server->unique_id) - 1] = '\0';
        vita_debug_log("[GameStream] server uniqueid=%s", server->unique_id);
    }

    if (xml_find_value(response.memory, response.size, "HttpsPort", https_port, sizeof(https_port)) == LEGACY_GAMESTREAM_OK) {
        server->https_port = (unsigned short)atoi(https_port);
    }

    if (!server->https_port) {
        server->https_port = 47984;
    }

    if (xml_find_value(response.memory, response.size, "mac", mac, sizeof(mac)) == LEGACY_GAMESTREAM_OK) {
        strncpy(server->mac, mac, sizeof(server->mac) - 1);
        server->mac[sizeof(server->mac) - 1] = '\0';
    } else {
        server->mac[0] = '\0';
    }

    char *end;
    errno = 0;
    long active_id = strtol(current_game, &end, 10);
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (end == current_game || *end || errno == ERANGE || active_id < 0 || active_id > INT_MAX) {
        http_buffer_free(&response);
        set_error("Invalid currentgame in serverinfo");
        return LEGACY_GAMESTREAM_INVALID;
    }
    server->paired = https && server->server_certificate && strcmp(paired, "1") == 0;
    server->current_game = (int)active_id;
    strncpy(server->server_info_app_version, app_version, sizeof(server->server_info_app_version) - 1);
    server->server_info_app_version[sizeof(server->server_info_app_version) - 1] = '\0';
    if (xml_find_value(response.memory, response.size, "GfeVersion", gfe_version, sizeof(gfe_version)) == LEGACY_GAMESTREAM_OK) {
        strncpy(server->server_info_gfe_version, gfe_version, sizeof(server->server_info_gfe_version) - 1);
        server->server_info_gfe_version[sizeof(server->server_info_gfe_version) - 1] = '\0';
    }
    if (xml_find_value(response.memory, response.size, "ServerCodecModeSupport",
                       codec_mode_support, sizeof(codec_mode_support)) == LEGACY_GAMESTREAM_OK) {
        server->server_codec_mode_support = atoi(codec_mode_support);
    } else {
        server->server_codec_mode_support = 0x00000001;
    }
    const char *busy = "_SERVER_BUSY";
    if (strlen(state) < strlen(busy) ||
        strcmp(state + strlen(state) - strlen(busy), busy) != 0) {
        server->current_game = 0;
    }
    server->server_major_version = atoi(app_version);

    http_buffer_free(&response);
    return LEGACY_GAMESTREAM_OK;
}

int legacy_gamestream_refresh_server_info(LegacyGameStreamServer *server)
{
    if (!server || !server->curl || !server->paired) return LEGACY_GAMESTREAM_WRONG_STATE;
    int result = load_server_info(server, true, NULL);
    if (result == LEGACY_GAMESTREAM_OK && !server->paired) {
        set_error("Client is no longer paired");
        return LEGACY_GAMESTREAM_WRONG_STATE;
    }
    return result;
}

int legacy_gamestream_quit_app(LegacyGameStreamServer *server)
{
    if (!server || !server->curl || !server->paired || !server->server_certificate) {
        set_error("Remote Quit requires a paired host");
        return LEGACY_GAMESTREAM_WRONG_STATE;
    }

    char *body = NULL;
    size_t length = 0;
    int result = legacy_gamestream_request(server, "cancel", NULL, 30L, &body, &length);
    if (result != LEGACY_GAMESTREAM_OK) {
        free(body);
        vita_debug_log("[GameStream] quit request failed result=%d", result);
        return result;
    }

    MoonlightGameStreamResponse response;
    result = moonlight_xml_parse_cancel(body, length, &response);
    free(body);
    if (result != MOONLIGHT_XML_OK) {
        set_error(response.status_message[0] ? response.status_message : "Invalid remote Quit response");
        vita_debug_log("[GameStream] quit response rejected result=%d status=%d: %s",
            result, response.status_code, s_error);
        if (result == MOONLIGHT_XML_OUT_OF_MEMORY) return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
        if (result == MOONLIGHT_XML_SERVER_ERROR) {
            return response.status_code == 401 ? LEGACY_GAMESTREAM_NOT_PAIRED : LEGACY_GAMESTREAM_FAILED;
        }
        return LEGACY_GAMESTREAM_INVALID;
    }

    result = legacy_gamestream_refresh_server_info(server);
    if (result != LEGACY_GAMESTREAM_OK) {
        vita_debug_log("[GameStream] quit verification failed result=%d", result);
        return result;
    }
    if (server->current_game != 0) {
        set_error("Host application is still running; quit it on the host or original device");
        vita_debug_log("[GameStream] quit did not stop active app=%d", server->current_game);
        return LEGACY_GAMESTREAM_QUIT_FAILED;
    }

    vita_debug_log("[GameStream] remote app quit verified");
    return LEGACY_GAMESTREAM_OK;
}

static int load_server_status(LegacyGameStreamServer *server)
{
    int result;

    if (!server->https_port) {
        result = load_server_info(server, false, NULL);
        if (result != LEGACY_GAMESTREAM_OK) {
            return result;
        }
    }

    if (server->server_certificate) {
        result = load_server_info(server, true, NULL);
        /* Revoked client authorization can be repaired by PIN. TLS failures cannot downgrade. */
        if (result == LEGACY_GAMESTREAM_NOT_PAIRED) result = load_server_info(server, false, NULL);
    } else {
        server->paired = false;
        result = LEGACY_GAMESTREAM_OK;
    }

    if (result == LEGACY_GAMESTREAM_OK &&
        !server->unsupported &&
        (server->server_major_version < MIN_SUPPORTED_GFE_VERSION ||
         server->server_major_version > MAX_SUPPORTED_GFE_VERSION)) {
        set_error("Unsupported GameStream server version");
        return LEGACY_GAMESTREAM_UNSUPPORTED_VERSION;
    }

    return result;
}

/* Keep parser allocation failures distinct from host rejection/malformed data. */
static int read_pairing_response(const HttpBuffer *response, LegacyGameStreamPairingError *error,
                                 const char *field, char *output, size_t output_size)
{
    char paired[8];
    int result = xml_status_ok(response->memory, response->size, error);
    if (result != LEGACY_GAMESTREAM_OK) return result;
    result = xml_find_value(response->memory, response->size, "paired", paired, sizeof(paired));
    if (result != LEGACY_GAMESTREAM_OK) return result;
    if (strcmp(paired, "1") != 0) return LEGACY_GAMESTREAM_FAILED;
    if (!field) return LEGACY_GAMESTREAM_OK;
    return xml_find_value(response->memory, response->size, field, output, output_size);
}

static int read_pairing_certificate(const char *pem, X509 **certificate)
{
    *certificate = NULL;
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    size_t length = strlen(pem);
    if (BIO_write(bio, pem, (int)length) != (int)length) {
        BIO_free(bio);
        return LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }
    ERR_clear_error();
    *certificate = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!*certificate) {
        return ERR_GET_REASON(ERR_peek_last_error()) == ERR_R_MALLOC_FAILURE
            ? LEGACY_GAMESTREAM_OUT_OF_MEMORY : LEGACY_GAMESTREAM_SECURITY_ERROR;
    }
    return LEGACY_GAMESTREAM_OK;
}

static int rsa_sign(
    const unsigned char *message,
    size_t message_length,
    unsigned char **signature,
    size_t *signature_length)
{
    EVP_MD_CTX *context;
    size_t required_length = 0;

    *signature = NULL;
    *signature_length = 0;

    context = EVP_MD_CTX_create();
    if (!context) {
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }

    if (EVP_DigestSignInit(context, NULL, EVP_sha256(), NULL, s_private_key) != 1 ||
        EVP_DigestSignUpdate(context, message, message_length) != 1 ||
        EVP_DigestSignFinal(context, NULL, &required_length) != 1 ||
        required_length == 0) {
        EVP_MD_CTX_destroy(context);
        return LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }

    *signature = (unsigned char *)OPENSSL_malloc(required_length);
    if (!*signature) {
        EVP_MD_CTX_destroy(context);
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }

    *signature_length = required_length;

    if (EVP_DigestSignFinal(context, *signature, signature_length) != 1) {
        OPENSSL_free(*signature);
        *signature = NULL;
        *signature_length = 0;
        EVP_MD_CTX_destroy(context);
        return LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }

    EVP_MD_CTX_destroy(context);
    return LEGACY_GAMESTREAM_OK;
}

static int get_certificate_signature(
    const char *certificate_pem,
    unsigned char *signature,
    size_t signature_capacity,
    size_t *signature_length)
{
    X509 *certificate;
    ASN1_BIT_STRING *certificate_signature;

    if (!certificate_pem || !signature || !signature_length) {
        return LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }

    *signature_length = 0;

    int result = read_pairing_certificate(certificate_pem, &certificate);
    if (result != LEGACY_GAMESTREAM_OK) return result;

    X509_get0_signature(&certificate_signature, NULL, certificate);
    if (!certificate_signature ||
        certificate_signature->length == 0 ||
        (size_t)certificate_signature->length > signature_capacity) {
        X509_free(certificate);
        return LEGACY_GAMESTREAM_SECURITY_ERROR;
    }

    memcpy(signature, certificate_signature->data, certificate_signature->length);
    *signature_length = (size_t)certificate_signature->length;

    X509_free(certificate);
    return LEGACY_GAMESTREAM_OK;
}

static int verify_signature(
    const unsigned char *data,
    size_t data_length,
    const unsigned char *signature,
    size_t signature_length,
    const char *certificate_pem)
{
    X509 *certificate;
    EVP_PKEY *public_key;
    EVP_MD_CTX *context;
    int result;

    result = read_pairing_certificate(certificate_pem, &certificate);
    if (result != LEGACY_GAMESTREAM_OK) return result;

    ERR_clear_error();
    public_key = X509_get_pubkey(certificate);
    if (!public_key) {
        X509_free(certificate);
        return ERR_GET_REASON(ERR_peek_last_error()) == ERR_R_MALLOC_FAILURE
            ? LEGACY_GAMESTREAM_OUT_OF_MEMORY : LEGACY_GAMESTREAM_SECURITY_ERROR;
    }

    context = EVP_MD_CTX_create();
    if (!context) {
        X509_free(certificate);
        EVP_PKEY_free(public_key);
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }
    if (EVP_DigestVerifyInit(context, NULL, EVP_sha256(), NULL, public_key) != 1 ||
        EVP_DigestVerifyUpdate(context, data, data_length) != 1) {
        result = LEGACY_GAMESTREAM_INTERNAL_ERROR;
    } else {
        int verified = EVP_DigestVerifyFinal(context, signature, signature_length);
        /* Zero rejects the proof; negative values indicate execution failure.
         * Both fail closed, and neither can become an incorrect-PIN result. */
        result = verified == 1 ? LEGACY_GAMESTREAM_OK
            : verified == 0 ? LEGACY_GAMESTREAM_SECURITY_ERROR : LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }

    EVP_MD_CTX_destroy(context);
    EVP_PKEY_free(public_key);
    X509_free(certificate);

    return result;
}

static int encrypt_ecb(
    const unsigned char *plaintext,
    size_t plaintext_length,
    const unsigned char *key,
    unsigned char *ciphertext)
{
    EVP_CIPHER_CTX *context;
    int output_length = 0;

    if ((plaintext_length % 16) != 0) {
        return LEGACY_GAMESTREAM_INVALID;
    }

    context = EVP_CIPHER_CTX_new();
    if (!context) {
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }

    if (EVP_EncryptInit_ex(context, EVP_aes_128_ecb(), NULL, key, NULL) != 1 ||
        EVP_CIPHER_CTX_set_padding(context, 0) != 1 ||
        EVP_EncryptUpdate(context, ciphertext, &output_length, plaintext, (int)plaintext_length) != 1) {
        EVP_CIPHER_CTX_free(context);
        return LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }

    EVP_CIPHER_CTX_free(context);
    return LEGACY_GAMESTREAM_OK;
}

static int decrypt_ecb(
    const unsigned char *ciphertext,
    size_t ciphertext_length,
    const unsigned char *key,
    unsigned char *plaintext)
{
    EVP_CIPHER_CTX *context;
    int output_length = 0;

    if ((ciphertext_length % 16) != 0) {
        return LEGACY_GAMESTREAM_INVALID;
    }

    context = EVP_CIPHER_CTX_new();
    if (!context) {
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }

    if (EVP_DecryptInit_ex(context, EVP_aes_128_ecb(), NULL, key, NULL) != 1 ||
        EVP_CIPHER_CTX_set_padding(context, 0) != 1 ||
        EVP_DecryptUpdate(context, plaintext, &output_length, ciphertext, (int)ciphertext_length) != 1) {
        EVP_CIPHER_CTX_free(context);
        return LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }

    EVP_CIPHER_CTX_free(context);
    return LEGACY_GAMESTREAM_OK;
}

static int pair(LegacyGameStreamServer *server, const char *pin, LegacyGameStreamPairingError *error,
                const MoonlightCancellation *cancel)
{
    s_error[0] = '\0';
    s_last_http_result = CURLE_OK;
    if (error) memset(error, 0, sizeof(*error));

    unsigned char salt_data[16];
    char salt_hex[sizeof(salt_data) * 2 + 1];
    char uuid[UUID_STRLEN];
    char url[4096];
    char result[8192];
    char plaincert[8192];
    unsigned char salt_pin[20];
    unsigned char aes_key[32];
    unsigned char challenge_data[16];
    unsigned char challenge_enc[16];
    char challenge_hex[sizeof(challenge_enc) * 2 + 1];
    unsigned char challenge_response_enc[64];
    unsigned char challenge_response[64];
    size_t challenge_response_length;
    unsigned char client_secret[16];
    unsigned char challenge_response_input[16 + SIGNATURE_LEN + sizeof(client_secret)];
    unsigned char challenge_response_hash[32];
    unsigned char challenge_response_hash_enc[32];
    char challenge_response_hex[sizeof(challenge_response_hash_enc) * 2 + 1];
    unsigned char pairing_secret[16 + SIGNATURE_LEN];
    char client_pairing_secret_hex[(sizeof(client_secret) + SIGNATURE_LEN) * 2 + 1];
    ASN1_BIT_STRING *certificate_signature = NULL;
    unsigned char *signature = NULL;
    size_t signature_length = 0;
    size_t result_length;
    int hash_length;
    HttpBuffer response;
    int step_result;

    if (!server || !pin || strlen(pin) != 4) {
        set_error("Invalid pairing PIN");
        return LEGACY_GAMESTREAM_INVALID;
    }

    if (server->paired) {
        set_error("Already paired");
        return LEGACY_GAMESTREAM_WRONG_STATE;
    }

    if (!server->unsupported &&
        (server->server_major_version < MIN_SUPPORTED_GFE_VERSION ||
         server->server_major_version > MAX_SUPPORTED_GFE_VERSION)) {
        set_error("Unsupported GameStream server version");
        return LEGACY_GAMESTREAM_UNSUPPORTED_VERSION;
    }

    if (RAND_bytes(salt_data, sizeof(salt_data)) != 1) {
        set_error("Unable to generate pairing salt");
        return LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }

    bytes_to_hex(salt_data, salt_hex, sizeof(salt_data));
    generate_uuid(uuid);

    snprintf(
        url,
        sizeof(url),
        "http://%s:%u/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1&phrase=getservercert&salt=%s&clientcert=%s",
        server->address,
        server->http_port,
        s_unique_id,
        uuid,
        salt_hex,
        s_cert_hex);

    http_buffer_init(&response);
    if (!response.memory) {
        set_error("Unable to allocate GameStream response");
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }

    /*
     * Sunshine keeps the initial /pair request open until the operator
     * enters the PIN in its Web UI. Match Sunshine's five-minute pending
     * pairing-session lifetime instead of the normal HTTP request timeout.
     */
    vita_debug_log("[GameStream] pairing phase=getservercert");
    step_result = http_request(server, url, &response, 300L, cancel);

    if (step_result != LEGACY_GAMESTREAM_OK) {
        vita_debug_log(
            "[GameStream] pairing getservercert failed: %d (%s)",
            step_result,
            s_error);
        http_buffer_free(&response);
        return step_result;
    }

    vita_debug_log("[GameStream] pairing phase=getservercert response received");

    step_result = read_pairing_response(&response, error, "plaincert", result, sizeof(result));
    if (step_result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        if (!s_error[0]) set_error("GameStream rejected pairing request");
        return step_result;
    }

    result_length = strlen(result);
    if ((result_length & 1) != 0 || result_length / 2 >= sizeof(plaincert)) {
        http_buffer_free(&response);
        set_error("Invalid server certificate");
        return LEGACY_GAMESTREAM_INVALID;
    }

    {
        size_t i;

        for (i = 0; i < result_length / 2; ++i) {
            unsigned int byte = 0;
            sscanf(result + i * 2, "%2x", &byte);
            plaincert[i] = (char)byte;
        }
        plaincert[result_length / 2] = '\0';
    }

    memcpy(salt_pin, salt_data, 16);
    memcpy(salt_pin + 16, pin, 4);

    hash_length = server->server_major_version >= 7 ? 32 : 20;
    if (hash_length == 32) {
        SHA256(salt_pin, sizeof(salt_pin), aes_key);
    } else {
        SHA1(salt_pin, sizeof(salt_pin), aes_key);
        memset(aes_key + 20, 0, sizeof(aes_key) - 20);
    }

    if (RAND_bytes(challenge_data, sizeof(challenge_data)) != 1) {
        http_buffer_free(&response);
        set_error("Unable to build pairing challenge");
        return LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }
    step_result = encrypt_ecb(challenge_data, sizeof(challenge_data), aes_key, challenge_enc);
    if (step_result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        set_error("Unable to encrypt pairing challenge");
        return step_result;
    }

    bytes_to_hex(challenge_enc, challenge_hex, sizeof(challenge_enc));
    generate_uuid(uuid);

    vita_debug_log("[GameStream] pairing phase=clientchallenge");

    snprintf(
        url,
        sizeof(url),
        "http://%s:%u/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1&clientchallenge=%s",
        server->address,
        server->http_port,
        s_unique_id,
        uuid,
        challenge_hex);

    step_result = http_request(server, url, &response, 15L, cancel);
    if (step_result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        return step_result;
    }

    step_result = read_pairing_response(&response, error, "challengeresponse", result, sizeof(result));
    if (step_result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        if (!s_error[0]) set_error("GameStream rejected client challenge");
        return step_result;
    }

    result_length = strlen(result);
    challenge_response_length = result_length / 2;
    if ((result_length & 1) != 0 ||
        challenge_response_length < (size_t)(hash_length + 16) ||
        challenge_response_length > sizeof(challenge_response_enc) ||
        (challenge_response_length % 16) != 0) {
        http_buffer_free(&response);
        set_error("Invalid server challenge response");
        return LEGACY_GAMESTREAM_INVALID;
    }

    {
        size_t i;

        for (i = 0; i < challenge_response_length; ++i) {
            unsigned int byte = 0;
            sscanf(result + i * 2, "%2x", &byte);
            challenge_response_enc[i] = (unsigned char)byte;
        }
    }

    step_result = decrypt_ecb(
            challenge_response_enc,
            challenge_response_length,
            aes_key,
            challenge_response);
    if (step_result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        set_error("Unable to decrypt server challenge");
        return step_result;
    }

    if (RAND_bytes(client_secret, sizeof(client_secret)) != 1 ||
        !s_cert) {
        http_buffer_free(&response);
        set_error("Unable to generate client pairing secret");
        return LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }

    X509_get0_signature(&certificate_signature, NULL, s_cert);
    if (!certificate_signature ||
        certificate_signature->length > SIGNATURE_LEN) {
        http_buffer_free(&response);
        set_error("Invalid client certificate signature");
        return LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }

    memset(challenge_response_hash, 0, sizeof(challenge_response_hash));
    memset(challenge_response_input, 0, sizeof(challenge_response_input));
    memcpy(challenge_response_input, challenge_response + hash_length, 16);
    memcpy(challenge_response_input + 16, certificate_signature->data, certificate_signature->length);
    memcpy(
        challenge_response_input + 16 + certificate_signature->length,
        client_secret,
        sizeof(client_secret));

    memset(challenge_response_hash, 0, sizeof(challenge_response_hash));
    if (hash_length == 32) {
        SHA256(challenge_response_input, sizeof(challenge_response_input), challenge_response_hash);
    } else {
        SHA1(challenge_response_input, sizeof(challenge_response_input), challenge_response_hash);
    }

    memset(challenge_response_hash_enc, 0, sizeof(challenge_response_hash_enc));
    step_result = encrypt_ecb(
            challenge_response_hash,
            sizeof(challenge_response_hash_enc),
            aes_key,
            challenge_response_hash_enc);
    if (step_result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        set_error("Unable to encrypt server challenge response");
        return step_result;
    }

    bytes_to_hex(
        challenge_response_hash_enc,
        challenge_response_hex,
        sizeof(challenge_response_hash_enc));

    generate_uuid(uuid);

    vita_debug_log("[GameStream] pairing phase=serverchallengeresp");

    snprintf(
        url,
        sizeof(url),
        "http://%s:%u/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1&serverchallengeresp=%s",
        server->address,
        server->http_port,
        s_unique_id,
        uuid,
        challenge_response_hex);

    step_result = http_request(server, url, &response, 15L, cancel);
    if (step_result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        return step_result;
    }

    step_result = read_pairing_response(&response, error, "pairingsecret", result, sizeof(result));
    if (step_result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        if (!s_error[0]) set_error("GameStream rejected server challenge");
        return step_result;
    }

    result_length = strlen(result);
    if ((result_length & 1) != 0 ||
        result_length / 2 != sizeof(pairing_secret)) {
        http_buffer_free(&response);
        set_error("Invalid pairing secret");
        return LEGACY_GAMESTREAM_INVALID;
    }

    {
        size_t i;

        for (i = 0; i < sizeof(pairing_secret); ++i) {
            unsigned int byte = 0;
            sscanf(result + i * 2, "%2x", &byte);
            pairing_secret[i] = (unsigned char)byte;
        }
    }

    /* Authenticate the proof before attributing a challenge mismatch to the PIN. */
    step_result = verify_signature(
            pairing_secret,
            16,
            pairing_secret + 16,
            SIGNATURE_LEN,
            plaincert);
    if (step_result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        set_error(step_result == LEGACY_GAMESTREAM_SECURITY_ERROR
            ? "Server pairing proof rejected" : "Unable to verify server pairing proof");
        return step_result;
    }

    {
        unsigned char server_certificate_signature[SIGNATURE_LEN];
        unsigned char expected_response_input[16 + SIGNATURE_LEN + 16];
        unsigned char expected_response_hash[SHA256_DIGEST_LENGTH];
        size_t server_certificate_signature_length = 0;

        step_result = get_certificate_signature(
                plaincert,
                server_certificate_signature,
                sizeof(server_certificate_signature),
                &server_certificate_signature_length);
        if (step_result != LEGACY_GAMESTREAM_OK) {
            http_buffer_free(&response);
            set_error("Unable to parse server certificate signature");
            return step_result;
        }

        memcpy(expected_response_input, challenge_data, sizeof(challenge_data));
        memcpy(
            expected_response_input + sizeof(challenge_data),
            server_certificate_signature,
            server_certificate_signature_length);
        memcpy(
            expected_response_input + sizeof(challenge_data) + server_certificate_signature_length,
            pairing_secret,
            16);

        if (hash_length == 32) {
            SHA256(
                expected_response_input,
                sizeof(challenge_data) + server_certificate_signature_length + 16,
                expected_response_hash);
        } else {
            SHA1(
                expected_response_input,
                sizeof(challenge_data) + server_certificate_signature_length + 16,
                expected_response_hash);
        }

        if (memcmp(challenge_response, expected_response_hash, (size_t)hash_length) != 0) {
            http_buffer_free(&response);
            set_error("Incorrect pairing PIN");
            return LEGACY_GAMESTREAM_INCORRECT_PIN;
        }
    }

    step_result = rsa_sign(
            client_secret,
            sizeof(client_secret),
            &signature,
            &signature_length);
    if (step_result != LEGACY_GAMESTREAM_OK || signature_length != SIGNATURE_LEN) {
        OPENSSL_free(signature);
        http_buffer_free(&response);
        set_error("Failed to sign client pairing secret");
        return step_result != LEGACY_GAMESTREAM_OK ? step_result : LEGACY_GAMESTREAM_INTERNAL_ERROR;
    }

    memcpy(pairing_secret, client_secret, sizeof(client_secret));
    memcpy(pairing_secret + sizeof(client_secret), signature, SIGNATURE_LEN);
    OPENSSL_free(signature);

    bytes_to_hex(
        pairing_secret,
        client_pairing_secret_hex,
        sizeof(pairing_secret));

    generate_uuid(uuid);

    vita_debug_log("[GameStream] pairing phase=clientpairingsecret");

    snprintf(
        url,
        sizeof(url),
        "http://%s:%u/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1&clientpairingsecret=%s",
        server->address,
        server->http_port,
        s_unique_id,
        uuid,
        client_pairing_secret_hex);

    step_result = http_request(server, url, &response, 15L, cancel);
    if (step_result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        return step_result;
    }

    step_result = read_pairing_response(&response, error, NULL, NULL, 0);
    if (step_result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        if (!s_error[0]) set_error("GameStream rejected client pairing secret");
        return step_result;
    }

    generate_uuid(uuid);

    vita_debug_log("[GameStream] pairing phase=pairchallenge");

    /* The signature and PIN-bound challenge above have now authenticated plaincert.
     * Use it temporarily to authenticate the final HTTPS exchange; roll back on failure. */
    X509 *candidate = NULL;
    step_result = read_pairing_certificate(plaincert, &candidate);
    if (step_result != LEGACY_GAMESTREAM_OK) {
        http_buffer_free(&response);
        set_error("Unable to read pairing server certificate");
        return step_result;
    }
    pthread_mutex_lock(&s_http_mutex);
    X509 *previous = (X509 *)server->server_certificate;
    server->server_certificate = candidate;
    pthread_mutex_unlock(&s_http_mutex);

    snprintf(
        url,
        sizeof(url),
        "https://%s:%u/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1&phrase=pairchallenge",
        server->address,
        server->https_port,
        s_unique_id,
        uuid);

    step_result = http_request(server, url, &response, 15L, cancel);
    if (step_result != LEGACY_GAMESTREAM_OK) {
        pthread_mutex_lock(&s_http_mutex);
        server->server_certificate = previous;
        X509_free(candidate);
        pthread_mutex_unlock(&s_http_mutex);
        http_buffer_free(&response);
        return step_result;
    }

    step_result = read_pairing_response(&response, error, NULL, NULL, 0);
    if (step_result != LEGACY_GAMESTREAM_OK) {
        pthread_mutex_lock(&s_http_mutex);
        server->server_certificate = previous;
        X509_free(candidate);
        pthread_mutex_unlock(&s_http_mutex);
        http_buffer_free(&response);
        if (!s_error[0]) set_error("GameStream rejected HTTPS pairing challenge");
        return step_result;
    }

    http_buffer_free(&response);

    X509_free(previous);
    if (save_pinned_certificate(server) != LEGACY_GAMESTREAM_OK) {
        /* Protocol success is independent from persistence, just like device.ini. */
        vita_debug_log("[GameStream] paired, but server.pem was not saved; re-pair required after restart");
    }
    server->paired = true;

    for (step_result = 0; step_result < 3; ++step_result) {
        if (moonlight_is_cancelled(cancel)) return LEGACY_GAMESTREAM_CANCELLED;
        if (load_server_info(server, true, cancel) == LEGACY_GAMESTREAM_OK && server->mac[0]) {
            vita_debug_log("[GameStream] Paired host MAC: %s", server->mac);
            break;
        }

        sceKernelDelayThread(200 * 1000);
    }

    return LEGACY_GAMESTREAM_OK;
}

typedef struct InitArgs {
    LegacyGameStreamServer *server;
    const char *address;
    unsigned short http_port;
    const char *client_directory;
    const char *server_certificate_path;
    int log_level;
    bool unsupported;
} InitArgs;

static int init_worker(void *argument);

int legacy_gamestream_init(
    LegacyGameStreamServer *server,
    const char *address,
    unsigned short http_port,
    const char *client_directory,
    const char *server_certificate_path,
    int log_level,
    bool unsupported)
{
    InitArgs args;

    if (!server || !address || !address[0] || !client_directory || !client_directory[0])
        return LEGACY_GAMESTREAM_INVALID;

    args.server = server;
    args.address = address;
    args.http_port = http_port;
    args.client_directory = client_directory;
    args.server_certificate_path = server_certificate_path;
    args.log_level = log_level;
    args.unsupported = unsupported;
    int result = run_on_worker(init_worker, &args);
    if (result != LEGACY_GAMESTREAM_OK) legacy_gamestream_shutdown(server);
    return result;
}

static int init_worker(void *argument)
{
    InitArgs *args = (InitArgs *)argument;
    LegacyGameStreamServer *server = args->server;
    const char *address = args->address;
    unsigned short http_port = args->http_port;
    const char *client_directory = args->client_directory;
    int log_level = args->log_level;
    bool unsupported = args->unsupported;
    CURL *curl;
    unsigned char random_seed[0x40];
    char *certificate_path;
    char *key_path;
    (void)log_level;

    if (!server || !address || !address[0] || !client_directory || !client_directory[0]) {
        set_error("Invalid GameStream initialization arguments");
        return LEGACY_GAMESTREAM_INVALID;
    }

    memset(server, 0, sizeof(*server));
    strncpy(server->address, address, sizeof(server->address) - 1);
    server->address[sizeof(server->address) - 1] = '\0';
    server->http_port = http_port ? http_port : 47989;
    server->unsupported = unsupported;

    if (args->server_certificate_path &&
        legacy_gamestream_set_certificate_path(server, args->server_certificate_path) != LEGACY_GAMESTREAM_OK)
        return LEGACY_GAMESTREAM_INVALID;

    s_error[0] = '\0';

    sceKernelGetRandomNumber(random_seed, sizeof(random_seed));
    RAND_seed(random_seed, sizeof(random_seed));
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();

    vita_debug_log("[GameStream] preparing client credentials dir=%s", client_directory);

    if (make_directory_tree(client_directory) < 0) {
        vita_debug_log("[GameStream] make_directory_tree failed");
        return LEGACY_GAMESTREAM_FAILED;
    }

    vita_debug_log("[GameStream] loading unique client id");
    if (load_unique_id(client_directory) != LEGACY_GAMESTREAM_OK) {
        vita_debug_log("[GameStream] load_unique_id failed: %s", s_error);
        return LEGACY_GAMESTREAM_FAILED;
    }

    vita_debug_log("[GameStream] loading client certificate");
    if (load_certificate(client_directory) != LEGACY_GAMESTREAM_OK) {
        vita_debug_log("[GameStream] load_certificate failed: %s", s_error);
        return LEGACY_GAMESTREAM_FAILED;
    }

    vita_debug_log("[GameStream] client credentials ready");

    if (!s_curl_initialized) {
        if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
            set_error("Unable to initialize libcurl");
            return LEGACY_GAMESTREAM_FAILED;
        }
        s_curl_initialized = true;
    }

    curl = curl_easy_init();
    if (!curl) {
        set_error("Unable to allocate libcurl handle");
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }

    const curl_version_info_data *curl_info = curl_version_info(CURLVERSION_NOW);
    if (!curl_info || !curl_info->ssl_version || strncmp(curl_info->ssl_version, "OpenSSL/", 8)) {
        curl_easy_cleanup(curl);
        set_error("Server certificate pinning requires the OpenSSL curl backend");
        return LEGACY_GAMESTREAM_SECURITY_ERROR;
    }

    vita_debug_log("[GameStream] init %s:%u dir=%s", address, http_port, client_directory);

    certificate_path = (char *)malloc(PATH_MAX_LOCAL);
    key_path = (char *)malloc(PATH_MAX_LOCAL);
    if (!certificate_path || !key_path) {
        free(certificate_path);
        free(key_path);
        curl_easy_cleanup(curl);
        server->curl = NULL;
        set_error("Unable to allocate certificate paths");
        return LEGACY_GAMESTREAM_OUT_OF_MEMORY;
    }

    snprintf(certificate_path, PATH_MAX_LOCAL, "%s/%s", client_directory, CERTIFICATE_FILE_NAME);
    snprintf(key_path, PATH_MAX_LOCAL, "%s/%s", client_directory, KEY_FILE_NAME);

    if (curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_CAINFO, NULL) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_CAPATH, NULL) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, configure_pinned_tls) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, server) != CURLE_OK) {
        free(certificate_path);
        free(key_path);
        curl_easy_cleanup(curl);
        set_error("Unable to install server certificate verification");
        return LEGACY_GAMESTREAM_SECURITY_ERROR;
    }
    curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, certificate_path);
    curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, key_path);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L);

    server->curl = curl;
    free(certificate_path);
    free(key_path);

    int pin_result = load_pinned_certificate(server);
    if (pin_result != LEGACY_GAMESTREAM_OK) return pin_result;

    vita_debug_log("[GameStream] requesting server status");
    return load_server_status(server);
}

typedef struct PairArgs {
    LegacyGameStreamServer *server;
    const char *pin;
    LegacyGameStreamPairingError *error;
    const MoonlightCancellation *cancel;
} PairArgs;

static int pair_worker(void *argument)
{
    PairArgs *args = (PairArgs *)argument;

    return pair(args->server, args->pin, args->error, args->cancel);
}

int legacy_gamestream_pair(LegacyGameStreamServer *server, const char *pin,
                          LegacyGameStreamPairingError *error,
                          const MoonlightCancellation *cancel)
{
    PairArgs args;

    args.server = server;
    args.pin = pin;
    args.error = error;
    args.cancel = cancel;
    pthread_mutex_lock(&s_pair_mutex);
    int result = run_on_worker(pair_worker, &args);
    pthread_mutex_unlock(&s_pair_mutex);
    return result;
}

int legacy_gamestream_get_server_mac(
    LegacyGameStreamServer *server,
    char *mac,
    unsigned int size)
{
    if (!server || !mac || size == 0) {
        return LEGACY_GAMESTREAM_INVALID;
    }

    if (!server->mac[0]) {
        int result = load_server_info(server, true, NULL);
        if (result != LEGACY_GAMESTREAM_OK) {
            return result;
        }
    }

    if (!server->mac[0]) {
        set_error("Server MAC address is unavailable");
        return LEGACY_GAMESTREAM_INVALID;
    }

    strncpy(mac, server->mac, size - 1);
    mac[size - 1] = '\0';

    return LEGACY_GAMESTREAM_OK;
}

void legacy_gamestream_shutdown(LegacyGameStreamServer *server)
{
    pthread_mutex_lock(&s_pair_mutex);
    pthread_mutex_lock(&s_http_mutex);
    if (server && server->curl) {
        curl_easy_cleanup((CURL *)server->curl);
        server->curl = NULL;
    }
    if (server) {
        X509_free((X509 *)server->server_certificate);
        server->server_certificate = NULL;
        server->paired = false;
    }

    X509_free(s_cert);
    EVP_PKEY_free(s_private_key);
    s_cert = NULL;
    s_private_key = NULL;
    s_cert_hex[0] = '\0';
    s_unique_id[0] = '\0';

    if (s_curl_initialized) {
        curl_global_cleanup();
        s_curl_initialized = false;
    }
    pthread_mutex_unlock(&s_http_mutex);
    pthread_mutex_unlock(&s_pair_mutex);
}
