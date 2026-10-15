#!/usr/bin/env python3
"""One-shot mode (-p): what reaches stdout and stderr, the exit status, and resuming an
interrupted run."""

import json
import re
import signal
import tempfile
import time
from pathlib import Path

import harness

# The mock provider defaults to an unrecorded session; a session id and resuming need the file.
RECORD = {"HAX_NO_SESSION": "0"}


def test_text_and_banner():
    """The scripted assistant text reaches stdout and the start banner, on stderr, names the model
    and session."""
    result = harness.run_oneshot(
        "hi",
        """
        text Hello from mock
        end-turn
        """,
        extra_env=RECORD,
    )
    harness.expect(result.returncode == 0, "exit status is 0", result)
    harness.expect("Hello from mock" in result.stdout, "scripted text reaches stdout", result)

    banner = result.stderr.splitlines()[0]
    harness.expect("mock-model" in banner, "start banner names the model on stderr", result)
    # Announced before the run, so a run killed outright is still resumable.
    _, separator, session_id = banner.rpartition(" · session ")
    harness.expect(
        bool(separator and session_id.strip()), "start banner names the session id", result
    )


def test_tool_runs_in_workdir():
    """A scripted bash tool call executes in the scratch working directory."""
    result = harness.run_oneshot(
        "go",
        """
        text Running a command
        tool bash {"command":"echo marker42 > out.txt"}
        end-turn

        text Tool finished.
        end-turn
        """,
    )
    harness.expect(result.returncode == 0, "exit status is 0", result)

    out_file = result.workdir / "out.txt"
    harness.expect(out_file.exists(), "bash tool call created out.txt", result)
    harness.expect(out_file.read_text() == "marker42\n", "out.txt has the scripted content", result)
    harness.expect("Tool finished." in result.stdout, "final assistant text reaches stdout", result)


def test_json_stream():
    """--json turns stdout into a JSONL stream: a session record, conversation records in
    session-file schema, and a terminal result record."""
    result = harness.run_oneshot(
        "go",
        """
        text Running a command
        tool bash {"command":"echo marker42 > out.txt"}
        end-turn

        text Tool finished.
        end-turn
        """,
        extra_args=["--json"],
    )
    harness.expect(result.returncode == 0, "exit status is 0", result)

    lines = result.stdout.splitlines()
    harness.expect(len(lines) >= 3, "stream has session, item, and result records", result)
    records = []
    for line in lines:
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            harness.expect(False, f"stdout line is not JSON: {line!r}", result)

    session = records[0]
    harness.expect(session.get("type") == "session", "stream opens with a session record", result)
    harness.expect(session.get("model") == "mock-model", "session record names the model", result)

    kinds = [record.get("kind") for record in records]
    harness.expect("user" in kinds, "the prompt appears as a user record", result)
    tool_calls = [r for r in records if r.get("kind") == "tool_call"]
    harness.expect(
        any(r.get("tool_name") == "bash" for r in tool_calls),
        "the scripted bash call appears as a tool_call record",
        result,
    )
    harness.expect("tool_result" in kinds, "the tool result is streamed", result)
    harness.expect("turn_usage" in kinds, "per-turn usage records are streamed", result)

    final = records[-1]
    harness.expect(final.get("type") == "result", "stream closes with a result record", result)
    harness.expect(final.get("outcome") == "complete", "result reports completion", result)
    harness.expect(final.get("text") == "Tool finished.", "result carries the final text", result)
    harness.expect(final.get("turns") == 2, "result counts both model round-trips", result)

    out_file = result.workdir / "out.txt"
    harness.expect(out_file.exists(), "the tool call still executed", result)
    harness.expect(result.stderr == "", "no banner or stats duplicate the stream on stderr", result)


def advertised_tools(transcript):
    """Tool names in the transcript's model-facing tools section, which precedes the first turn."""
    text = transcript.read_text()
    if "── tools ──" not in text:
        return []
    section = text.split("── tools ──", 1)[1].split("# turn", 1)[0]
    return re.findall(r"^\[(\w+)\]$", section, re.MULTILINE)


def test_tools_flag_limits_offered_and_runnable_tools():
    """--tools offers only the listed tools: the model-facing transcript lists just those, and a
    scripted call to one left out is refused as unknown instead of running."""
    transcript = Path(tempfile.mkdtemp(prefix="transcript-")) / "t.txt"
    result = harness.run_oneshot(
        "go",
        """
        text Trying bash anyway
        tool bash {"command":"echo marker42 > out.txt"}
        end-turn

        text Done.
        end-turn
        """,
        extra_args=["--json", "--tools=read,write"],
        extra_env={"HAX_TRANSCRIPT": str(transcript)},
    )
    harness.expect(result.returncode == 0, "exit status is 0", result)
    harness.expect(
        not (result.workdir / "out.txt").exists(), "the withheld bash tool did not run", result
    )
    records = [json.loads(line) for line in result.stdout.splitlines()]
    outputs = [r.get("output", "") for r in records if r.get("kind") == "tool_result"]
    harness.expect(
        outputs == ["unknown tool: bash"], "the call comes back as an unknown tool", result
    )
    offered = advertised_tools(transcript)
    harness.expect(offered == ["read", "write"], f"only the listed tools are offered: {offered}", result)


