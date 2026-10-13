#!/usr/bin/env python3
"""Keys typed while a turn runs reach the next prompt as if typed there: an Enter submits once the
turn ends, a paste waits as a draft, and arrow keys are dropped. After an Esc, typed text waits as
a draft instead of submitting."""

import os
import time

import harness

SCRIPT = """
    tool bash {"command":"cat fifo"}
    end-turn

    text First done.
    end-turn

    text Second done.
    end-turn
"""


def busy() -> harness.Terminal:
    """Submit a prompt whose tool blocks on ./fifo, and wait until it runs."""
    term = harness.Terminal(SCRIPT)
    os.mkfifo(term.workdir / "fifo")
    term.wait_for("❯")
    term.type("go")
    term.send("Enter")
    term.wait_for("[bash] cat fifo")
    return term


def finish(term: harness.Terminal) -> None:
    """Unblock the tool once the keys sent so far have reached hax."""
    # Nothing on screen acknowledges keys typed during a turn.
    time.sleep(0.3)
    term.write_fifo(term.workdir / "fifo", "unblocked\n")


def test_enter_submits_after_turn():
    term = busy()
    term.type("next")
    term.send("Enter")
    finish(term)
    term.expect_screen(
        """
        ▌ hax › mock · mock-model
        ▌ ctrl-d quit · try /help

        ▌ go

        [bash] cat fifo

        First done.

        {*}s

        ▌ next

        Second done.

        {*}s

        ❯
        """
    )


def test_paste_waits_as_draft():
    term = busy()
    term.paste("line one\nline two")
    finish(term)
    term.expect_screen(
        """
        ▌ hax › mock · mock-model
        ▌ ctrl-d quit · try /help

        ▌ go

        [bash] cat fifo

        First done.

        {*}s

        ❯ line one
          line two
        """
    )


def test_arrow_keys_are_dropped():
    """Up would recall the previous prompt; the turn neither pauses nor leaves text behind."""
    term = busy()
    term.send("Up", "Left", "Down", "F1")
    finish(term)
    term.expect_screen(
        """
        ▌ hax › mock · mock-model
        ▌ ctrl-d quit · try /help

        ▌ go

        [bash] cat fifo

        First done.

        {*}s

        ❯
        """
    )


def test_escape_keeps_draft():
    term = busy()
    term.type("steer")
    term.send("Enter", "Escape")
    finish(term)
    term.expect_screen(
        """
        ▌ hax › mock · mock-model
        ▌ ctrl-d quit · try /help

        ▌ go

        [bash] cat fifo

        {*}s

        ❯ steer
        """
    )


harness.main(globals())
