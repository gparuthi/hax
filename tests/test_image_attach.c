/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "image_attach.h"
#include "xalloc.h"

/* The 2x3 RGB PNG from tests/tools/test_read.c: a complete, decodable container. */
static const unsigned char TINY_PNG[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x08, 0x02, 0x00, 0x00, 0x00, 0x36, 0x88, 0x49,
    0xd6, 0x00, 0x00, 0x00, 0x15, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x60, 0x80, 0x00, 0x8d,
    0x80, 0x0a, 0x20, 0x62, 0x08, 0x58, 0xf0, 0x01, 0x88, 0x00, 0x1f, 0x05, 0x05, 0xa1, 0xfc, 0xf8,
    0x4b, 0x42, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static char *write_file(const char *name, const void *data, size_t len)
{
    char *path = xasprintf("%s/%s", t_tempdir(), name);
    FILE *f = fopen(path, "wb");
    EXPECT(f != NULL);
    if (f) {
        EXPECT(fwrite(data, 1, len, f) == len);
        fclose(f);
    }
    return path;
}

static void test_loads_complete_image(void)
{
    char *path = write_file("page.png", TINY_PNG, sizeof(TINY_PNG));
    struct item_image image;
    char *error = NULL;
    EXPECT(image_attach_load(path, &image, &error) == 0);
    EXPECT(error == NULL);
    EXPECT_STR_EQ(image.mime, "image/png");
    EXPECT(image.width == 2 && image.height == 3);
    EXPECT(image.data_b64 != NULL && strncmp(image.data_b64, "iVBORw0KGgo", 11) == 0);
    free(image.mime);
    free(image.data_b64);
    free(path);
}

static void test_rejects_bad_input(void)
{
    struct item_image image;
    char *error = NULL;

    EXPECT(image_attach_load("/nonexistent/hax-image.png", &image, &error) == -1);
    EXPECT(error && strstr(error, "cannot read image") != NULL);
    EXPECT(image.data_b64 == NULL);
    free(error);

    char *text = write_file("notes.txt", "hello", 5);
    EXPECT(image_attach_load(text, &image, &error) == -1);
    EXPECT(error && strstr(error, "is not a PNG") != NULL);
    free(error);
    free(text);

    char *cut = write_file("cut.png", TINY_PNG, 40);
    EXPECT(image_attach_load(cut, &image, &error) == -1);
    EXPECT(error && strstr(error, "truncated or malformed") != NULL);
    free(error);
    free(cut);
}

int main(void)
{
    test_loads_complete_image();
    test_rejects_bad_input();
    T_REPORT();
}
