"""Synthetic recording throughput and process resource benchmark."""
import argparse
import json
import pathlib
import subprocess
import tempfile
import time

import psutil


def benchmark(command, cwd):
    start = time.perf_counter()
    proc = subprocess.Popen(command, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    peak = 0
    cpu = 0.0
    try:
        process = psutil.Process(proc.pid)
        while proc.poll() is None:
            try:
                peak = max(peak, process.memory_info().rss)
                sample = process.cpu_times()
                cpu = max(cpu, sample.user + sample.system)
            except psutil.NoSuchProcess:
                break
            time.sleep(.05)
    except psutil.NoSuchProcess:
        pass
    if proc.wait():
        return {"error": "encoder unavailable or failed"}
    return {"wall_seconds": round(time.perf_counter()-start, 3),
            "sampled_peak_rss_mib": round(peak / 1048576, 2),
            "sampled_cpu_seconds": round(cpu, 3)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--encoder", action="append", default=[])
    parser.add_argument("--resolution", default="1920x1080")
    parser.add_argument("--fps", type=int, default=60)
    parser.add_argument("--seconds", type=int, default=10)
    parser.add_argument("--bitrate", default="12M")
    cfg = parser.parse_args()
    encoders = cfg.encoder or ["libx264"]
    results = {}
    with tempfile.TemporaryDirectory(prefix="screenrecorder-bench-") as tmp:
        for i, encoder in enumerate(encoders):
            name = f"result-{i}.mp4"
            command = [cfg.ffmpeg, "-v", "error", "-y", "-f", "lavfi",
                       "-i", f"testsrc2=size={cfg.resolution}:rate={cfg.fps}",
                       "-c:v", encoder, "-b:v", cfg.bitrate, "-bf", "0", "-t", str(cfg.seconds)]
            if encoder == "libx264":
                command += ["-preset", "veryfast", "-threads", "4"]
            if encoder == "h264_nvenc":
                command += ["-preset", "p4"]
            if encoder == "h264_videotoolbox":
                command += ["-allow_sw", "0", "-realtime", "1"]
            result = benchmark(command + [name], tmp)
            if "wall_seconds" in result:
                result["encoded_fps"] = round(cfg.seconds * cfg.fps / max(.001, result["wall_seconds"]), 2)
                result["file_mib"] = round((pathlib.Path(tmp) / name).stat().st_size / 1048576, 2)
            results[encoder] = result
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
