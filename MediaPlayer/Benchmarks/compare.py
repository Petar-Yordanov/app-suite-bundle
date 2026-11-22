"""Compare Media Player and standalone VLC on the same local video and desktop.

Requires psutil. Close existing player instances first. A clip must be longer
than warmup + sample time + 5 seconds. The GUI playback workload is not a
headless/video-disabled benchmark.
"""
import argparse
import json
import pathlib
import statistics
import subprocess
import sys
import time
import psutil


def measure(command, warmup, seconds):
    p = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        process = psutil.Process(p.pid)
        time.sleep(warmup)
        if p.poll() is not None:
            raise RuntimeError(f"Exited during warmup: {command[0]}")
        cpu = process.cpu_times()
        cpu_start = cpu.user + cpu.system
        start = time.monotonic()
        rss = []
        while time.monotonic() - start < seconds:
            if p.poll() is not None:
                raise RuntimeError(f"Exited before sample completed: {command[0]}")
            rss.append(process.memory_info().rss / 1048576)
            time.sleep(min(0.25, max(0, seconds - (time.monotonic() - start))))
        cpu = process.cpu_times()
        return {
            "mean_rss_mib": round(statistics.mean(rss), 2),
            "peak_rss_mib": round(max(rss), 2),
            "cpu_percent_of_one_core": round(100 * (cpu.user + cpu.system - cpu_start) / (time.monotonic() - start), 2),
        }
    finally:
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--player", required=True, help="Path to mediaplayer executable (inside .app on macOS)")
    parser.add_argument("--vlc", required=True, help="Path to VLC executable")
    parser.add_argument("--video", required=True, type=pathlib.Path)
    parser.add_argument("--seconds", type=int, default=15)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--software-output", action="store_true", help="Use software output for Media Player; not a native-path comparison")
    args = parser.parse_args()
    if not args.video.is_file() or not (1 <= args.seconds <= 500) or not (1 <= args.warmup <= 60):
        parser.error("Provide a local video and valid positive benchmark durations")
    video = str(args.video.resolve())
    horizon = args.seconds + args.warmup + 5
    player = [args.player]
    if args.software_output:
        player.append("--software-output")
    vlc = [args.vlc, "--ignore-config", "--no-one-instance", "--no-metadata-network-access", "--no-video-title-show", "--no-repeat", "--no-loop"]
    if sys.platform != "darwin":
        vlc.append("--no-qt-privacy-ask")
    print("Benchmarking idle and playback. Keep windows visible and unobscured.", file=sys.stderr)
    results = {
        "sample_seconds": args.seconds,
        "warmup_seconds": args.warmup,
        "video": video,
        "software_output": args.software_output,
        "media_player_idle": measure(player + ["--benchmark-idle", "--benchmark-seconds", str(horizon)], args.warmup, args.seconds),
        "vlc_idle": measure(vlc, args.warmup, args.seconds),
        "media_player_playing": measure(player + ["--benchmark", "--benchmark-seconds", str(horizon), video], args.warmup, args.seconds),
        "vlc_playing": measure(vlc + ["--play-and-exit", video], args.warmup, args.seconds),
    }
    print(json.dumps(results, indent=2))
    # Separate app-instrumented first-frame/decoder counters. VLC GUI does not
    # expose equivalent first-frame timing through its public command line.
    stats = subprocess.run(player + ["--benchmark", "--benchmark-seconds", str(args.seconds), video],
                           capture_output=True, text=True, timeout=args.seconds + 40, check=True)
    print(stats.stdout.strip())


if __name__ == "__main__":
    main()
