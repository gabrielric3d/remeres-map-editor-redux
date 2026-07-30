"""Remove blank lines and convert pseudo-comments (`-- text --`) to real
XML comments (`<!-- text -->`) in brush XML files.

Preserves original line endings (CRLF on Windows) and leading indentation
of the converted comment lines.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

PSEUDO_COMMENT_RE = re.compile(r"^([ \t]*)--\s*(.*?)\s*--[ \t]*$")
WHITESPACE_ONLY_RE = re.compile(r"^[ \t]*$")


def fix_file(path: Path) -> tuple[int, int]:
    """Return (blank_lines_removed, pseudo_comments_converted)."""
    raw = path.read_bytes()
    # Detect line ending: CRLF if any \r\n present, else LF.
    eol = b"\r\n" if b"\r\n" in raw else b"\n"
    text = raw.decode("utf-8")
    lines = text.splitlines()

    out: list[str] = []
    removed = 0
    converted = 0
    in_comment = False  # tracks multi-line XML comments

    for line in lines:
        # Track XML comment state across the line.
        scan = line
        opens_unclosed = False
        if in_comment:
            end_idx = scan.find("-->")
            if end_idx == -1:
                # Whole line is inside a multi-line comment.
                if WHITESPACE_ONLY_RE.match(line):
                    removed += 1
                    continue
                out.append(line)
                continue
            # Comment ends on this line; continue scanning after it.
            scan = scan[end_idx + 3 :]
            in_comment = False

        # Walk remaining segments to detect any new <!-- that doesn't close.
        i = 0
        while True:
            j = scan.find("<!--", i)
            if j == -1:
                break
            k = scan.find("-->", j + 4)
            if k == -1:
                opens_unclosed = True
                break
            i = k + 3

        line_is_in_comment = in_comment or opens_unclosed
        if opens_unclosed:
            in_comment = True

        if WHITESPACE_ONLY_RE.match(line):
            removed += 1
            continue
        if not line_is_in_comment:
            m = PSEUDO_COMMENT_RE.match(line)
            if m:
                indent, body = m.group(1), m.group(2)
                out.append(f"{indent}<!-- {body} -->")
                converted += 1
                continue
        out.append(line)

    new_text = eol.decode("utf-8").join(out)
    # Preserve trailing newline if original had one.
    if raw.endswith(b"\r\n") or raw.endswith(b"\n"):
        new_text += eol.decode("utf-8")

    path.write_bytes(new_text.encode("utf-8"))
    return removed, converted


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: fix_xml_lines.py <file> [<file> ...]", file=sys.stderr)
        return 2
    for arg in argv[1:]:
        p = Path(arg)
        removed, converted = fix_file(p)
        print(f"{p}: removed {removed} blank lines, converted {converted} pseudo-comments")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
