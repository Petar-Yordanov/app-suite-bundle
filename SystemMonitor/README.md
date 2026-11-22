# System Monitor

**Local builds:** See [the App Suite build guide](../LOCAL-BUILD.md) for the auto-detecting SDK bootstrap and direct Xmake commands.

Lightweight Qt Quick system and process monitor. C++20, Qt 6 and Xmake. No
embedded browser, daemon, historical graphs, mandatory account or telemetry.

## Build and run

Install Xmake 3.1.1, Qt 6.8.3 Desktop (Quick, Quick Controls, Network and
Concurrent), and a native C++20 toolchain. Set QT_ROOT_DIR to the directory
containing the Qt kit's bin/qmake. If Xmake already finds Qt, omit --qt.

Windows 11 x64 (Visual Studio 2022 Developer PowerShell):

```powershell
cd SystemMonitor
xmake f -c -y -p windows -a x64 -m release --vs=2022 "--qt=$env:QT_ROOT_DIR"
xmake build -y systemmonitor
xmake run systemmonitor
```

Linux x64 / Apple Silicon macOS:

```bash
cd SystemMonitor
# Linux
xmake f -c -y -p linux -a x86_64 -m release --qt="$QT_ROOT_DIR"
# macOS instead of preceding line:
# xmake f -c -y -p macosx -a arm64 -m release --qt="$QT_ROOT_DIR"
xmake build -y systemmonitor
xmake run systemmonitor
```

Linux requires the standard build-essential toolchain, Qt runtime dependencies
and systemd for service management. macOS requires Xcode CLI tools. Other
architectures can be built with a matching native Qt kit and toolchain but are
not in the supplied CI matrix. No mandatory external runtime dependency beyond Qt.
NVML is loaded optionally from the installed NVIDIA driver.

## Features and usage

Processes: search by name, PID, user, executable or command line; sort by
CPU, memory and other columns. Click a row to inspect command line,
memory, network availability, path and parent PID. Confirm destructive or
resource-changing actions before running them. For priority, choose one of
five classes; CPU affinity takes comma-separated logical CPU IDs. Windows
graceful Close sends WM_CLOSE; force termination works for accessible
console/background processes. End tree uses a child-first snapshot and checks
process identities before every action. The monitor, PID 1, Linux kernel
threads and Windows critical processes are protected.

Resources: live overall/per-core CPU, memory, storage and network totals,
GPU utilization/memory and available temperatures. No history graphs.
Unavailable measurements are shown as unavailable and never replaced by zero.
First samples of cumulative counters need a second sample to calculate rates.

Startup: list supported boot/login entries, enable/disable without deleting the
underlying executable. Services: list and start/stop native service entries
where permitted. Core/boot services may be protected; an administrator may
still be required. Changes are explicit, confirmed and run outside the UI thread.

Default keyboard shortcuts:

| Action | Shortcut |
| --- | --- |
| Refresh active view | F5 |
| Find processes | Ctrl+F |
| Settings | Ctrl+, |

All can be changed. Settings also include theme and refresh interval (250 ms
to 10 seconds). The default interval is 1 second. Expensive process details
are collected every fifth sample.

## Platform capabilities and known limitations

| Capability | Windows 11 | macOS | Linux |
| --- | --- | --- | --- |
| System and per-core CPU | GetSystemTimes / PDH | Mach host CPU info | /proc/stat |
| Physical/virtual memory | Windows memory/commit and pagefile APIs | Mach memory, compressed memory, swap; no global commit counter | /proc/meminfo |
| GPU | WDDM engine and adapter memory PDH, DXGI VRAM capacity, optional NVML | IOAccelerator properties when driver exposes them; shared memory on Apple Silicon | DRM/sysfs, optional NVML |
| Temperatures | NVIDIA NVML; other sensors unavailable without vendor-specific support | Generic public temperature API unavailable | hwmon; optional NVML |
| Disk throughput | PDH | IOKit storage driver counters | /proc/diskstats |
| Network throughput | PDH | route sysctl 64-bit interface counters | /proc/net/dev |
| Process CPU, RAM and identity | Toolhelp/Win32 | libproc | /proc |
| Process disk throughput | Unavailable without ETW attribution | proc_pid_rusage | /proc/PID/io when permitted |
| Process network throughput | Not implemented: requires ETW attribution | Not implemented: no public libproc byte counters | Not implemented: requires eBPF/kernel traffic attribution |
| CPU affinity | Single processor group | Unsupported | sched_setaffinity |
| Startup management | Run registry keys and Startup folders | LaunchAgents | XDG autostart desktop entries |
| Services | SCM start/stop | launchctl third-party jobs; Apple system jobs read-only | systemd start/stop |

Additional details:

- Windows protected processes and other users' processes may hide paths, memory
  or command lines. Windows private commit and macOS virtual address-space size
  are different metrics. Process CPU percentage is normalized to the total
  logical CPU capacity, not percentages above 100% per core.
