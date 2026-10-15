/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "tool_filter.h"

static const char *const KNOWN[] = {"read", "edit", "write", "bash", "task_wait"};
#define N_KNOWN (sizeof(KNOWN) / sizeof(KNOWN[0]))

static void expect_valid(const char *list)
{
    char *error = NULL;
    EXPECT(tool_filter_validate(list, KNOWN, N_KNOWN, &error) == 0);
    EXPECT(error == NULL);
    free(error);
}

static void expect_invalid(const char *list, const char *message_part)
{
    char *error = NULL;
    EXPECT(tool_filter_validate(list, KNOWN, N_KNOWN, &error) == -1);
    EXPECT(error != NULL);
    if (error)
        EXPECT(strstr(error, message_part) != NULL);
    free(error);
}

static void test_validate_accepts_known_names_and_none(void)
{
    expect_valid("read");
    expect_valid("read,bash");
    expect_valid(" read , write ,, task_wait ");
    expect_valid("none");
}

static void test_validate_rejects_bad_lists(void)
{
    expect_invalid("raed", "unknown tool 'raed'");
    expect_invalid("read,grep", "unknown tool 'grep'");
    expect_invalid("READ", "unknown tool 'READ'");
    expect_invalid("read,none", "cannot be combined");
    expect_invalid("", "names no tool");
    expect_invalid(" , ", "names no tool");
    /* The message lists what is valid so a typo is fixable from the error alone. */
    expect_invalid("nope", "task_wait");
}

static void test_allows_follows_the_setting(void)
{
    unsetenv("HAX_TOOLS");
    EXPECT(tool_filter_allows("bash") == 1);
    EXPECT(tool_filter_allows("read") == 1);

    /* An empty value counts as unset, never as "no tools". */
    setenv("HAX_TOOLS", "", 1);
    EXPECT(tool_filter_allows("bash") == 1);

    setenv("HAX_TOOLS", "read, write", 1);
    EXPECT(tool_filter_allows("read") == 1);
    EXPECT(tool_filter_allows("write") == 1);
    EXPECT(tool_filter_allows("bash") == 0);
    EXPECT(tool_filter_allows("edit") == 0);
    /* Whole names only: a prefix or superstring is not a match. */
    EXPECT(tool_filter_allows("rea") == 0);
    EXPECT(tool_filter_allows("reader") == 0);

    setenv("HAX_TOOLS", "none", 1);
    EXPECT(tool_filter_allows("read") == 0);
    EXPECT(tool_filter_allows("none") == 1);

    unsetenv("HAX_TOOLS");
}

int main(void)
{
    test_validate_accepts_known_names_and_none();
    test_validate_rejects_bad_lists();
    test_allows_follows_the_setting();
    T_REPORT();
}
