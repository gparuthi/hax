/* SPDX-License-Identifier: MIT */
#include "tool_filter.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"
#include "config.h"
#include "xalloc.h"

/* Pull the next comma-separated entry off `*cursor`, trimmed of blanks; empty entries are
 * skipped. Returns the entry length, or 0 when the list is exhausted. */
static size_t next_entry(const char **cursor, const char **entry)
{
    const char *p = *cursor;
    while (p && *p) {
        const char *end = strchr(p, ',');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        const char *start = p;
        p = end ? end + 1 : NULL;
        while (n > 0 && isspace((unsigned char)*start)) {
            start++;
            n--;
        }
        while (n > 0 && isspace((unsigned char)start[n - 1]))
            n--;
        if (n > 0) {
            *cursor = p;
            *entry = start;
            return n;
        }
    }
    *cursor = NULL;
    return 0;
}

static int entry_is(const char *entry, size_t n, const char *name)
{
    return strlen(name) == n && strncmp(entry, name, n) == 0;
}

int tool_filter_allows(const char *name)
{
    const char *list = config_str("tools");
    if (!list)
        return 1;
    const char *entry;
    size_t n;
    while ((n = next_entry(&list, &entry)) > 0) {
        if (entry_is(entry, n, name))
            return 1;
    }
    return 0;
}

int tool_filter_validate(const char *list, const char *const *known, size_t n_known, char **error)
{
    *error = NULL;
    const char *entry;
    size_t n;
    size_t entries = 0;
    int has_none = 0;
    while ((n = next_entry(&list, &entry)) > 0) {
        entries++;
        if (entry_is(entry, n, "none")) {
            has_none = 1;
            continue;
        }
        int found = 0;
        for (size_t i = 0; i < n_known && !found; i++)
            found = entry_is(entry, n, known[i]);
        if (!found) {
            struct buf names;
            buf_init(&names);
            for (size_t i = 0; i < n_known; i++) {
                if (i > 0)
                    buf_append_str(&names, ", ");
                buf_append_str(&names, known[i]);
            }
            *error = xasprintf("unknown tool '%.*s' (known: %s; 'none' offers no tools)", (int)n,
                               entry, names.data ? names.data : "");
            buf_free(&names);
            return -1;
        }
    }
    if (entries == 0)
        *error = xstrdup("names no tool (use 'none' to offer no tools)");
    else if (has_none && entries > 1)
        *error = xstrdup("'none' cannot be combined with other tools");
    return *error ? -1 : 0;
}
