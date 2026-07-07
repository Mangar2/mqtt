#!/usr/bin/env python3
"""Move files/directories inside src/ and rewrite matching #include paths."""

import argparse
import re
import subprocess
from pathlib import Path

INCLUDE_RE = re.compile(r'(#include\s*")([^"<>]+)(")')


def git_mv(src: Path, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["git", "mv", str(src), str(dest)], check=True)


def relpath(path: Path, root: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def plan_moves(sources: list[Path], dest: Path) -> list[tuple[Path, Path]]:
    if len(sources) == 1 and not dest.exists():
        return [(sources[0], dest)]
    dest.mkdir(parents=True, exist_ok=True)
    return [(s, dest / s.name) for s in sources]


def build_mapping(moves: list[tuple[Path, Path]], root: Path):
    file_map: dict[str, str] = {}
    dir_map: dict[str, str] = {}
    for old, new in moves:
        if old.is_dir():
            dir_map[relpath(old, root) + "/"] = relpath(new, root) + "/"
        else:
            file_map[relpath(old, root)] = relpath(new, root)
    return file_map, dir_map


def rewrite_includes(root: Path, file_map: dict, dir_map: dict) -> int:
    dir_prefixes = sorted(dir_map, key=len, reverse=True)
    changed = 0
    for path in root.rglob("*"):
        if path.suffix not in (".h", ".hpp", ".cpp"):
            continue
        text = path.read_text(encoding="utf-8")

        def repl(m: re.Match) -> str:
            quoted = m.group(2)
            if quoted in file_map:
                return m.group(1) + file_map[quoted] + m.group(3)
            for prefix in dir_prefixes:
                if quoted.startswith(prefix):
                    return m.group(1) + dir_map[prefix] + quoted[len(prefix):] + m.group(3)
            return m.group(0)

        new_text = INCLUDE_RE.sub(repl, text)
        if new_text != text:
            path.write_text(new_text, encoding="utf-8")
            changed += 1
    return changed


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Move one or more files/directories with 'git mv' and update every "
            "quoted #include referencing them, project-wide. "
            "If --dest exists as a directory (or multiple sources are given), "
            "each source is moved into --dest, keeping its basename. "
            "If a single source is given and --dest does not yet exist, the "
            "source is renamed to exactly --dest."
        )
    )
    parser.add_argument("--src-root", required=True, help="the include root (usually 'src')")
    parser.add_argument("--dest", required=True, help="destination file or directory")
    parser.add_argument("sources", nargs="+", help="file(s)/directory(ies) to move")
    args = parser.parse_args()

    root = Path(args.src_root).resolve()
    sources = [Path(s).resolve() for s in args.sources]
    dest = Path(args.dest).resolve()

    for s in sources:
        if not s.exists():
            parser.error(f"source not found: {s}")

    moves = plan_moves(sources, dest)
    file_map, dir_map = build_mapping(moves, root)

    for old, new in moves:
        print(f"git mv {relpath(old, root)} -> {relpath(new, root)}")
        git_mv(old, new)

    changed = rewrite_includes(root, file_map, dir_map)
    print(f"Updated includes in {changed} file(s).")


if __name__ == "__main__":
    main()
