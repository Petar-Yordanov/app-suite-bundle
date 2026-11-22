#!/usr/bin/env python3
"""Publish verified CI installers into the suite's persistent rolling release."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

TAG = "latest-builds"
TITLE = "Latest App Suite Builds"


def gh(*args, check=True):
    return subprocess.run(["gh", *args], check=check, capture_output=True, text=True)


def data(*args):
    return json.loads(gh(*args).stdout)


def ensure_release(repo, sha):
    endpoint = f"repos/{repo}/releases/tags/{TAG}"
    existing = gh("api", endpoint, check=False)
    if existing.returncode == 0:
        return json.loads(existing.stdout)
    # Two app workflows may finish at the same time. A concurrent create is
    # harmless; the follow-up GET must succeed before touching any assets.
    result = gh("release", "create", TAG, "--repo", repo, "--target", sha,
                "--title", TITLE, "--notes",
                "Latest successful installers for each application. Assets are updated independently; their labels identify the source commit. The tag's source archive is not a complete snapshot of all current binaries.",
                "--latest=false", check=False)
    if result.returncode:
        print(result.stderr.strip())
    return data("api", endpoint)


def assets(repo, release_id):
    pages = data("api", "--paginate", "--slurp", f"repos/{repo}/releases/{release_id}/assets?per_page=100")
    return {a["name"]: a for page in pages for a in page}


def rename(repo, asset, name, label):
    return data("api", "--method", "PATCH", f"repos/{repo}/releases/assets/{asset['id']}",
                "-f", f"name={name}", "-f", f"label={label}")


def delete(repo, asset):
    gh("api", "--method", "DELETE", f"repos/{repo}/releases/assets/{asset['id']}")


def digest(path):
    with path.open("rb") as handle:
        return "sha256:" + hashlib.file_digest(handle, "sha256").hexdigest()


def publish(repo, release_id, path, sha, token):
    name = path.name
    stage = f"pending-{token}-{name}"
    backup = f"previous-{token}-{name}"
    current = assets(repo, release_id)
    # Recover an interrupted previous publication before attempting another.
    if name not in current:
        previous = [a for n, a in current.items() if n.startswith("previous-") and n.endswith("-" + name)]
        if previous:
            old = max(previous, key=lambda a: a["updated_at"])
            rename(repo, old, name, old.get("label") or name)
            current = assets(repo, release_id)
    expected = digest(path)
    if name in current and current[name].get("digest") == expected:
        print(f"{name}: identical, keeping existing asset")
        return
    # Never use gh release upload --clobber: it deletes the existing asset
    # before uploading the replacement. Stage and verify the new upload first.
    with tempfile.TemporaryDirectory() as temporary:
        staged_file = Path(temporary) / stage
        shutil.copyfile(path, staged_file)
        gh("release", "upload", TAG, str(staged_file), "--repo", repo)
    current = assets(repo, release_id)
    new = current.get(stage)
    if not new or new.get("state") != "uploaded" or new.get("size") != path.stat().st_size or new.get("digest") != expected:
        raise RuntimeError(f"Upload verification failed for {name}; previous installer has not been touched")
    old = current.get(name)
    if old:
        old = rename(repo, old, backup, old.get("label") or name)
    try:
        rename(repo, new, name, f"{name} ({sha[:12]})")
    except Exception:
        if old:
            # A successful PATCH may have lost its HTTP response. Check first.
            current = assets(repo, release_id)
            if name not in current:
                rename(repo, old, name, old.get("label") or name)
        raise
    # Cleanup only after the replacement has its permanent filename.
    for asset_name, asset in assets(repo, release_id).items():
        if asset_name != name and asset_name.endswith("-" + name) and asset_name.startswith(("previous-", "pending-")):
            delete(repo, asset)
    print(f"Published {name} from {sha[:12]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("names", nargs="+")
    args = parser.parse_args()
    repo = os.environ["GITHUB_REPOSITORY"]
    sha = os.environ["GITHUB_SHA"]
    token = os.environ["GITHUB_RUN_ID"] + "-" + os.environ["GITHUB_RUN_ATTEMPT"]
    files = []
    for name in args.names:
        if Path(name).name != name or not name.endswith((".exe", ".deb", ".pkg", ".zip")):
            parser.error(f"Invalid installer filename: {name}")
        file = args.directory / name
        if not file.is_file() or not 0 < file.stat().st_size < 2 * 1024**3:
            parser.error(f"Missing, empty, or oversized installer: {file}")
        files.append(file)
    release = ensure_release(repo, sha)
    if release.get("immutable"):
        raise RuntimeError("The rolling release is immutable. Use a mutable rolling release or versioned releases instead.")
    for file in files:
        publish(repo, release["id"], file, sha, token)
    print(f"Permanent download page: https://github.com/{repo}/releases/tag/{TAG}")


if __name__ == "__main__":
    main()
