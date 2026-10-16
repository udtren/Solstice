#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Solstice contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check repository-owned Markdown navigation and the agent wiki schema."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from datetime import date
from html.parser import HTMLParser
import os
from pathlib import Path
import re
import subprocess
import sys
import unicodedata
from urllib.parse import unquote, urlsplit

from markdown_it import MarkdownIt
import yaml


ROOT = Path(__file__).resolve().parents[2]
FIXED_DOCS = ("AGENTS.md", "README.md", "build-tools/gmic/README.md")
WIKI = Path("docs/agent/wiki")
PAGE_TYPES = {
    "concepts": "concept", "decisions": "decision", "pitfalls": "pitfall",
    "benchmarks": "benchmark", "history": "history", "queries": "query",
}
DIFF_PATHS = (*FIXED_DOCS, "docs", "dev-tools/docs", ".github/workflows/docs-check.yml")
MARKDOWN = MarkdownIt("commonmark").enable(["table", "strikethrough"])


class UniqueLoader(yaml.SafeLoader):
    """Reject duplicate frontmatter keys instead of silently using the last one."""

    def construct_mapping(self, node, deep=False):
        result = {}
        for key_node, value_node in node.value:
            key = self.construct_object(key_node, deep=deep)
            if not isinstance(key, str) or key in result:
                raise yaml.constructor.ConstructorError(
                    None, None, "non-string or duplicate metadata key", key_node.start_mark
                )
            result[key] = self.construct_object(value_node, deep=deep)
        return result


@dataclass
class Reference:
    target: str
    line: int


