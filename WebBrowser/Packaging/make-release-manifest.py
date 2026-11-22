#!/usr/bin/env python3
"""Build an update manifest for the actual three desktop installer artifacts."""
import argparse
import hashlib
import json
import pathlib
import re
from urllib.parse import urlsplit, quote

ARTIFACTS = {
    "windows-x86_64": "WebBrowser-windows-x64-setup.exe",
    "linux-x86_64": "WebBrowser-linux-x64.deb",
    "macos-arm64": "WebBrowser-macos-arm64.pkg",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--base-url", required=True, help="HTTPS directory containing uploaded installers")
    parser.add_argument("--artifacts", type=pathlib.Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        parser.error("version must be major.minor.patch")
    base = urlsplit(args.base_url)
    if base.scheme != "https" or not base.hostname or base.username or base.password or base.query or base.fragment:
        parser.error("base URL must be an HTTPS directory without credentials, query or fragment")
    artifacts = {}
    for key, filename in ARTIFACTS.items():
        path = args.artifacts / filename
        if not path.is_file() or not 0 < path.stat().st_size <= 512 * 1024**2:
            parser.error(f"missing, empty or oversized installer: {path}")
        with path.open("rb") as source:
            digest = hashlib.file_digest(source, "sha256").hexdigest()
        artifacts[key] = {"url": args.base_url.rstrip("/") + "/" + quote(filename),
                          "filename": filename, "sha256": digest}
    print(json.dumps({"version": args.version, "artifacts": artifacts}, indent=2))


if __name__ == "__main__":
    main()
