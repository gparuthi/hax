/* SPDX-License-Identifier: MIT */
#ifndef HAX_PROVIDERS_USAGE_URL_H
#define HAX_PROVIDERS_USAGE_URL_H

#include "provider.h"

/* The /usage report of a custom provider that sets providers.<id>.usage_url: a plain-text document
 * the endpoint serves, printed as is. */

/* Resolve a usage_url setting: an absolute http(s) URL is used verbatim, anything else is a path
 * relative to `base_url`. Returns an owned string. */
char *usage_url_resolve(const char *base_url, const char *usage_url);

/* query_usage hook for an http_provider with a usage URL: GET it with the provider's metadata
 * authentication and extra headers, then print the text body under the provider's name. */
int usage_url_query(struct provider *provider);

#endif /* HAX_PROVIDERS_USAGE_URL_H */
