"""Compare Arky and bsdtar against a trusted local archive.

Run on the real desktop for --gui measurements. The CLI subprocesses use
identical listing/extraction workloads; the GUI measurement is separate.
"""
import argparse
import json
import pathlib
import subprocess
import tempfile
import time

import psutil


def measure(command, cwd=None):
    start = time.perf_counter()
    process = subprocess.Popen(command, cwd=cwd, stdout=subprocess.DEVNULL)
    peak = 0
    try:
        sample = psutil.Process(process.pid)
    except psutil.NoSuchProcess:
        sample = None
    while sample is not None and process.poll() is None:
        try:
            peak = max(peak, sample.memory_info().rss)
        except psutil.NoSuchProcess:
            break
        time.sleep(0.02)
    if process.wait():
        raise RuntimeError(f"Failed: {command[0]}")
    return {"wall_seconds": round(time.perf_counter() - start, 4),
            "sampled_peak_rss_mib": round(peak / 1048576, 2)}


def bytes_on_disk(folder):
    return sum(p.stat().st_size for p in folder.rglob("*")
               if p.is_file() and not p.is_symlink())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=pathlib.Path)
    parser.add_argument("--reference", required=True, help="bsdtar executable")
    parser.add_argument("--archive", required=True, type=pathlib.Path)
    parser.add_argument("--gui", type=pathlib.Path, help="Arky GUI executable (inside .app on macOS)")
    parser.add_argument("--reverse", action="store_true", help="Run reference first to expose cache-order effects")
    args = parser.parse_args()
    archive = str(args.archive.resolve(strict=True))
    cli = str(args.cli.resolve(strict=True))
    results = {}
    with tempfile.TemporaryDirectory(prefix="arky-benchmark-") as temp:
        root = pathlib.Path(temp)
        tools = [
            ("arky", [cli, "list", archive], [cli, "extract", archive]),
            ("reference", [args.reference, "-tf", archive], [args.reference, "-xf", archive, "-C"])
        ]
        if args.reverse:
            tools.reverse()
        for name, listing, extraction in tools:
            destination = root / name
            destination.mkdir()
            listed = measure(listing)
            extracted = measure(extraction + [str(destination)])
            extracted["output_bytes"] = bytes_on_disk(destination)
            extracted["throughput_mib_s"] = round(extracted["output_bytes"] /
                (max(0.0001, extracted["wall_seconds"]) * 1048576), 2)
            results[name] = {"list": listed, "extract": extracted}
        if args.gui:
            output = root / "ui.json"
            results["gui"] = measure([str(args.gui.resolve(strict=True)), "--benchmark-ui", archive, str(output)])
            results["gui"].update(json.loads(output.read_text(encoding="utf-8")))
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
