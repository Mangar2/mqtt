#!/usr/bin/env python3
from __future__ import annotations

"""
Find free functions (not class/struct members) that share the same name
across more than one C++ source file.

This is a heuristic brace-depth scanner, not a full C++ parser. It handles
the common cases in this codebase (free functions, static file-local
helpers, functions inside namespaces, simple templates and qualifiers) but
can misjudge unusual formatting, macros, or complex declarations. Treat the
report as a discovery aid, not a strict gate.

Usage
-----
    python test/check_duplicate_functions.py
    python test/check_duplicate_functions.py --root src/yaha
"""

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

TEST_DIR = Path(__file__).parent.resolve()
PROJECT_ROOT = TEST_DIR.parent

SOURCE_EXTENSIONS = {
    ".c", ".cc", ".cpp", ".cxx",
    ".h", ".hh", ".hpp", ".hxx",
    ".ipp", ".inl", ".tpp",
}

# Keywords that can appear as "identifier(...) {" but are not function
# definitions (control-flow statements).
CONTROL_KEYWORDS = {
    "if", "for", "while", "switch", "catch", "else", "do",
    "return", "sizeof", "static_assert", "decltype", "typeid",
    "co_await", "co_yield", "co_return",
    "constexpr", "consteval",
}

CLASS_HEAD_RE = re.compile(r"^\s*(template\s*<.*>\s*)?(class|struct|union)\b", re.DOTALL)
NAMESPACE_HEAD_RE = re.compile(r"^\s*(inline\s+)?namespace\b")

TRAILING_QUALIFIER_RE = re.compile(
    r"(\bconst\b|\boverride\b|\bfinal\b|\bnoexcept\s*(\([^()]*\))?|=\s*0|=\s*default|=\s*delete)\s*$"
)

IDENTIFIER_TAIL_RE = re.compile(r"(~?[A-Za-z_]\w*)\s*$")


def _strip_comments_and_strings(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)

    def _blank(match: re.Match) -> str:
        body = match.group(0)
        return body[0] + " " * (len(body) - 2) + body[-1]

    text = re.sub(r'"(?:\\.|[^"\\])*"', _blank, text)
    text = re.sub(r"'(?:\\.|[^'\\])*'", _blank, text)
    return text


def _has_top_level_colon(text: str) -> bool:
    depth = 0
    idx = 0
    length = len(text)
    while idx < length:
        ch = text[idx]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == ":" and depth == 0:
            if idx + 1 < length and text[idx + 1] == ":":
                idx += 2
                continue
            if idx > 0 and text[idx - 1] == ":":
                idx += 1
                continue
            return True
        idx += 1
    return False


def _find_matching_open_paren(text: str, close_idx: int) -> int | None:
    depth = 0
    for idx in range(close_idx, -1, -1):
        ch = text[idx]
        if ch == ")":
            depth += 1
        elif ch == "(":
            depth -= 1
            if depth == 0:
                return idx
    return None


def _extract_free_function_name(header_text: str) -> str | None:
    text = header_text.strip()
    if not text:
        return None

    arrow_idx = text.rfind("->")
    if arrow_idx != -1:
        text = text[:arrow_idx].rstrip()

    while True:
        stripped = TRAILING_QUALIFIER_RE.sub("", text).rstrip()
        if stripped == text:
            break
        text = stripped

    if not text.endswith(")"):
        return None

    if _has_top_level_colon(text):
        return None

    open_idx = _find_matching_open_paren(text, len(text) - 1)
    if open_idx is None:
        return None

    head = text[:open_idx].rstrip()
    match = IDENTIFIER_TAIL_RE.search(head)
    if not match:
        return None

    name = match.group(1)
    if name in CONTROL_KEYWORDS:
        return None
    if name.startswith("operator") or name.startswith("~"):
        return None
    if name.isupper():
        # Filters macro invocations shaped like functions, e.g. TEST(...) { ... }.
        return None
    if head[: match.start()].rstrip().endswith("::"):
        # Out-of-line class member or namespace-qualified definition.
        return None

    return name


def _scan_file(path: Path) -> list[tuple[str, int]]:
    raw = path.read_text(encoding="utf-8", errors="ignore")
    text = _strip_comments_and_strings(raw)

    found: list[tuple[str, int]] = []
    scope_stack: list[str] = []
    buffer_chars: list[str] = []
    buffer_start_line = 1
    line_no = 1

    for ch in text:
        if ch == "\n":
            line_no += 1

        if ch == ";":
            buffer_chars = []
            buffer_start_line = line_no
            continue

        if ch == "{":
            header = "".join(buffer_chars)
            stripped_header = header.strip()
            if CLASS_HEAD_RE.match(stripped_header):
                scope_stack.append("class")
            elif NAMESPACE_HEAD_RE.match(stripped_header):
                scope_stack.append("namespace")
            else:
                if not scope_stack or scope_stack[-1] != "class":
                    name = _extract_free_function_name(header)
                    if name:
                        found.append((name, buffer_start_line))
                scope_stack.append("other")
            buffer_chars = []
            buffer_start_line = line_no
            continue

        if ch == "}":
            if scope_stack:
                scope_stack.pop()
            buffer_chars = []
            buffer_start_line = line_no
            continue

        buffer_chars.append(ch)

    return found


def _is_under_test_dir(path: Path) -> bool:
    return "test" in path.parts


def _collect_occurrences(root: Path, include_tests: bool) -> dict[str, list[tuple[str, int]]]:
    occurrences: dict[str, list[tuple[str, int]]] = defaultdict(list)
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in SOURCE_EXTENSIONS:
            continue
        if not include_tests and _is_under_test_dir(path):
            continue
        rel_path = path.relative_to(PROJECT_ROOT).as_posix()
        for name, line_no in _scan_file(path):
            occurrences[name].append((rel_path, line_no))
    return occurrences


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default="src",
        help="Directory to scan, relative to project root (default: src)",
    )
    parser.add_argument(
        "--include-tests",
        action="store_true",
        default=False,
        help="Also scan files under a .../test/ directory (default: excluded)",
    )
    args = parser.parse_args()

    root = (PROJECT_ROOT / args.root).resolve()
    if not root.is_dir():
        print(f"[ERROR] root directory not found: {root}")
        return 2

    occurrences = _collect_occurrences(root, args.include_tests)
    duplicates = {
        name: locations
        for name, locations in occurrences.items()
        if len({file_path for file_path, _ in locations}) > 1
    }

    if not duplicates:
        print(f"No duplicate free-function names found under {args.root}.")
        return 0

    print(f"Duplicate free-function names under {args.root}:\n")
    for name in sorted(duplicates):
        locations = sorted(duplicates[name])
        print(f"- {name} ({len(locations)} occurrences)")
        for file_path, line_no in locations:
            print(f"    {file_path}:{line_no}")
        print()

    print(f"Total duplicate names: {len(duplicates)}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
