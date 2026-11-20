"""Measure the resident app separately from its QML UI. Requires psutil."""
import argparse
import subprocess
import time
import psutil

parser = argparse.ArgumentParser()
parser.add_argument("executable", help="Path to the built or installed screenshottool executable")
parser.add_argument("--seconds", type=float, default=10)
args = parser.parse_args()
if args.seconds <= 0:
    parser.error("--seconds must be positive")

process = subprocess.Popen([args.executable, "--background", "--benchmark-idle"])
try:
    resident = psutil.Process(process.pid)
    time.sleep(2)
    if process.poll() is not None:
        raise RuntimeError("Screenshot Tool did not remain resident")
    resident.cpu_percent()
    samples = []
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        samples.append(resident.memory_info().rss)
        time.sleep(min(0.25, max(0, deadline - time.monotonic())))
    print(f"Idle CPU: {resident.cpu_percent():.2f}% of one logical CPU")
    print(f"Mean RSS: {sum(samples)/len(samples)/1048576:.2f} MiB")
    print(f"Peak sampled RSS: {max(samples)/1048576:.2f} MiB")
finally:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
