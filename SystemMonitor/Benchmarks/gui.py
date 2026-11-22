"""Measure monitor GUI startup, CPU and RSS; optionally compare another monitor."""
import argparse
import json
import os
import subprocess
import sys
import time

def sample(pid):
    if sys.platform.startswith("linux"):
        with open(f"/proc/{pid}/stat") as f:
            stat = f.read().rsplit(")", 1)[1].split()
        seconds = (int(stat[11]) + int(stat[12])) / os.sysconf("SC_CLK_TCK")
        rss = int(stat[21]) * os.sysconf("SC_PAGE_SIZE")
        return rss, seconds
    if sys.platform == "win32":
        cmd = f"$p=Get-Process -Id {pid}; Write-Output ($p.WorkingSet64.ToString()+' '+$p.TotalProcessorTime.TotalSeconds.ToString([Globalization.CultureInfo]::InvariantCulture))"
        fields = subprocess.check_output(["powershell", "-NoProfile", "-Command", cmd], text=True).split()
        return int(fields[0]), float(fields[1])
    rss, cpu = subprocess.check_output(["ps", "-p", str(pid), "-o", "rss=,time="], text=True).split()
    seconds = 0
    for x in cpu.split(":"):
        seconds = seconds * 60 + float(x)
    return int(rss) * 1024, seconds

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--exe")
    p.add_argument("--pid", type=int)
    p.add_argument("--compare-pid", type=int, help="PID of Task Manager, Activity Monitor, or another monitor")
    p.add_argument("--compare-name", default="baseline")
    p.add_argument("--seconds", type=int, default=8)
    args = p.parse_args()
    if not args.exe and not args.pid:
        p.error("provide --exe or --pid")
    if args.seconds < 2 or (args.exe and args.seconds > 12):
        p.error("use --seconds 2..12 with --exe, or select an existing PID for longer runs")
    proc = None
    startup = None
    try:
        if args.exe:
            proc = subprocess.Popen([os.path.abspath(args.exe), "--benchmark-idle"], stdout=subprocess.PIPE, text=True)
            line = proc.stdout.readline().strip()
            if line.startswith("READY_MS="):
                startup = int(line.split("=")[1])
            pid = proc.pid
        else:
            pid = args.pid
        pids = {"systemmonitor": pid}
        if args.compare_pid:
            pids[args.compare_name] = args.compare_pid
        samples = {name: [] for name in pids}
        start = time.monotonic()
        while time.monotonic() - start < args.seconds:
            for name, process in pids.items():
                samples[name].append((time.monotonic(), *sample(process)))
            time.sleep(.5)
        result = {}
        for name, rows in samples.items():
            first, last = rows[0], rows[-1]
            result[name] = {
                "pid": pids[name], "startup_ms": startup if name == "systemmonitor" else None,
                "mean_rss_mib": round(sum(s[1] for s in rows) / len(rows) / 1048576, 2),
                "peak_rss_mib": round(max(s[1] for s in rows) / 1048576, 2),
                "cpu_percent_one_core": round(100*(last[2]-first[2])/max(.001,last[0]-first[0]), 3),
            }
        print(json.dumps({"monitors": result, "scope": "Selected GUI processes only. Match refresh interval, visibility, view, workload and display backend."}, indent=2))
    finally:
        if proc and proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)

if __name__ == "__main__":
    main()
