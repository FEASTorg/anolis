#!/usr/bin/env python3
"""
Summarize clang-tidy logs with actionable buckets.
Outputs per-check, per-file, most common messages, and file+check hot spots.
`n` controls how many top entries to show in each category (default 10).

Usage:
  python scripts/summarize_clang_tidy.py [-n TOP] [-o output.log] path/to/clang-tidy.log
Example:
    python scripts/summarize_clang_tidy.py -n 50 -o clang-tidy-summary.log clang-tidy.log
"""

from __future__ import annotations

import argparse
import re
from collections import Counter
from pathlib import Path
from typing import List

ISSUE_RE = re.compile(
    r"^(?P<file>/[^:]+):(?P<line>\d+):(?P<col>\d+): (warning|error): (?P<msg>.*) \[(?P<check>[^\]]+)\]"
)


def parse_issues(text: str):
    for line in text.splitlines():
        m = ISSUE_RE.match(line)
        if m:
            yield m.groupdict()


def format_summary(path: Path, top_n: int) -> List[str]:
    data = list(parse_issues(path.read_text()))
    total = len(data)
    by_check = Counter(item["check"] for item in data)
    by_file = Counter(item["file"] for item in data)
    by_msg = Counter((item["check"], item["msg"]) for item in data)
    by_file_check = Counter((item["file"], item["check"]) for item in data)

    lines: List[str] = []
    lines.append(f"== {path} ==")
    lines.append(f"Total issues: {total}")

    lines.append("Top checks:")
    for check, count in by_check.most_common(top_n):
        lines.append(f" {count:5} {check}")

    lines.append("Top files:")
    for file, count in by_file.most_common(top_n):
        lines.append(f" {count:5} {file}")

    lines.append("Top messages (check: message):")
    for (check, msg), count in by_msg.most_common(top_n):
        lines.append(f" {count:5} {check}: {msg}")

    lines.append("Top file+check hot spots:")
    for (file, check), count in by_file_check.most_common(top_n):
        lines.append(f" {count:5} {file} :: {check}")

    return lines


def summarize(path: Path, top_n: int, output: Path | None) -> None:
    lines = format_summary(path, top_n)
    text = "\n".join(lines) + "\n"
    if output:
        output.write_text(text)
    else:
        print(text, end="")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=Path, help="clang-tidy log path")
    ap.add_argument("-n", "--top", type=int, default=10, help="top N entries to show")
    ap.add_argument("-o", "--output", type=Path, help="write summary to file instead of stdout")
    args = ap.parse_args()

    summarize(args.log, args.top, args.output)


if __name__ == "__main__":
    main()