def test_tools_none_offers_no_tools():
    """--tools=none keeps the prompt and context but advertises no tool at all."""
    transcript = Path(tempfile.mkdtemp(prefix="transcript-")) / "t.txt"
    result = harness.run_oneshot(
        "hi",
        """
        text Plain answer
        end-turn
        """,
        extra_args=["--tools=none"],
        extra_env={"HAX_TRANSCRIPT": str(transcript)},
    )
    harness.expect(result.returncode == 0, "exit status is 0", result)
    harness.expect("Plain answer" in result.stdout, "the turn completes", result)
    offered = advertised_tools(transcript)
    harness.expect(offered == [], f"no tool is advertised: {offered}", result)


def test_invalid_tools_stop_the_run():
    """A bad allowlist from the flag or the environment fails before any model call, rather than
    silently offering every tool."""
    flag = harness.run_oneshot(
        "hi", "text never sent\nend-turn\n", extra_args=["--tools=read,grep"]
    )
    harness.expect(flag.returncode != 0, "an unknown name in --tools fails the run", flag)
    harness.expect("unknown tool 'grep'" in flag.stderr, "the error names the tool", flag)
    harness.expect("never sent" not in flag.stdout, "no model call was made", flag)

    env = harness.run_oneshot(
        "hi", "text never sent\nend-turn\n", extra_env={"HAX_TOOLS": "raed"}
    )
    harness.expect(env.returncode != 0, "an unknown name in HAX_TOOLS fails the run", env)
    harness.expect("unknown tool 'raed'" in env.stderr, "the error names the tool", env)
    harness.expect("never sent" not in env.stdout, "no model call was made", env)


def test_sigint_then_resume():
    """SIGINT interrupts a run gracefully: the in-flight stream is cancelled, partial output is kept
    and marked in the recorded session, the --json stream still closes with a result record, and
    the exit status is 130. A promptless --resume then speaks for the user and completes."""
    script = """
        text Partial answer before the interrupt.
        delay 30000
        text Never delivered
        end-turn
    """
    home, workdir = harness.make_home()
    proc = harness.spawn_hax(["--json", "go"], script, home, workdir, extra_env=RECORD)

    lines = []
    for line in proc.stdout:
        lines.append(line)
        if json.loads(line).get("kind") == "user":
            break
    # The user record precedes the provider call; the script streams its first
    # sentence immediately and then stalls for seconds, so this lands mid-stream.
    time.sleep(0.5)
    proc.send_signal(signal.SIGINT)
    out, err = proc.communicate(timeout=20)
    result = harness.spawned_result(proc, "".join(lines) + out, err, workdir)

    harness.expect(result.returncode == 130, "a graceful interrupt exits 130", result)
    records = [json.loads(line) for line in result.stdout.splitlines()]
    # Announced before the run, so a killed one is resumable without reaching a result record.
    harness.expect(bool(records[0].get("id")), "the opening session record carries the id", result)
    final = records[-1]
    harness.expect(final.get("type") == "result", "the stream still closes with a result", result)
    harness.expect(
        final.get("outcome") == "interrupted", "the result reports the interrupt", result
    )
    harness.expect(
        any(
            record.get("kind") == "assistant"
            and record.get("origin") == "interrupted"
            and "Partial answer" in record.get("text", "")
            for record in records
        ),
        "partial text is kept and marked interrupted",
        result,
    )
    session_id = final.get("session_id")
    harness.expect(bool(session_id), "the interrupted run is resumable", result)

    resume = harness.spawn_hax(
        ["--json", f"--resume={session_id}"],
        """
        text Resumed answer.
        end-turn
        """,
        home,
        workdir,
        extra_env=RECORD,
    )
    out, err = resume.communicate(timeout=30)
    result = harness.spawned_result(resume, out, err, workdir)

    harness.expect(result.returncode == 0, "the promptless resume completes", result)
    records = [json.loads(line) for line in result.stdout.splitlines()]
    harness.expect(
        any(
            record.get("kind") == "user" and record.get("origin") == "continuation"
            for record in records
        ),
        "the marked tail gets a continuation record",
        result,
    )
    harness.expect(records[-1].get("outcome") == "complete", "the resumed result completes", result)


harness.main(globals())
