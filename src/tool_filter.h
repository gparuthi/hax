/* SPDX-License-Identifier: MIT */
#ifndef HAX_TOOL_FILTER_H
#define HAX_TOOL_FILTER_H

#include <stddef.h>

/* The `tools` allowlist (--tools / HAX_TOOLS / config.json): a comma-separated list of tool
 * names, or "none" for no tools. Unset offers every tool. It trims what the model is offered and
 * may call; it is not a security boundary while bash is allowed. */

/* Report whether the configured allowlist offers `name`. */
int tool_filter_allows(const char *name);

/* Check an allowlist value against the `n_known` tool names in `known`. Returns 0 when valid;
 * otherwise -1 with a malloc'd message in `*error`. */
int tool_filter_validate(const char *list, const char *const *known, size_t n_known, char **error);

#endif /* HAX_TOOL_FILTER_H */
