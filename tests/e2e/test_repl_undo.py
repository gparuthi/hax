#!/usr/bin/env python3
"""Ctrl-_ undoes prompt edits, and each prompt starts with nothing to undo. How edits group into
undo steps is pinned by the input_core unit tests; these check the key and the reset."""

import harness


def test_undo_then_reset_after_submit():
    term = harness.Terminal(
        """
        text Hello from mock
        end-turn
        """
    )
    term.wait_for("❯")
    term.type("hi there")
    term.send("C-u")
    term.send("C-_")
    term.expect_screen(
        """
        ▌ hax › mock · mock-model
        ▌ ctrl-d quit · try /help

        ❯ hi there
        """
    )
    term.send("C-_")
    term.expect_screen(
        """
        ▌ hax › mock · mock-model
        ▌ ctrl-d quit · try /help

        ❯ hi
        """
    )

    term.send("Enter")
    term.expect_screen(
        """
        ▌ hax › mock · mock-model
        ▌ ctrl-d quit · try /help

        ▌ hi

        Hello from mock

        {*}s

        ❯
        """
    )
    term.send("C-_")
    term.type("x")
    term.expect_screen(
        """
        ▌ hax › mock · mock-model
        ▌ ctrl-d quit · try /help

        ▌ hi

        Hello from mock

        {*}s

        ❯ x
        """
    )


harness.main(globals())
