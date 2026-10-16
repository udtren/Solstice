# SPDX-FileCopyrightText: 2026 Solstice contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regression fixtures for both missed errors and false positives."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from check_docs import Checker, Document, FIXED_DOCS, WIKI


class DocumentationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for path in FIXED_DOCS:
            self.write(path, "# Overview\n")
        self.write(WIKI / "index.md", "---\ntype: index\nupdated: 2026-10-16\n---\n")
        self.write(WIKI / "log.md", "---\ntype: log\n---\n")

    def write(self, path, content):
        path = self.root / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def check(self):
        checker = Checker(self.root)
        checker.run()
        return checker.errors

    def page(self, front=None, indexed=True):
        path = WIKI / "concepts/example.md"
        front = front or "type: concept\nupdated: 2026-10-16\nsources: [evidence]\nrelated: []"
        self.write(path, f"---\n{front}\n---\n\n# Example\n")
        if indexed:
            self.write(WIKI / "index.md", "---\ntype: index\nupdated: 2026-10-16\n---\n[Example](concepts/example.md)\n")
        return path

    def test_inline_reference_image_html_and_encoded_paths(self):
        self.write("docs/a (b).md", "# Heading\n")
        self.write("docs/hash#name.md", "# Heading\n")
        self.write("docs/画.png", "fixture")
        self.write("docs/links.md", """# Links
[inline](a%20(b).md#heading "Title")
[ref][destination]
![image](画.png)
<a href='a%20%28b%29.md#heading'>HTML</a>
<img src="画.png" />
[encoded hash](hash%23name.md#heading)

[destination]: <a (b).md#heading>
""")
        self.assertEqual(self.check(), [])

    def test_code_comments_and_external_urls_are_not_local_links(self):
        self.write("docs/examples.md", """# Examples
`[inline](missing.md)`
<!-- [comment](missing.md) <img src='missing.png'> -->

```markdown
[fenced](missing.md)
```

~~~markdown
[tilde fenced](missing.md)
~~~

    [indented](missing.md)

[web](https://example.invalid/missing#anchor)
[protocol relative](//example.invalid/missing)
[email](mailto:someone@example.invalid)
""")
        self.assertEqual(self.check(), [])

    def test_heading_markup_unicode_setext_and_duplicate_collisions(self):
        self.write("docs/headings.md", """# A `code` & *text* 日本語 😄
# Repeat
# Repeat-1
# Repeat
Setext title
------------
<a id="custom"></a>

[unicode](#a-code--text-日本語-)
[first](#repeat)
[literal suffix](#repeat-1)
[second](#repeat-2)
[setext](#setext-title)
[custom](#custom)
""")
        self.assertEqual(self.check(), [])

    def test_missing_file_image_heading_and_case_report_lines(self):
        self.write("docs/Target.md", "# Real\n")
        self.write("docs/broken.md", "# Broken\n[one](absent.md)\n![two](absent.png)\n[three](Target.md#absent)\n[four](target.md)\n")
        errors = self.check()
        self.assertEqual(len(errors), 4, errors)
        for line in (2, 3, 4, 5):
            self.assertTrue(any(f"docs/broken.md:{line}:" in e for e in errors), errors)

    def test_fenced_heading_does_not_create_anchor(self):
        self.write("docs/broken.md", "```\n# Hidden\n```\n\n[link](#hidden)\n")
        self.assertIn("missing heading/anchor", self.check()[0])

    def test_links_cannot_leave_repository(self):
        self.write("docs/broken.md", "[outside](../../outside.md)\n")
        self.assertIn("link leaves repository", self.check()[0])

    def test_reference_link_location_is_usage_not_definition(self):
        doc = Document(Path("x.md"), "# Title\n\nParagraph\n[link][id]\n\n[id]: missing.md\n")
        self.assertEqual([(r.target, r.line) for r in doc.links], [("missing.md", 4)])

    def test_wiki_valid_schema_and_relative_related_link(self):
        self.page("type: concept\nupdated: 2026-10-16\nsources: ['symbol: detail']\nrelated: [../log.md]")
        self.assertEqual(self.check(), [])

    def test_history_index_and_log_have_distinct_schemas(self):
        self.write(WIKI / "history/snapshot.md", "---\ntype: history\nstatus: archived\nsources: [original]\n---\n# Snapshot\n")
        self.write(WIKI / "index.md", "---\ntype: index\nupdated: 2026-10-16\n---\n[Snapshot](history/snapshot.md)\n")
        self.assertEqual(self.check(), [])

    def test_plain_text_or_code_in_index_is_not_coverage(self):
        self.page(indexed=False)
        self.write(WIKI / "index.md", "---\ntype: index\nupdated: 2026-10-16\n---\nconcepts/example.md\n`[Example](concepts/example.md)`\n")
        self.assertIn("no link in wiki/index.md", self.check()[0])

    def test_missing_metadata_wrong_type_empty_sources_and_related(self):
        self.page("type: decision\nupdated: 2026-10-16\nsources: []\nrelated: [missing.md]")
        errors = self.check()
        self.assertEqual(len(errors), 3, errors)
        self.assertTrue(any("wiki type" in e for e in errors))
        self.assertTrue(any("sources must" in e for e in errors))
        self.assertTrue(any("related must" in e for e in errors))
        self.page("type: concept")
        self.assertEqual(len(self.check()), 3)

    def test_invalid_yaml_and_duplicate_keys_are_diagnostics(self):
        for front in ("type: [", "type: concept\ntype: history", "type: concept\nupdated: 2026-02-30"):
            with self.subTest(front=front):
                self.page(front)
                errors = self.check()
                self.assertEqual(len(errors), 1, errors)
                self.assertIn("invalid frontmatter", errors[0])

    def test_unquoted_colon_in_source_is_not_a_string(self):
        self.page("type: concept\nupdated: 2026-10-16\nsources:\n  - phase: details\nrelated: []")
        self.assertIn("sources must", self.check()[0])

    def test_unclosed_frontmatter(self):
        self.page()
        self.write(WIKI / "concepts/example.md", "---\ntype: concept\n")
        self.assertIn("unclosed frontmatter", self.check()[0])

    def test_cli_exit_codes_and_whitespace_in_committed_pr_diff(self):
        def git(*args):
            return subprocess.run(["git", "-C", str(self.root), *args], check=True, capture_output=True, text=True).stdout.strip()

        git("init", "-q")
        git("add", ".")
        git("-c", "user.name=Doc Test", "-c", "user.email=docs@example.invalid", "-c", "commit.gpgsign=false", "commit", "-qm", "baseline")
        base = git("rev-parse", "HEAD")
        script = Path(__file__).with_name("check_docs.py")

        def cli(*args):
            return subprocess.run([sys.executable, "-B", str(script), "--root", str(self.root), *args], capture_output=True, text=True, encoding="utf-8")

        self.assertEqual(cli().returncode, 0)
        self.write("docs/broken.md", "[bad](missing.md)\n")
        self.assertEqual(cli().returncode, 1)
        self.write("docs/broken.md", "Trailing spaces   \n")
        git("add", ".")
        git("-c", "user.name=Doc Test", "-c", "user.email=docs@example.invalid", "-c", "commit.gpgsign=false", "commit", "-qm", "whitespace fixture")
        result = cli("--base", base)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("trailing whitespace", result.stdout)
        self.assertEqual(cli("--base", "missing-ref").returncode, 1)


if __name__ == "__main__":
    unittest.main()
