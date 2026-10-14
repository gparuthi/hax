/* SPDX-License-Identifier: MIT */
#ifndef HAX_IMAGE_ATTACH_H
#define HAX_IMAGE_ATTACH_H

#include <stddef.h>

#include "provider.h"

/* The read tool's byte cap: 5 MiB once base64-encoded. */
#define IMAGE_ATTACH_MAX_BYTES ((size_t)5 * 1024 * 1024 / 4 * 3)

/* Load `path` as an image part for a user message. The file must be a complete PNG, JPEG, GIF,
 * or WebP within IMAGE_ATTACH_MAX_BYTES; dimensions are left to the provider, which downscales
 * or rejects. On success fills `out` (owned fields) and returns 0; on failure returns -1 and sets
 * `*error` to an allocated message. */
int image_attach_load(const char *path, struct item_image *out, char **error);

#endif /* HAX_IMAGE_ATTACH_H */
