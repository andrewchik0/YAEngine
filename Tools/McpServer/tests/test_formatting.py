from yaengine_mcp.formatting import MAX_TEXT_CHARS, capped_lines, format_log_tail


def test_text_under_the_cap_is_unchanged():
    assert capped_lines(["head"], ["a", "b"], "ask for less") == "head\na\nb"


def test_cap_keeps_the_first_lines_and_ends_with_the_notice():
    body = ["x" * 10] * 20
    assert len("\n".join(["h", *body])) > 150

    text = capped_lines(["h"], body, "ask for less", limit=150)

    lines = text.splitlines()
    assert len(text) <= 150
    assert lines[:7] == ["h", *body[:6]]
    assert lines[-1] == "truncated: 14 more lines not shown to stay under 150 characters - ask for less"
    assert len(lines) == 8


def test_log_tail_keeps_the_newest_lines_under_the_cap():
    entries = [
        {"seq": seq, "level": "info", "tag": "Render", "file": "C:\\src\\Render.cpp", "line": 7, "text": "y" * 100}
        for seq in range(1, 2001)
    ]

    text = format_log_tail({"lines": entries, "lastSeq": 2000})

    lines = text.splitlines()
    assert len(text) <= MAX_TEXT_CHARS
    assert lines[0] == "lastSeq=2000 lines=2000"
    shown = len(lines) - 2
    assert lines[1] == (
        f"truncated: {2000 - shown} older lines not shown to stay under {MAX_TEXT_CHARS} characters - "
        "ask for fewer lines (count) or a higher min_level"
    )
    assert lines[2].startswith(f"{2001 - shown} info [Render] ")
    assert lines[-1].startswith("2000 info [Render] ") and lines[-1].endswith("(Render.cpp:7)")
