"""Sample a running ScreenRecorder process and its encoder/capture children.

Use equivalent settings for an OBS/other recorder process for comparison.
"""
import argparse
import json
import time

import psutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--seconds", type=float, default=15)
    args = parser.parse_args()
    root = psutil.Process(args.pid)
    observed = {}
    peak = 0
    total_cpu = 0
    start = time.perf_counter()
    while time.perf_counter() - start < args.seconds:
        try:
            processes = [root] + root.children(recursive=True)
        except psutil.NoSuchProcess:
            break
        rss = 0
        live = set()
        for proc in processes:
            try:
                key = (proc.pid, proc.create_time())
                cpu = proc.cpu_times().user + proc.cpu_times().system
                rss += proc.memory_info().rss
                if key in observed:
                    total_cpu += max(0, cpu-observed[key])
                observed[key] = cpu
                live.add(key)
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
        observed = {key: value for key, value in observed.items() if key in live}
        peak = max(peak, rss)
        time.sleep(.2)
    elapsed = time.perf_counter() - start
    print(json.dumps({"elapsed_seconds": round(elapsed, 3),
                      "sampled_peak_process_tree_rss_mib": round(peak / 1048576, 2),
                      "average_cpu_percent_one_core": round(100*total_cpu/max(.001,elapsed), 2)}, indent=2))


if __name__ == "__main__":
    main()
