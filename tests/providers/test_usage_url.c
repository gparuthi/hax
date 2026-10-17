/* SPDX-License-Identifier: MIT */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "harness.h"
#include "loopback.h"
#include "output.h"
#include "provider.h"
#include "providers/http_provider.h"
#include "providers/registry.h"
#include "providers/usage_url.h"

static void expect_resolved(const char *base_url, const char *usage_url, const char *expected)
{
    char *resolved = usage_url_resolve(base_url, usage_url);
    EXPECT_STR_EQ(resolved, expected);
    free(resolved);
}

static void test_resolve(void)
{
    expect_resolved("http://h/v1", "usage", "http://h/v1/usage");
    expect_resolved("http://h/v1", "/usage", "http://h/v1/usage");
    expect_resolved("http://h/v1/", "usage?x=1", "http://h/v1/usage?x=1");
    expect_resolved("http://h/v1", "http://other/u", "http://other/u");
    expect_resolved("http://h/v1", "HTTPS://other/u", "HTTPS://other/u");
}

static void run_query(void *user)
{
    struct provider *provider = user;
    provider->query_usage(provider);
}

/* Serve one scripted reply, query /usage against it, and return what was printed. The request the
 * server read is left in `server->requests`. */
static char *query_with_reply(struct loopback *server, const char *config_template)
{
    int port = loopback_start(server);
    EXPECT(port > 0);
    if (port <= 0)
        return NULL;

    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d/v1", port);
    char config[512];
    snprintf(config, sizeof(config), config_template, base_url);
    EXPECT(config_load(config) == 0);

    struct provider_def def = {.id = "up", .display_name = "Up"};
    struct provider *provider = http_provider_new(&def);
    EXPECT(provider != NULL);
    char *output = NULL;
    if (provider) {
        EXPECT(provider->query_usage != NULL);
        if (provider->query_usage)
            output = t_capture_stdout(run_query, provider);
        provider->destroy(provider);
    }
    loopback_stop(server);
    EXPECT(config_load(NULL) == 0);
    return output;
}

static void test_prints_text_report(void)
{
    struct loopback server = {.n_requests = 1};
    loopback_reply_ok(&server, 0, "spent today: $1.20\n\x1b[31mlimit\x1b[0m: $5\n\n  \n");
    char *output = query_with_reply(
        &server, "{\"providers\": {\"up\": {\"base_url\": \"%s\", \"api_key\": \"sk-test\","
                 " \"usage_url\": \"usage\", \"extra_headers\": {\"x-team\": \"blue\"}}}}");
    EXPECT(output != NULL);
    if (output) {
        char *plain = t_strip_sgr(output);
        EXPECT_STR_EQ(plain, "Up\n  spent today: $1.20\n  limit: $5\n");
        free(plain);
        EXPECT(strstr(output, "[31m") == NULL);
    }
    free(output);

    EXPECT(strncmp(server.requests[0], "GET /v1/usage ", 14) == 0);
    EXPECT(strstr(server.requests[0], "Authorization: Bearer sk-test\r\n") != NULL);
    EXPECT(strstr(server.requests[0], "x-team: blue\r\n") != NULL);
    EXPECT(strstr(server.requests[0], "Accept: text/plain\r\n") != NULL);
}

static void test_http_error_is_one_line(void)
{
    struct loopback server = {
        .response = "HTTP/1.1 404 Not Found\r\nContent-Length: 4\r\nConnection: close\r\n\r\nnope",
        .n_requests = 1,
    };
    char *output = query_with_reply(
        &server, "{\"providers\": {\"up\": {\"base_url\": \"%s\", \"usage_url\": \"usage\"}}}");
    EXPECT(output != NULL);
    if (output) {
        char *plain = t_strip_sgr(output);
        EXPECT(strstr(plain, "failed to fetch usage from http://127.0.0.1:") == plain);
        EXPECT(strstr(plain, "/v1/usage (HTTP 404)\n") != NULL);
        EXPECT(strchr(plain, '\n') == plain + strlen(plain) - 1);
        free(plain);
    }
    free(output);
}

static void test_unauthorized_names_the_key(void)
{
    struct loopback server = {
        .response = "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
        .n_requests = 1,
    };
    char *output = query_with_reply(
        &server, "{\"providers\": {\"up\": {\"base_url\": \"%s\", \"usage_url\": \"usage\"}}}");
    EXPECT(output != NULL);
    if (output) {
        char *plain = t_strip_sgr(output);
        EXPECT_STR_EQ(plain, "Up rejected the configured API key (401)\n");
        free(plain);
    }
    free(output);
}

static void test_blank_report_is_an_error(void)
{
    struct loopback server = {.n_requests = 1};
    loopback_reply_ok(&server, 0, " \n\n");
    char *output = query_with_reply(
        &server, "{\"providers\": {\"up\": {\"base_url\": \"%s\", \"usage_url\": \"usage\"}}}");
    EXPECT(output != NULL);
    if (output) {
        char *plain = t_strip_sgr(output);
        EXPECT(strstr(plain, "usage response from ") == plain);
        EXPECT(strstr(plain, " is empty\n") != NULL);
        free(plain);
    }
    free(output);
}

int main(void)
{
    test_resolve();
    test_prints_text_report();
    test_http_error_is_one_line();
    test_unauthorized_names_the_key();
    test_blank_report_is_an_error();
    T_REPORT();
}