- Apple Silicon memory is unified, not dedicated VRAM. Driver reporting varies.
  macOS active+wired+compressed memory is an approximation, not the same
  as Activity Monitor's memory-pressure graph. APFS volumes overlap; do not
  sum their capacities. Linux disk aggregate excludes loop, RAM, device-mapper
  and software RAID layers to reduce double counting.
- Network aggregate includes visible non-loopback interfaces; virtual/VPN
  interfaces can count the same traffic twice. NIC arrival, counter reset or
  removal can produce discontinuities.
- Startup management is scoped to the listed mechanisms, not every scheduled
  task, login item or vendor-specific startup facility. Linux system entries
  are disabled for the current user using a Hidden desktop override. User
  entries are backed up before replacement. On Windows native registry values
  are preserved, and Startup files are moved to a sibling backup directory.
  On macOS launchctl manages enable/disable for supported agent labels; the
  entry state is configuration, not necessarily process running state. KeepAlive
  launch jobs can restart after a stop request.
- Process identity is verified using creation time. Linux signals use pidfds
  on supported kernels; Windows controls use an opened process handle. Legacy
  PID-based APIs still have an unavoidable narrow reuse race; tree membership
  is a snapshot, so child processes born during termination may survive.
- Windows automatically attempts above-normal priority. Linux/macOS attempt
  nice -5 and fall back safely if not authorized. Settings can explicitly
  request elevation for the monitor's priority. No real-time scheduling.
  Individual privileged actions use UAC, pkexec (Linux) or macOS authorization,
  with narrowly scoped helper arguments. Install/run only a trusted executable.
  Services follow platform authorization and protections.

## Architecture and performance

- A native C++ sampler per platform; cumulative counter rates use actual
  monotonic elapsed time and discard counter resets. Native collection and
  management run on separate bounded thread pools; sample requests coalesce
  when a previous collection is still running.
- Incremental C++ process model with identity-aware row insertion/removal and
  coalesced changed ranges. Qt Quick ListView uses recycled delegates; no full
  table recreation or QML object per system process.
- No historical storage. Only current and previous snapshots, and bounded
  command-line metadata (up to 8 KiB on Linux/macOS, 4096 UTF-16 units on Windows).
- Default normal refresh is 1 Hz; expensive process metadata updates less often.
  Slow driver calls, permission checks and process enumeration do not run on
  the GUI thread. System resource pressure still depends on OS scheduling and
  available memory; responsiveness cannot be guaranteed when the OS is stalled.

## Automated tests

```bash
xmake build -y systemmonitor-tests
xmake build -y systemmonitor-model-tests
xmake build -y systemmonitor-bench
xmake run systemmonitor-tests
xmake run systemmonitor-model-tests
xmake run systemmonitor-bench
xmake run systemmonitor --smoke-test
```

Native tests operate exclusively on a disposable child process. They verify
counter deltas, system CPU, process identification, stale-PID rejection, self
protection, tree ordering and suspend/resume/termination. Qt tests also exercise
filtering, sorting, 10,000-row incremental updates and persistent model indices.
Smoke tests exercise the QML, filtering and startup enumeration. In CI use
QT_QPA_PLATFORM=offscreen and QT_QUICK_BACKEND=software for smoke tests.

## Performance comparison

The native benchmark outputs mean, p95 and maximum sample duration over 20
samples. Optionally pass sample count and interval in milliseconds:

```bash
xmake run systemmonitor-bench 30 1000
python3 Benchmarks/gui.py --exe /absolute/path/to/systemmonitor
```

To compare a running System Monitor with a running Task Manager, Activity
Monitor or desktop Linux monitor:

```bash
python3 Benchmarks/gui.py --pid 1234 --compare-pid 5678 --compare-name TaskManager --seconds 30
```

Replace the PIDs. Match refresh intervals, visible process views, screen
resolution/display backend and workload. Record RSS (working set on Windows)
and CPU as % of one logical core. Compare an idle desktop and a representative
busy workload. Repeat runs, record startup separately, and note that shared
memory accounting differs across OSes. The script measures only the selected
processes, not their helpers. Keep the workload within available CPU and RAM.
Actual GUI comparisons must be run on target machines; native sampler latency
is not equivalent to full GUI performance.

## Installation and updates

The root build workflow produces a direct .exe installer (Windows x64),
.pkg (macOS ARM64) and .deb (Linux x64, Ubuntu 24.04-compatible). It deploys
Qt and smoke-tests the deployed application before packaging. No nested ZIP.
Local packaging scripts are in Packaging/; require Inno Setup, productbuild
or dpkg-deb/patchelf for the respective platform. macOS packaging is ad-hoc
signed, not notarized.

Use --portable to store configuration in SystemMonitor-data next to the
executable/app bundle. Set PORTABLE=1 while packaging to produce optional
portable archives.

Automatic updates are configurable via a trusted HTTPS manifest. See
Packaging/release-example.json. The updater periodically checks, downloads
and SHA-256 verifies installers; the user explicitly opens the installer.
No network requests are made until an update feed is configured.
