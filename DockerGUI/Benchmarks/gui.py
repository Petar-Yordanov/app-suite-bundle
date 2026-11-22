"""Measure the GUI PID only, excluding Docker Engine/VM and child CLI processes."""
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
    p.add_argument("--seconds", type=int, default=8)
    args = p.parse_args()
    if not args.exe and not args.pid:
        p.error("provide --exe or --pid")
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
        samples = []
        start = time.monotonic()
        while time.monotonic() - start < args.seconds:
            samples.append((time.monotonic(), *sample(pid)))
            time.sleep(.5)
        first, last = samples[0], samples[-1]
        print(json.dumps({
            "pid": pid, "startup_ms": startup,
            "mean_rss_mib": round(sum(s[1] for s in samples) / len(samples) / 1048576, 2),
            "peak_rss_mib": round(max(s[1] for s in samples) / 1048576, 2),
            "cpu_percent_one_core": round(100*(last[2]-first[2])/max(.001,last[0]-first[0]), 3),
            "scope": "Only the selected GUI process; not Docker Engine, VM, subprocesses or other GUI processes."
        }, indent=2))
    finally:
        if proc and proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)

if __name__ == "__main__":
    main()
