#!/usr/bin/env python3
"""Measure real viewers in an isolated desktop session. Requires psutil, not the app.

Use --viewer-json '["Name", "path/to/executable", "{image}"]' for each viewer.
Include --benchmark for ImageViewer to use its first-image-render signal. Other
viewers require pressing Enter once their image is rendered (human reaction error).
"""
import argparse
import json
import pathlib
import queue
import statistics
import subprocess
import threading
import time

import psutil


def sample(process):
    rss = cpu = 0
    try:
        children = process.children(recursive=True)
    except psutil.Error:
        children = []
    for child in [process, *children]:
        try:
            rss += child.memory_info().rss
            times = child.cpu_times()
            cpu += times.user + times.system
        except psutil.Error:
            continue
    return rss, cpu


def terminate_spawned(proc):
    if proc.poll() is not None:
        return
    try:
        root = psutil.Process(proc.pid)
        children = root.children(recursive=True)
        for child in [*reversed(children), root]:
            try:
                child.terminate()
            except psutil.Error:
                pass
        _, alive = psutil.wait_procs([root, *children], timeout=3)
        for child in alive:
            try:
                child.kill()
            except psutil.Error:
                pass
    except psutil.Error:
        pass


def trial(name, argv, image, seconds):
    command = [arg.replace("{image}", str(image)) for arg in argv]
    automatic = "--benchmark" in command
    started = time.perf_counter()
    proc = subprocess.Popen(command, stdout=subprocess.PIPE if automatic else subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, text=True, bufsize=1)
    try:
        process = psutil.Process(proc.pid)
        if automatic:
            messages = queue.Queue()
            def drain():
                for line in proc.stdout:
                    if line.startswith("FIRST_IMAGE_MS="):
                        messages.put(float(line.split("=", 1)[1].strip()))
                messages.put(None)
            thread = threading.Thread(target=drain, daemon=True)
            thread.start()
            try:
                elapsed = messages.get(timeout=30)
            except queue.Empty as exc:
                raise RuntimeError(f"{name} timed out before first rendered image") from exc
            if elapsed is None:
                raise RuntimeError(f"{name} exited before displaying the image")
        else:
            input(f"{name}: press Enter as soon as the image is visible, without interacting with it first. ")
            elapsed = (time.perf_counter() - started) * 1000
        if proc.poll() is not None:
            raise RuntimeError(f"{name} launcher exited. Close existing instances and use the actual viewer executable.")
        time.sleep(1.0)  # let disk activity and preloading settle
        first_rss, first_cpu = sample(process)
        samples = [first_rss]
        idle_start = time.perf_counter()
        while time.perf_counter() - idle_start < seconds:
            time.sleep(0.25)
            if proc.poll() is not None:
                raise RuntimeError(f"{name} exited during idle sampling")
            rss, _ = sample(process)
            samples.append(rss)
        _, last_cpu = sample(process)
        idle_elapsed = time.perf_counter() - idle_start
        return {"first_image_ms": elapsed, "idle_rss_mib": statistics.median(samples) / 1048576,
                "peak_sampled_rss_mib": max(samples) / 1048576,
                "idle_cpu_pct_one_core": max(0, last_cpu - first_cpu) / idle_elapsed * 100,
                "measurement": "render signal" if automatic else "manual observation"}
    finally:
        terminate_spawned(proc)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=pathlib.Path)
    parser.add_argument("--viewer-json", action="append", required=True, help="JSON array: [label, executable, args..., {image}]")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--idle-seconds", type=float, default=5)
    args = parser.parse_args()
    image = args.image.resolve(strict=True)
    if not image.is_file() or args.runs < 1 or args.idle_seconds <= 0:
        parser.error("Use an existing still image and positive run/sample counts.")
    reports = []
    for encoded in args.viewer_json:
        spec = json.loads(encoded)
        if not isinstance(spec, list) or len(spec) < 3 or not all(isinstance(s, str) for s in spec) or not any("{image}" in s for s in spec[1:]):
            parser.error("Each viewer spec must be a JSON list of strings containing {image}.")
        name, *command = spec
        runs = [trial(name, command, image, args.idle_seconds) for _ in range(args.runs)]
        report = {"viewer": name, "runs": runs, "median": {key: statistics.median([r[key] for r in runs])
                  for key in ("first_image_ms", "idle_rss_mib", "peak_sampled_rss_mib", "idle_cpu_pct_one_core")}}
        reports.append(report)
    print(json.dumps({"image": str(image), "idle_seconds": args.idle_seconds, "viewers": reports}, indent=2))


if __name__ == "__main__":
    main()
