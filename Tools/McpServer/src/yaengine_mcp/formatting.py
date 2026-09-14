"""Compact text rendering for tool results and the smoke script."""

import json
import re
from typing import Any

# Keeps one tool result within what a client context comfortably takes.
MAX_TEXT_CHARS = 60_000


def to_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(", ", ": "), default=str)


def capped_lines(head: list, body: list, hint: str, *, keep_newest: bool = False, limit: int = MAX_TEXT_CHARS) -> str:
    """head and body joined by newlines. Past limit characters, body lines are left out (the oldest with
    keep_newest, otherwise the last) and one line says how many and how to ask for less."""
    text = "\n".join([*head, *body])
    if len(text) <= limit:
        return text

    budget = limit - len("\n".join(head)) - len(_truncation_notice(len(body), hint, keep_newest, limit)) - 1
    kept, used = [], 0
    for line in reversed(body) if keep_newest else body:
        used += len(line) + 1
        if used > budget:
            break
        kept.append(line)
    if keep_newest:
        kept.reverse()

    notice = _truncation_notice(len(body) - len(kept), hint, keep_newest, limit)
    return "\n".join([*head, notice, *kept] if keep_newest else [*head, *kept, notice])


def _truncation_notice(omitted: int, hint: str, older: bool, limit: int) -> str:
    which = "older lines" if older else "more lines"
    return f"truncated: {omitted} {which} not shown to stay under {limit} characters - {hint}"


def format_log_tail(result: dict) -> str:
    """Renders a log.tail result as one line per entry: '<seq> <level> [<tag>] <text> (<file>:<line>)'."""
    entries = result.get("lines") or []
    lines = []
    for entry in entries:
        tag = entry.get("tag")
        source = re.split(r"[\\/]", entry.get("file") or "")[-1]
        where = f" ({source}:{entry.get('line')})" if source else ""
        tag_text = f" [{tag}]" if tag else ""
        lines.append(f"{entry.get('seq')} {entry.get('level')}{tag_text} {entry.get('text')}{where}")
    head = [f"lastSeq={result.get('lastSeq')} lines={len(entries)}"]
    return capped_lines(head, lines, "ask for fewer lines (count) or a higher min_level", keep_newest=True)
