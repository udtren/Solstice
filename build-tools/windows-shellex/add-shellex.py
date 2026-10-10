# SPDX-FileCopyrightText: 2026 Solstice contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Adds the Krita Shell Extension (Explorer thumbnails of .kra/.krz) to a
Solstice installation or package folder.

usage: add-shellex.py <installation or package root> [--cache-dir DIR]

Downloads the pinned krita-nsis package (shellex-package.json), checks the
archive and every extracted file against their SHA-256, and writes
<root>/shellex/ with the 32-bit and 64-bit DLLs, their license and the
Solstice registration scripts. It never touches the registry; users run
register-thumbnails.cmd themselves. Also usable as package-complete.py's
--pre-zip-hook, which passes the package root as the only argument.
See docs/agent/windows-shell-thumbnails.md.
"""

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = ["register-thumbnails.ps1", "register-thumbnails.cmd", "unregister-thumbnails.cmd", "README.txt"]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fetch(spec, cache_dir):
    name = os.path.basename(spec["url"])
    cached = os.path.join(cache_dir, name) if cache_dir else None
    if cached and os.path.isfile(cached):
        with open(cached, "rb") as f:
            data = f.read()
        if sha256(data) == spec["sha256"]:
            return data
        print(f"Cached {cached} has a wrong checksum; downloading again")
    print(f"Downloading {spec['url']}")
    with urllib.request.urlopen(spec["url"], timeout=120) as response:
        data = response.read()
    if sha256(data) != spec["sha256"]:
        sys.exit(f"Checksum mismatch for {spec['url']}: {sha256(data)}")
    if cached:
        os.makedirs(cache_dir, exist_ok=True)
        with open(cached, "wb") as f:
            f.write(data)
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", help="installation or package root (contains bin/)")
    parser.add_argument("--cache-dir", help="keep the downloaded archive here")
    args = parser.parse_args()

    root = os.path.abspath(args.root.rstrip("\\/"))
    if not os.path.isdir(os.path.join(root, "bin")):
        sys.exit(f"{root} does not look like a Solstice installation (no bin/)")
    with open(os.path.join(HERE, "shellex-package.json"), encoding="utf-8") as f:
        spec = json.load(f)

    data = fetch(spec, args.cache_dir)
    with tempfile.TemporaryDirectory() as temp:
        archive = os.path.join(temp, "krita-nsis.zip")
        with open(archive, "wb") as f:
            f.write(data)
        extracted = {}
        with zipfile.ZipFile(archive) as z:
            # read only the pinned members; nothing else is extracted
            for member, digest in spec["files"].items():
                content = z.read(member)
                if sha256(content) != digest:
                    sys.exit(f"Checksum mismatch for {member}")
                extracted[os.path.basename(member)] = content

    target = os.path.join(root, "shellex")
    os.makedirs(target, exist_ok=True)
    for name, content in extracted.items():
        with open(os.path.join(target, name), "wb") as f:
            f.write(content)
    for name in SCRIPTS:
        shutil.copyfile(os.path.join(HERE, name), os.path.join(target, name))
    with open(os.path.join(target, "VERSION.txt"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(f"{spec['name']} {spec['version']} (DLL {spec['dllVersion']})\n{spec['project']}\n")
    print(f"Shell extension added to {target}")


if __name__ == "__main__":
    main()
