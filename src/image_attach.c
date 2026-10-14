/* SPDX-License-Identifier: MIT */
#include "image_attach.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"
#include "system/fs.h"
#include "text/base64.h"
#include "tools/image_sniff.h"

int image_attach_load(const char *path, struct item_image *out, char **error)
{
    memset(out, 0, sizeof(*out));
    *error = NULL;

    size_t len = 0;
    int truncated = 0;
    char *data = fs_read_file_capped(path, IMAGE_ATTACH_MAX_BYTES, &len, &truncated);
    if (!data) {
        *error = xasprintf("cannot read image %s: %s", path, strerror(errno));
        return -1;
    }
    if (truncated) {
        free(data);
        *error = xasprintf("image %s is over %zu bytes", path, IMAGE_ATTACH_MAX_BYTES);
        return -1;
    }

    struct image_info info;
    if (!image_sniff(data, len, &info)) {
        free(data);
        *error = xasprintf("%s is not a PNG, JPEG, GIF, or WebP image", path);
        return -1;
    }
    /* An undecodable attachment would fail the request rather than degrade it. */
    if (info.width <= 0 || info.height <= 0 || !info.complete) {
        free(data);
        *error = xasprintf("%s looks like %s but is truncated or malformed", path, info.mime);
        return -1;
    }

    out->mime = xstrdup(info.mime);
    out->data_b64 = base64_encode(data, len, NULL);
    out->width = info.width;
    out->height = info.height;
    free(data);
    return 0;
}
