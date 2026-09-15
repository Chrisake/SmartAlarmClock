/**
 * @file http_stream.c
 */

/*********************
 *      INCLUDES
 *********************/

#include "net/http_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <wchar.h>
  #include <windows.h>
  #include <winhttp.h>
#elif defined(ESP_PLATFORM)
  #include <strings.h>
  #include "esp_crt_bundle.h"
  #include "esp_http_client.h"
#endif

/*********************
 *      DEFINES
 *********************/

/** Connect, send and receive timeouts. A live stream that stays silent this long has dropped. */
#define TIMEOUT_MS 15000

#if defined(_WIN32)

/**********************
 *      TYPEDEFS
 **********************/

struct http_stream_s {
    HINTERNET session;
    HINTERNET connect;
    HINTERNET request;
};

/**********************
 *   STATIC FUNCTIONS
 **********************/

static wchar_t * widen(const char * text)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if(n <= 0) return NULL;

    wchar_t * wide = malloc((size_t)n * sizeof(wchar_t));
    if(wide) MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, n);
    return wide;
}

/** A response header, as UTF-8; false if the response has none. */
static bool header_get(HINTERNET request, DWORD query, const wchar_t * name, char * out, size_t size)
{
    wchar_t value[128];
    DWORD   len = sizeof(value);

    if(!WinHttpQueryHeaders(request, query, name, value, &len, WINHTTP_NO_HEADER_INDEX)) return false;
    return WideCharToMultiByte(CP_UTF8, 0, value, -1, out, (int)size, NULL, NULL) > 0;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

http_stream_t * http_stream_open(const char * url, bool icy, http_stream_info_t * info)
{
    http_stream_t * stream = calloc(1, sizeof(*stream));
    wchar_t *       wide   = widen(url);
    wchar_t *       agent  = widen(HTTP_USER_AGENT);
    URL_COMPONENTS  parts;
    wchar_t         host[256];
    const wchar_t * path;
    DWORD           policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    DWORD           status = 0;
    DWORD           size   = sizeof(status);
    char            metaint[16];
    bool            ok     = false;

    memset(info, 0, sizeof(*info));
    if(!stream || !wide || !agent) goto done;

    memset(&parts, 0, sizeof(parts));
    parts.dwStructSize      = sizeof(parts);
    parts.dwSchemeLength    = (DWORD)-1;
    parts.dwHostNameLength  = (DWORD)-1;
    parts.dwUrlPathLength   = (DWORD)-1;
    parts.dwExtraInfoLength = (DWORD)-1;

    if(!WinHttpCrackUrl(wide, 0, 0, &parts)) goto done;
    if(parts.dwHostNameLength >= sizeof(host) / sizeof(host[0])) goto done;

    wmemcpy(host, parts.lpszHostName, parts.dwHostNameLength);
    host[parts.dwHostNameLength] = L'\0';

    /*The path runs on into the query string, which is what the request wants.*/
    path = parts.dwUrlPathLength ? parts.lpszUrlPath : L"/";

    stream->session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
    if(!stream->session) goto done;
    WinHttpSetTimeouts(stream->session, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS);

    stream->connect = WinHttpConnect(stream->session, host, parts.nPort, 0);
    if(!stream->connect) goto done;

    stream->request = WinHttpOpenRequest(stream->connect, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if(!stream->request) goto done;

    /*WinHTTP will not follow HTTPS to HTTP by default; favicons do that.*/
    WinHttpSetOption(stream->request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

    if(!WinHttpSendRequest(stream->request, icy ? L"Icy-MetaData: 1\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS,
                           icy ? (DWORD)-1L : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) goto done;
    if(!WinHttpReceiveResponse(stream->request, NULL)) goto done;

    WinHttpQueryHeaders(stream->request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    info->status = (int)status;
    if(status < 200 || status >= 300) goto done;

    header_get(stream->request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, info->content_type,
               sizeof(info->content_type));
    if(header_get(stream->request, WINHTTP_QUERY_CUSTOM, L"icy-metaint", metaint, sizeof(metaint))) {
        info->icy_metaint = (uint32_t)strtoul(metaint, NULL, 10);
    }

    ok = true;

done:
    free(wide);
    free(agent);

    if(!ok) {
        http_stream_close(stream);
        return NULL;
    }
    return stream;
}

int http_stream_read(http_stream_t * stream, char * buf, size_t len)
{
    DWORD available = 0;
    DWORD read      = 0;

    if(!WinHttpQueryDataAvailable(stream->request, &available)) return -1;
    if(available == 0) return 0;
    if(available > len) available = (DWORD)len;
    if(!WinHttpReadData(stream->request, buf, available, &read)) return -1;
    return (int)read;
}

void http_stream_close(http_stream_t * stream)
{
    if(!stream) return;

    if(stream->request) WinHttpCloseHandle(stream->request);
    if(stream->connect) WinHttpCloseHandle(stream->connect);
    if(stream->session) WinHttpCloseHandle(stream->session);
    free(stream);
}

#elif defined(ESP_PLATFORM)

/* TODO: untested on the clock. Needs CONFIG_MBEDTLS_CERTIFICATE_BUNDLE, and a
 * few kilobytes of stack for TLS on the calling thread. */

/*********************
 *      DEFINES
 *********************/

#define MAX_REDIRECTS 5

/** Read timeouts in a row before a stream counts as dropped. */
#define READ_RETRIES 3

/**********************
 *      TYPEDEFS
 **********************/

struct http_stream_s {
    esp_http_client_handle_t client;
    http_stream_info_t *     info;  /**< Filled in from headers while opening */
};

/**********************
 *   STATIC FUNCTIONS
 **********************/

static esp_err_t header_event(esp_http_client_event_t * evt)
{
    http_stream_t * stream = evt->user_data;

    if(evt->event_id != HTTP_EVENT_ON_HEADER || !stream->info) return ESP_OK;

    if(strcasecmp(evt->header_key, "Content-Type") == 0) {
        snprintf(stream->info->content_type, sizeof(stream->info->content_type), "%s", evt->header_value);
    }
    else if(strcasecmp(evt->header_key, "icy-metaint") == 0) {
        stream->info->icy_metaint = (uint32_t)strtoul(evt->header_value, NULL, 10);
    }
    return ESP_OK;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

http_stream_t * http_stream_open(const char * url, bool icy, http_stream_info_t * info)
{
    memset(info, 0, sizeof(*info));

    http_stream_t * stream = calloc(1, sizeof(*stream));
    if(!stream) return NULL;
    stream->info = info;

    esp_http_client_config_t config = {
        .url               = url,
        .user_agent        = HTTP_USER_AGENT,
        .timeout_ms        = TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler     = header_event,
        .user_data         = stream,
    };

    stream->client = esp_http_client_init(&config);
    if(!stream->client) goto fail;
    if(icy) esp_http_client_set_header(stream->client, "Icy-MetaData", "1");

    for(int hop = 0;; hop++) {
        if(esp_http_client_open(stream->client, 0) != ESP_OK) goto fail;
        if(esp_http_client_fetch_headers(stream->client) < 0) goto fail;

        info->status  = esp_http_client_get_status_code(stream->client);
        bool redirect = info->status == 301 || info->status == 302 || info->status == 303 ||
                        info->status == 307 || info->status == 308;
        if(!redirect || hop >= MAX_REDIRECTS) break;

        esp_http_client_flush_response(stream->client, NULL);
        if(esp_http_client_set_redirection(stream->client) != ESP_OK) goto fail;
        info->content_type[0] = '\0';
        info->icy_metaint     = 0;
    }

    stream->info = NULL;
    if(info->status < 200 || info->status >= 300) goto fail;
    return stream;

fail:
    stream->info = NULL;
    http_stream_close(stream);
    return NULL;
}

int http_stream_read(http_stream_t * stream, char * buf, size_t len)
{
    for(int attempt = 0; attempt < READ_RETRIES; attempt++) {
        int n = esp_http_client_read(stream->client, buf, (int)len);
        if(n != -ESP_ERR_HTTP_EAGAIN) return n < 0 ? -1 : n;
    }
    return -1;
}

void http_stream_close(http_stream_t * stream)
{
    if(!stream) return;

    if(stream->client) {
        esp_http_client_close(stream->client);
        esp_http_client_cleanup(stream->client);
    }
    free(stream);
}

#else

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

http_stream_t * http_stream_open(const char * url, bool icy, http_stream_info_t * info)
{
    (void)icy;
    memset(info, 0, sizeof(*info));
    fprintf(stderr, "http_stream: no HTTP client on this platform, skipped %s\n", url);
    return NULL;
}

int http_stream_read(http_stream_t * stream, char * buf, size_t len)
{
    (void)stream;
    (void)buf;
    (void)len;
    return -1;
}

void http_stream_close(http_stream_t * stream)
{
    (void)stream;
}

#endif
