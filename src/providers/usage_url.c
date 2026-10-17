/* SPDX-License-Identifier: MIT */
#include "providers/usage_url.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "busy.h"
#include "provider.h"
#include "xalloc.h"
#include "providers/http_provider.h"
#include "providers/usage_render.h"
#include "render/ctrl_strip.h"
#include "terminal/ansi.h"
#include "terminal/ui.h"
#include "transport/http.h"

#define USAGE_URL_TIMEOUT_S 5
#define USAGE_URL_MAX_BYTES (64 * 1024)

static int has_http_scheme(const char *url)
{
    return strncasecmp(url, "http://", 7) == 0 || strncasecmp(url, "https://", 8) == 0;
}

char *usage_url_resolve(const char *base_url, const char *usage_url)
{
    if (has_http_scheme(usage_url))
        return xstrdup(usage_url);
    while (*usage_url == '/')
        usage_url++;
    size_t base_length = strlen(base_url);
    while (base_length > 0 && base_url[base_length - 1] == '/')
        base_length--;
    return xasprintf("%.*s/%s", (int)base_length, base_url, usage_url);
}

/* Drop the trailing blank run so the report ends on its last line of text. */
static void trim_trailing_space(char *text)
{
    size_t length = strlen(text);
    while (length > 0 &&
           (text[length - 1] == '\n' || text[length - 1] == ' ' || text[length - 1] == '\t'))
        length--;
    text[length] = '\0';
}

static void print_report(const char *name, char *text)
{
    usage_heading_print(name, NULL, 0);
    for (char *line = text, *next; line; line = next) {
        next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        printf("  " ANSI_DIM "%s" ANSI_RESET "\n", line);
    }
}

int usage_url_query(struct provider *provider)
{
    const char *url = http_provider_usage_url(provider);
    char **auth_headers = http_provider_metadata_headers(provider);
    const char *fixed[] = {"Accept: text/plain", NULL};
    char **headers = string_array_concat(fixed, (const char *const *)auth_headers);
    string_array_free(auth_headers);

    char *body = NULL;
    char *text = NULL;
    long status = 0;
    struct busy *busy = busy_begin("fetching usage...");
    int request_result = http_get(url, (const char *const *)headers, USAGE_URL_TIMEOUT_S,
                                  USAGE_URL_MAX_BYTES, busy_tick, NULL, &body, &status);
    int cancelled = busy_end(busy);
    string_array_free(headers);

    int result = -1;
    if (cancelled)
        goto out;
    if (request_result != 0 || !body) {
        if (status == 401)
            ui_error("%s rejected the configured API key (401)", provider->name);
        else if (status > 0)
            ui_error("failed to fetch usage from %s (HTTP %ld)", url, status);
        else
            ui_error("failed to fetch usage from %s", url);
        goto out;
    }

    /* The body is server text: strip terminal controls before it reaches the screen. */
    text = ctrl_strip_dup(body);
    trim_trailing_space(text);
    if (*text) {
        print_report(provider->name, text);
        result = 0;
    } else {
        ui_error("usage response from %s is empty", url);
    }

out:
    free(text);
    free(body);
    return result;
}
