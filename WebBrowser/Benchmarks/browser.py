#!/usr/bin/env python3
"""Measure isolated browser process trees against the same local page workload."""
import argparse
import http.server
import json
import statistics
import subprocess
import tempfile
import threading
import time
import psutil

PAGE = b"""<!doctype html><meta charset=utf-8><title>Browser benchmark</title>
<style>body{font:16px sans-serif}p{padding:5px;background:#def}canvas{width:480px}</style>
<h1>Browser workload</h1><canvas width=480 height=180></canvas><div id=rows></div>
<script>
let f=document.createDocumentFragment();
for(let i=0;i<1500;i++){let p=document.createElement('p');p.textContent='Benchmark row '+i;f.append(p)}
rows.append(f);let c=document.querySelector('canvas').getContext('2d');
for(let i=0;i<120;i++){c.fillStyle='hsl('+i*3+',80%,50%)';c.fillRect(i*4,0,4,180)}
</script>"""

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(PAGE)))
        self.end_headers()
        self.wfile.write(PAGE)
    def log_message(self, *args):
        pass

def sample(root):
    rss = uss = cpu = 0
    all_uss = True
    processes = [root] + root.children(recursive=True)
    for proc in processes:
        try:
            rss += proc.memory_info().rss
            if hasattr(proc, "memory_full_info"):
                try:
                    uss += proc.memory_full_info().uss
                except (AttributeError, psutil.AccessDenied, psutil.NoSuchProcess):
                    all_uss = False
            else:
                all_uss = False
            times = proc.cpu_times()
            cpu += times.user + times.system
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return rss/2**20, uss/2**20 if all_uss else None, cpu, len(processes)

def measure(command, seconds, label):
    start = time.monotonic()
    with tempfile.TemporaryFile() as log:
        p = subprocess.Popen(command, stdout=log, stderr=log)
        root = psutil.Process(p.pid)
        try:
            time.sleep(8)
            if p.poll() is not None:
                log.seek(0)
                raise RuntimeError(f"{label} exited: {log.read().decode(errors='replace')[-4000:]}")
            values = []
            first = sample(root)
            begin = time.monotonic()
            for _ in range(seconds):
                time.sleep(1)
                values.append(sample(root))
            dt = time.monotonic()-begin
            log.seek(0)
            ready = next((line.split()[-1] for line in log.read().decode(errors="replace").splitlines() if "READY_MS" in line), None)
            return {
                "browser":label, "ready_ms":int(ready) if ready and ready.isdigit() else None,
                "median_tree_rss_mib":round(statistics.median(v[0] for v in values),2),
                "peak_tree_rss_mib":round(max(v[0] for v in values),2),
                "median_tree_uss_mib":round(statistics.median(v[1] for v in values),2) if all(v[1] is not None for v in values) else None,
                "idle_cpu_percent_one_core":round(max(0,values[-1][2]-first[2])/dt*100,2),
                "processes":values[-1][3], "elapsed_s":round(time.monotonic()-start,2)
            }
        finally:
            try:
                children=root.children(recursive=True)
                for proc in reversed(children):
                    try: proc.terminate()
                    except psutil.NoSuchProcess: pass
                p.terminate()
                p.wait(timeout=5)
            except (psutil.NoSuchProcess, subprocess.TimeoutExpired):
                if p.poll() is None: p.kill()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--exe",required=True)
    parser.add_argument("--compare-exe")
    parser.add_argument("--compare-arg",action="append",default=[],help="Repeat; {profile} is replaced with an isolated profile directory")
    parser.add_argument("--tabs",type=int,default=5)
    parser.add_argument("--seconds",type=int,default=15)
    a=parser.parse_args()
    if not 1<=a.tabs<=128 or a.seconds<1: parser.error("tabs must be 1–128, seconds positive")
    server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    urls=[f"http://127.0.0.1:{server.server_port}/?tab={i}" for i in range(a.tabs)]
    try:
        results=[measure([a.exe,"--benchmark",*urls],a.seconds,"WebBrowser")]
        if a.compare_exe:
            with tempfile.TemporaryDirectory() as profile:
                if not any("{profile}" in x for x in a.compare_arg):
                    parser.error("comparison requires isolated profile via --compare-arg containing {profile}")
                results.append(measure([a.compare_exe,*[x.replace("{profile}",profile) for x in a.compare_arg],*urls],a.seconds,"Comparison"))
        print(json.dumps({"tabs":a.tabs,"warmup_seconds":8,"sample_seconds":a.seconds,"rss_may_double_count_shared_pages":True,"results":results},indent=2))
    finally:
        server.shutdown()

if __name__=="__main__":
    main()