class HTMLReferences(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.anchors = set()

    def handle_starttag(self, tag, attrs):
        for name, value in attrs:
            if value is None:
                continue
            if name in ("href", "src"):
                self.links.append(Reference(value, self.getpos()[0]))
            if name == "id" or (tag == "a" and name == "name"):
                self.anchors.add(value)


def heading_text(tokens):
    """Visible inline text, including code and image alt text, excluding HTML tags."""
    parts = []
    for token in tokens:
        if token.type in ("text", "code_inline"):
            parts.append(token.content)
        elif token.type in ("softbreak", "hardbreak"):
            parts.append(" ")
        elif token.type == "image":
            parts.append(heading_text(token.children or []))
    return "".join(parts)


def heading_slug(text):
    # GitHub-style anchors for ordinary headings: keep letters, marks, numbers,
    # underscores and hyphens; remove punctuation/emoji and replace spaces.
    return "".join(
        char for char in text.lower()
        if char in " _-" or unicodedata.category(char)[0] in "LMN"
    ).replace(" ", "-")


@dataclass
class Document:
    path: Path
    text: str
    links: list[Reference] = field(default_factory=list)
    anchors: set[str] = field(default_factory=set)
    metadata: dict = field(default_factory=dict)
    metadata_error: tuple[int, str] | None = None

    def __post_init__(self):
        lines = self.text.splitlines(keepends=True)
        if lines and lines[0].strip() == "---":
            end = next((i for i in range(1, len(lines)) if lines[i].strip() == "---"), None)
            if end is None:
                self.metadata_error = (1, "unclosed frontmatter")
            else:
                try:
                    self.metadata = yaml.load("".join(lines[1:end]), Loader=UniqueLoader)
                    if not isinstance(self.metadata, dict):
                        self.metadata_error = (1, "frontmatter must be a mapping")
                        self.metadata = {}
                except (yaml.YAMLError, ValueError) as error:
                    mark = getattr(error, "problem_mark", None)
                    self.metadata_error = (
                        mark.line + 2 if mark else 1,
                        "invalid frontmatter: " + str(getattr(error, "problem", error)),
                    )
                # Retain line numbers without interpreting metadata as Markdown.
                lines[:end + 1] = ["\n"] * (end + 1)
        tokens = MARKDOWN.parse("".join(lines))
        used_slugs = set()
        for i, token in enumerate(tokens):
            line = token.map[0] + 1 if token.map else 1
            if token.type == "heading_open":
                original = heading_slug(heading_text(tokens[i + 1].children or []))
                slug, suffix = original, 0
                while slug in used_slugs:
                    suffix += 1
                    slug = f"{original}-{suffix}"
                used_slugs.add(slug)
                self.anchors.add(slug)
            if token.type == "html_block":
                self.add_html(token.content, line)
            if token.type == "inline":
                for child in token.children or []:
                    if child.type in ("link_open", "image"):
                        attr = "href" if child.type == "link_open" else "src"
                        self.links.append(Reference(child.attrGet(attr) or "", line))
                    elif child.type == "html_inline":
                        self.add_html(child.content, line)
                    if child.type in ("softbreak", "hardbreak"):
                        line += 1
                    else:
                        line += child.content.count("\n")

    def add_html(self, content, line):
        parser = HTMLReferences()
        parser.feed(content)
        self.links.extend(Reference(r.target, line + r.line - 1) for r in parser.links)
        self.anchors.update(parser.anchors)

    def field_line(self, name):
        match = re.search(rf"(?m)^{re.escape(name)}:", self.text)
        return self.text[:match.start()].count("\n") + 1 if match else 1


class Checker:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.documents = {}
        self.errors = []
        self.local_references = 0
        self.heading_references = 0

    def error(self, path, line, message):
        self.errors.append(f"{path.as_posix()}:{line}: {message}")

    def document(self, path):
        if path not in self.documents:
            try:
                text = (self.root / path).read_text(encoding="utf-8-sig")
                self.documents[path] = Document(path, text)
            except (OSError, UnicodeError) as error:
                self.error(path, 1, f"cannot read Markdown: {error}")
                self.documents[path] = None
        return self.documents[path]

    def destination(self, path, reference):
        try:
            url = urlsplit(reference.target)
        except ValueError:
            self.error(path, reference.line, f"invalid link: {reference.target}")
            return None
        if url.scheme or url.netloc or reference.target.startswith("//"):
            return None
        raw = unquote(url.path)
        candidate = (self.root / raw.lstrip("/")) if raw.startswith("/") else (
            self.root / path.parent / raw if raw else self.root / path
        )
        try:
            candidate.resolve().relative_to(self.root)
            # resolve() may correct casing on Windows; retain the link spelling.
            target = Path(os.path.abspath(candidate)).relative_to(self.root)
        except (ValueError, OSError):
            self.error(path, reference.line, f"link leaves repository: {reference.target}")
            return None
        return target, unquote(url.fragment)

    def exists_exactly(self, path):
        # Check spelling on Windows too: Linux/GitHub paths are case-sensitive.
        current = self.root
        for part in path.parts:
            if not current.is_dir() or part not in {p.name for p in current.iterdir()}:
                return False
            current /= part
        return current.exists()

    def check_link(self, path, reference):
        destination = self.destination(path, reference)
        if destination is None:
            return
        target, fragment = destination
        self.local_references += 1
        if not self.exists_exactly(target):
            self.error(path, reference.line, f"missing target or wrong case: {reference.target}")
        elif fragment and target.suffix.lower() == ".md":
            self.heading_references += 1
            doc = self.document(target)
            if doc and fragment not in doc.anchors:
                self.error(path, reference.line, f"missing heading/anchor: {reference.target}")

    def check_wiki(self, paths):
        index = self.document(WIKI / "index.md")
        indexed = set()
        if index:
            for reference in index.links:
                destination = self.destination(index.path, reference)
                if destination:
                    indexed.add(destination[0])
        for path in sorted(paths):
            doc = self.document(path)
            if not doc:
                continue
            if doc.metadata_error:
                self.error(path, *doc.metadata_error)
                continue
            data = doc.metadata
            is_index = path == WIKI / "index.md"
            is_log = path == WIKI / "log.md"
            expected = "index" if is_index else "log" if is_log else PAGE_TYPES.get(path.parent.name)
            if expected is None or data.get("type") != expected:
                self.error(path, doc.field_line("type"), f"wiki type must be {expected or 'in a known page folder'}")
            if not (is_index or is_log) and path not in indexed:
                self.error(path, 1, "wiki page has no link in wiki/index.md")
            if is_log:
                continue
            field_name = "status" if expected == "history" else "updated"
            value = data.get(field_name)
            if field_name == "updated":
                try:
                    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", str(value)):
                        raise ValueError
                    date.fromisoformat(str(value))
                except ValueError:
                    self.error(path, doc.field_line(field_name), "updated must be a valid YYYY-MM-DD date")
            elif not isinstance(value, str) or not value.strip():
                self.error(path, doc.field_line(field_name), "history status must be nonempty text")
            if is_index:
                continue
            for name in ("sources",) if expected == "history" else ("sources", "related"):
                value = data.get(name)
                if not isinstance(value, list) or (name == "sources" and not value) or any(
                    not isinstance(item, str) or not item.strip() for item in value
                ):
                    self.error(path, doc.field_line(name), f"{name} must be a list of nonempty strings" + (" (not empty)" if name == "sources" else ""))
                    continue
                if name == "related":
                    for item in value:
                        reference = Reference(item, doc.field_line(name))
                        destination = self.destination(path, reference)
                        if not destination or destination[0] not in paths:
                            self.error(path, reference.line, f"related must name an existing wiki page: {item}")
                        else:
                            self.check_link(path, reference)

    def run(self):
        paths = sorted({Path(p) for p in FIXED_DOCS} | {
            p.relative_to(self.root) for p in (self.root / "docs").rglob("*.md")
        })
        for path in paths:
            doc = self.document(path)
            if doc:
                for reference in doc.links:
                    self.check_link(path, reference)
        wiki_paths = {p for p in paths if WIKI in p.parents}
        self.check_wiki(wiki_paths)
        return len(paths)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root (default: script location)")
    parser.add_argument("--base", default="HEAD", help="git diff --check base; PR CI uses HEAD^ of the merge commit")
    args = parser.parse_args(argv)
    checker = Checker(args.root)
    count = checker.run()
    for error in checker.errors:
        print(error)
    base = subprocess.run(
        ["git", "-C", str(checker.root), "rev-parse", "--verify", "--end-of-options", f"{args.base}^{{commit}}"],
        capture_output=True, text=True, check=False,
    )
    if base.returncode:
        print(f"Cannot resolve whitespace-check base {args.base!r}: {base.stderr.strip()}")
        return 1
    diff = subprocess.run(
        ["git", "-C", str(checker.root), "diff", "--check", base.stdout.strip(), "--", *DIFF_PATHS],
        check=False,
    )
    print(f"Checked {count} Markdown files, {checker.local_references} local references "
          f"({checker.heading_references} heading references); {len(checker.errors)} documentation errors.")
    return int(bool(checker.errors or diff.returncode))


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    raise SystemExit(main())
