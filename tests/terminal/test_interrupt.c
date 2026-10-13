/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "buf.h"
#include "harness.h"
#include "terminal/interrupt.h"

static int feed(struct interrupt_classifier *classifier, const char *bytes)
{
    int escapes = 0;
    size_t length = strlen(bytes);
    for (size_t i = 0; i < length; i++)
        escapes += interrupt_classifier_feed(classifier, (unsigned char)bytes[i]);
    return escapes;
}

static void test_text_is_ignored(void)
{
    struct interrupt_classifier classifier;
    interrupt_classifier_init(&classifier);

    EXPECT(feed(&classifier, "hello world\n\t!@#") == 0);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_IDLE);
    EXPECT(interrupt_classifier_timeout(&classifier) == 0);
}

static void test_timeout_confirms_bare_escape(void)
{
    struct interrupt_classifier classifier;
    interrupt_classifier_init(&classifier);

    EXPECT(feed(&classifier, "\x1b") == 0);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_ESCAPE_PENDING);
    EXPECT(interrupt_classifier_timeout(&classifier) == 1);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_IDLE);
    EXPECT(interrupt_classifier_timeout(&classifier) == 0);
}

static void test_csi_arrow_keys_are_ignored(void)
{
    struct interrupt_classifier classifier;
    interrupt_classifier_init(&classifier);

    EXPECT(feed(&classifier, "\x1b[A") == 0);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_IDLE);
    EXPECT(feed(&classifier, "\x1b[B\x1b[C\x1b[D") == 0);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_IDLE);
}

static void test_csi_parameters_are_ignored(void)
{
    struct interrupt_classifier classifier;
    interrupt_classifier_init(&classifier);

    EXPECT(feed(&classifier, "\x1b[1;5C") == 0);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_IDLE);
    EXPECT(feed(&classifier, "\x1b[200~") == 0);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_IDLE);
}

static void test_ss3_function_keys_are_ignored(void)
{
    struct interrupt_classifier classifier;
    interrupt_classifier_init(&classifier);

    EXPECT(feed(&classifier, "\x1bOP") == 0);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_IDLE);
    EXPECT(feed(&classifier, "\x1bOQ\x1bOR\x1bOS") == 0);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_IDLE);
}

static void test_non_sequence_byte_confirms_escape(void)
{
    struct interrupt_classifier classifier;
    interrupt_classifier_init(&classifier);

    EXPECT(feed(&classifier, "\x1b"
                             "a") == 1);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_IDLE);
}

static void test_repeated_escape_confirms_each_press(void)
{
    struct interrupt_classifier classifier;
    interrupt_classifier_init(&classifier);

    EXPECT(feed(&classifier, "\x1b\x1b") == 1);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_ESCAPE_PENDING);
    EXPECT(interrupt_classifier_timeout(&classifier) == 1);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_IDLE);
}

static void test_escape_after_text_is_confirmed(void)
{
    struct interrupt_classifier classifier;
    interrupt_classifier_init(&classifier);

    EXPECT(feed(&classifier, "abc\x1b") == 0);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_ESCAPE_PENDING);
    EXPECT(interrupt_classifier_timeout(&classifier) == 1);
}

static void test_control_byte_ends_truncated_csi(void)
{
    struct interrupt_classifier classifier;
    interrupt_classifier_init(&classifier);

    EXPECT(feed(&classifier, "\x1b[1\x07") == 0);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_IDLE);
}

static void test_reset_discards_pending_escape(void)
{
    struct interrupt_classifier classifier;
    interrupt_classifier_init(&classifier);
    EXPECT(feed(&classifier, "\x1b") == 0);
    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_ESCAPE_PENDING);

    interrupt_classifier_init(&classifier);

    EXPECT(classifier.state == INTERRUPT_CLASSIFIER_IDLE);
    EXPECT(interrupt_classifier_timeout(&classifier) == 0);
}

/* Feed `bytes` and expect `kept` as type-ahead along with `escapes` bare Escs. */
static void expect_typeahead(struct interrupt_typeahead_filter *filter, const char *bytes,
                             const char *kept, int escapes)
{
    struct buf out;
    buf_init(&out);
    int bare_escapes =
        interrupt_typeahead_filter_feed(filter, (const unsigned char *)bytes, strlen(bytes), &out);
    EXPECT(bare_escapes == escapes);
    EXPECT(out.len == strlen(kept));
    EXPECT(out.len == 0 || memcmp(out.data, kept, out.len) == 0);
    buf_free(&out);
}

static void test_typeahead_keeps_text_and_enters(void)
{
    struct interrupt_typeahead_filter filter;
    interrupt_typeahead_filter_init(&filter);

    expect_typeahead(&filter, "next\rline\nmore\x7f\t", "next\rline\nmore\x7f\t", 0);
}

static void test_typeahead_drops_navigation_keys(void)
{
    struct interrupt_typeahead_filter filter;
    interrupt_typeahead_filter_init(&filter);

    expect_typeahead(&filter, "a\x1b[Ab\x1bOPc\x1b[1;5Dd\x1b[202~", "abcd", 0);
    expect_typeahead(&filter, "\x1b[1;2;3;4;5;6;7m", "", 0);
}

static void test_typeahead_drops_bare_escape(void)
{
    struct interrupt_typeahead_filter filter;
    interrupt_typeahead_filter_init(&filter);

    /* The byte that shows an Esc was bare is still typed text. */
    expect_typeahead(&filter, "\x1bx", "x", 1);
    expect_typeahead(&filter, "\x1b\x1b[A", "", 1);
    expect_typeahead(&filter, "\x1b", "", 0);
    EXPECT(interrupt_classifier_timeout(&filter.classifier) == 1);
}

static void test_typeahead_sequence_spans_reads(void)
{
    struct interrupt_typeahead_filter filter;
    interrupt_typeahead_filter_init(&filter);

    expect_typeahead(&filter, "\x1b", "", 0);
    expect_typeahead(&filter, "[B", "", 0);
    expect_typeahead(&filter, "\x1b[20", "", 0);
    expect_typeahead(&filter, "0~x\x1b[2", "\x1b[200~x\x1b[2", 0);
    expect_typeahead(&filter, "01~y", "01~y", 0);
    EXPECT(!filter.in_paste);
}

static void test_typeahead_keeps_bracketed_paste(void)
{
    struct interrupt_typeahead_filter filter;
    interrupt_typeahead_filter_init(&filter);

    /* An Esc inside a paste is pasted text, not an interrupt. */
    expect_typeahead(&filter, "\x1b[200~a\rb\x1b\x1b[2c\x1b[201~",
                     "\x1b[200~a\rb\x1b\x1b[2c\x1b[201~", 0);
    expect_typeahead(&filter, "\x1b[Ad\x1b", "d", 0);
    EXPECT(filter.classifier.state == INTERRUPT_CLASSIFIER_ESCAPE_PENDING);
}

int main(void)
{
    test_text_is_ignored();
    test_timeout_confirms_bare_escape();
    test_csi_arrow_keys_are_ignored();
    test_csi_parameters_are_ignored();
    test_ss3_function_keys_are_ignored();
    test_non_sequence_byte_confirms_escape();
    test_repeated_escape_confirms_each_press();
    test_escape_after_text_is_confirmed();
    test_control_byte_ends_truncated_csi();
    test_reset_discards_pending_escape();
    test_typeahead_keeps_text_and_enters();
    test_typeahead_drops_navigation_keys();
    test_typeahead_drops_bare_escape();
    test_typeahead_sequence_spans_reads();
    test_typeahead_keeps_bracketed_paste();
    T_REPORT();
}
