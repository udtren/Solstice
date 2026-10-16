#!/usr/bin/env python3
#
#  SPDX-License-Identifier: GPL-3.0-or-later
#
"""Regenerate krita.pot from the sources and merge it into po/<lang>/krita.po.

A Windows replacement for Messages.sh and the KDE l10n infrastructure it
expects (extractrc, xgettext flags, rc.cpp reference fix-up). See
docs/agent/translations.md.

Usage (from any directory):
    python dev-tools/translations/update_translations.py [--lang zh_CN ...]
        [--gettext-dir <krita-dev-root>/_install/bin] [--pot-only]
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PERL_SHIM = Path(__file__).resolve().parent / "perl"

# Keywords of the KDE extraction scripts (ki18n and KDE frameworks macros)
KDE_KEYWORDS = [
    "-ki18n:1", "-ki18nc:1c,2", "-ki18np:1,2", "-ki18ncp:1c,2,3",
    "-kki18n:1", "-kki18nc:1c,2", "-kki18np:1,2", "-kki18ncp:1c,2,3",
    "-kxi18n:1", "-kxi18nc:1c,2", "-kxi18np:1,2", "-kxi18ncp:1c,2,3",
    "-kkxi18n:1", "-kkxi18nc:1c,2", "-kkxi18np:1,2", "-kkxi18ncp:1c,2,3",
    "-kkli18n:1", "-kkli18nc:1c,2", "-kkli18np:1,2", "-kkli18ncp:1c,2,3",
    "-kklxi18n:1", "-kklxi18nc:1c,2", "-kklxi18np:1,2", "-kklxi18ncp:1c,2,3",
    "-kI18N_NOOP:1", "-kI18NC_NOOP:1c,2", "-kI18N_NOOP2:1c,2",
    "-kI18N_NOOP2_NOSTRIP:1c,2", "-ktr2i18n:1", "-ktr2xi18n:1",
]
COMMON_FLAGS = [
    "--copyright-holder=This_file_is_part_of_KDE",
    "--msgid-bugs-address=https://github.com/udtren/Solstice/issues",
    "--from-code=UTF-8",
    "--add-comments=i18n",
    "--force-po",
]
CPP_FLAGS = ["-C", "--kde", "-k", *KDE_KEYWORDS]
# kundo2_aware_xgettext.sh: undo command names get the "(qtundo-format)" context
KUNDO2_FLAGS = ["-C", "--kde", "-k", "-kkundo2_i18n:1", "-kkundo2_i18np:1,2",
                "-kkundo2_i18nc:1c,2", "-kkundo2_i18ncp:1c,2,3"]
PY_FLAGS = ["-L", "Python", "-k", "-ki18n:1", "-ki18nc:1c,2", "-ki18np:1,2",
            "-ki18ncp:1c,2,3"]


def tool(name, gettext_dir):
    exe = name + (".exe" if os.name == "nt" else "")
    if gettext_dir:
        path = Path(gettext_dir) / exe
        if path.exists():
            return str(path)
        sys.exit(f"{exe} not found in {gettext_dir}")
    found = shutil.which(name)
    if not found:
        sys.exit(f"{name} not found; pass --gettext-dir <krita-dev-root>/_install/bin "
                 "or set SOLSTICE_GETTEXT_DIR")
    return found


def find_perl(perl_arg):
    if perl_arg:
        return perl_arg
    found = shutil.which("perl")
    if found:
        return found
    git = shutil.which("git")
    if git:
        # Git for Windows: <git>/cmd/git.exe and <git>/usr/bin/perl.exe
        candidate = Path(git).resolve().parents[1] / "usr" / "bin" / "perl.exe"
        if candidate.exists():
            return str(candidate)
    sys.exit("perl not found; pass --perl")


def run(cmd, **kwargs):
    result = subprocess.run(cmd, cwd=REPO, **kwargs)
    if result.returncode != 0:
        if result.stderr:
            sys.stderr.write(result.stderr.decode("utf-8", "replace"))
        sys.exit(f"command failed ({result.returncode}): {cmd[0]} {cmd[1]} ...")
    return result


def tracked_files():
    out = subprocess.run(["git", "ls-files", "-z"], cwd=REPO, check=True,
                         capture_output=True).stdout.decode("utf-8")
    return [f for f in out.split("\0") if f and (REPO / f).is_file()]


def select_sources(files):
    def no_tests(f):
        return "/tests/" not in f

    ui = [f for f in files if f.endswith(".ui") and no_tests(f)]
    xmlgui = [f for f in files if f.endswith(".xmlgui")
              and not f.endswith(("KritaSketchWin.xmlgui", "KritaGeminiWin.xmlgui"))]
    actions = [f for f in files if f.endswith(".action") and no_tests(f)]
    code = [f for f in files if f.endswith((".cc", ".h", ".cpp", ".qml")) and no_tests(f)
            and not f.startswith(("sdk/templates/", "krita/crashreporter/"))]
    python = [f for f in files if f.endswith(".py")]
    return ui, xmlgui, actions, code, python


def chunks(items, size=200):
    for i in range(0, len(items), size):
        yield items[i:i + size]


def perl_output(perl, script_args, files):
    """Run a Perl extractor over files in batches (Windows command-line limit)."""
    parts = []
    for batch in chunks(files):
        parts.append(run([*perl, *script_args, *batch], capture_output=True)
                     .stdout.decode("utf-8"))
    return "".join(parts)


def write_extractrc(work):
    """Write the extractrc used for .ui and .xmlgui files.

    action_i18n.pl is KDE's extractrc, changed so that only "context" becomes
    the gettext context. For Qt Designer files the KDE infrastructure also
    uses the "comment" attribute (Qt's disambiguation) as the context, which
    the existing catalogs rely on (for example msgctxt "EXIF").
    """
    text = (REPO / "action_i18n.pl").read_text(encoding="utf-8")
    old = '$context = $attr_context if $attr_context;'
    if old not in text:
        sys.exit("action_i18n.pl changed; update write_extractrc()")
    new = (old
           + '\n         my ($attr_comment) = $attr =~ /\\bcomment=\\"([^\\"]*)\\"/ if $attr;'
           + '\n         $context = $attr_comment if $attr_comment and not $attr_context;')
    path = work / "extractrc.pl"
    path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")
    return str(path)


def write_list(path, files):
    path.write_text("\n".join(files) + "\n", encoding="utf-8")


def header_and_body(text):
    # the header entry ends at the first empty line
    head, _, body = text.partition("\n\n")
    return head, body


def add_qtundo_context(body):
    """Prefix existing contexts with "(qtundo-format)" and add it where missing."""
    out, last = [], ""
    for line in body.split("\n"):
        if line.startswith('msgctxt "'):
            line = 'msgctxt "(qtundo-format) ' + line[len('msgctxt "'):]
        elif line.startswith('msgid "') and not last.startswith(('"', "msgctxt")):
            out.append('msgctxt "(qtundo-format)"')
        out.append(line)
        last = line
    return "\n".join(out)


FILE_COMMENT = re.compile(r"^#\. i18n: file: (?:\./)?(.+)$")


def fix_rc_references(pot_text):
    """Turn '// i18n: file: x.ui:12' comments into references, drop rc.cpp ones.

    This is what the KDE extraction infrastructure does after xgettext reads
    the generated rc.cpp.
    """
    out_blocks = []
    for block in pot_text.split("\n\n"):
        lines = block.split("\n")
        refs, kept = [], []
        for line in lines:
            match = FILE_COMMENT.match(line)
            if match:
                refs.append(match.group(1))
            elif line.startswith("#: "):
                refs.extend(r for r in line[3:].split() if not r.startswith("rc.cpp:"))
            else:
                kept.append(line)
        if refs:
            # references go after the extracted comments, before flags
            pos = 0
            while pos < len(kept) and kept[pos].startswith(("#.", "# ")):
                pos += 1
            kept[pos:pos] = ["#: " + r for r in refs]
        out_blocks.append("\n".join(kept))
    return "\n\n".join(out_blocks)


def sort_by_reference(pot_text):
    """Order entries like the KDE catalogs: entries without references first,
    in extraction order, then by the file and line of the first reference."""
    head, body = header_and_body(pot_text)
    blocks = [b for b in body.strip("\n").split("\n\n") if b]

    def key(item):
        index, block = item
        match = re.search(r"^#: (\S+)", block, re.M)
        if not match:
            return (0, "", 0, index)
        path, _, line = match.group(1).rpartition(":")
        return (1, path, int(line) if line.isdigit() else 0, index)

    ordered = [b for _, b in sorted(enumerate(blocks), key=key)]
    return head + "\n\n" + "\n\n".join(ordered) + "\n"


def generate_pot(pot, xgettext, msgcat, perl, work):
    files = tracked_files()
    ui, xmlgui, actions, code, python = select_sources(files)

    probe = subprocess.run([perl, "-MArchive::Zip", "-e", "1"], capture_output=True)
    if probe.returncode != 0:
        # relative to the repository root, which MSYS Perl reads like any Perl
        perl = [perl, "-I" + PERL_SHIM.relative_to(REPO).as_posix()]
    else:
        perl = [perl]

    extractrc = write_extractrc(work)
    rc = perl_output(perl, [extractrc], ui)
    rc += perl_output(perl, [extractrc], xmlgui)
    rc += perl_output(perl, ["action_i18n.pl", "--context=action"], actions)
    rc += run([*perl, "extracti18n.pl"], capture_output=True).stdout.decode("utf-8")
    # resource files checked out with CRLF would leave "\r" inside the strings
    rc = rc.replace("\r", "")
    rc_cpp = REPO / "rc.cpp"
    if rc_cpp.exists():
        sys.exit("rc.cpp already exists in the repository root; remove it first")
    rc_cpp.write_text(rc, encoding="utf-8", newline="\n")

    try:
        sources = work / "sources.txt"
        write_list(sources, ["rc.cpp", *code])
        normal = work / "normal.pot"
        undo = work / "undo.pot"
        run([xgettext, *COMMON_FLAGS, *CPP_FLAGS, "-f", str(sources), "-o", str(normal)])
        run([xgettext, *COMMON_FLAGS, *KUNDO2_FLAGS, "-f", str(sources), "-o", str(undo)])
    finally:
        rc_cpp.unlink()

    normal_text = normal.read_text(encoding="utf-8")
    undo_text = undo.read_text(encoding="utf-8")
    if "(qtundo-format)" in normal_text + undo_text:
        sys.exit("'(qtundo-format)' must not be added manually; use kundo2_i18n*()")

    # prefix "(qtundo-format)" to the contexts of undo command names
    undo_head, undo_body = header_and_body(undo_text)
    if undo_body.strip():
        undo.write_text(undo_head + "\n\n" + add_qtundo_context(undo_body),
                        encoding="utf-8", newline="\n")
        merged = work / "merged.pot"
        run([msgcat, "-F", str(normal), str(undo), "-o", str(merged)])
        normal_head, _ = header_and_body(normal_text)
        _, merged_body = header_and_body(merged.read_text(encoding="utf-8"))
        pot_text = normal_head + "\n\n" + merged_body
    else:
        pot_text = normal_text

    pot.write_text(fix_rc_references(pot_text), encoding="utf-8", newline="\n")

    py_list = work / "python.txt"
    write_list(py_list, python)
    run([xgettext, *COMMON_FLAGS, *PY_FLAGS, "-j", "-f", str(py_list), "-o", str(pot)])
    pot.write_text(sort_by_reference(pot.read_text(encoding="utf-8")),
                   encoding="utf-8", newline="\n")


def merge(lang, pot, msgmerge, msgfmt, work):
    po = REPO / "po" / lang / "krita.po"
    if not po.exists():
        sys.exit(f"{po} does not exist")
    out = work / f"{lang}.po"
    run([msgmerge, "--quiet", "--previous", str(po), str(pot), "-o", str(out)])
    run([msgfmt, "--check", "--statistics", "-o", str(work / f"{lang}.mo"), str(out)])
    shutil.copyfile(out, po)
    print(f"updated {po.relative_to(REPO)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--lang", nargs="+", default=["zh_CN"],
                        help="catalogs under po/ to update (default: zh_CN)")
    parser.add_argument("--gettext-dir", default=os.environ.get("SOLSTICE_GETTEXT_DIR"),
                        help="directory with xgettext/msgcat/msgmerge/msgfmt "
                             "(default: SOLSTICE_GETTEXT_DIR or PATH)")
    parser.add_argument("--perl", help="perl executable (default: PATH or Git for Windows)")
    parser.add_argument("--pot", type=Path,
                        help="where to write krita.pot (default: a temporary directory, kept)")
    parser.add_argument("--pot-only", action="store_true",
                        help="only regenerate krita.pot; do not touch po/")
    args = parser.parse_args()

    xgettext = tool("xgettext", args.gettext_dir)
    msgcat = tool("msgcat", args.gettext_dir)
    msgmerge = tool("msgmerge", args.gettext_dir)
    msgfmt = tool("msgfmt", args.gettext_dir)
    perl = find_perl(args.perl)

    work = Path(tempfile.mkdtemp(prefix="solstice-i18n-"))
    pot = (args.pot or work / "krita.pot").resolve()
    pot.parent.mkdir(parents=True, exist_ok=True)

    generate_pot(pot, xgettext, msgcat, perl, work)
    print(f"wrote {pot}")
    if not args.pot_only:
        for lang in args.lang:
            merge(lang, pot, msgmerge, msgfmt, work)


if __name__ == "__main__":
    main()
