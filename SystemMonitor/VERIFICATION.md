# Verification status

The Linux native backend and test suite compiled with GCC C++20, -O2,
-Wall -Wextra -Werror in the available Linux workspace. Tests passed:

- Real native snapshot and CPU counter deltas.
- Real process creation identity and memory.
- Self and stale-PID protection.
- Children-first process-tree traversal with creation-identity checks.
- Suspend, resume and force termination of a disposable child.

The local workspace does not contain a working Xmake or Qt 6 development
SDK. Windows/macOS native compilation, full Qt/QML compilation, UI/model
tests, deployment and live hardware-specific metrics must be verified in CI
and on actual target machines. No claim of measured GUI resource superiority.

The native benchmark in this workspace is intentionally reported separately
from GUI startup/RSS/CPU. The supplied CI builds and tests all three platforms;
the comparison script allows measurements against existing monitors.

Measured Linux sandbox, 20 samples at a 100 ms interval:

| Workload | Process count | Mean | p95 | Max |
| --- | ---: | ---: | ---: | ---: |
| Minimal process namespace | 2 | 0.64 ms | 1.03 ms | 1.88 ms |
| 32 additional disposable sleep processes | 35 | 1.71 ms | 2.27 ms | 11.33 ms |

The sandbox has a limited process namespace and no representative GPU workload,
so these measurements are not a real desktop comparison.

An illustrative command-line comparison (32 additional sleep processes,
20 samples, 100 ms interval, output discarded):

| Tool | Total child CPU time | Wall time |
| --- | ---: | ---: |
| System Monitor native sampler benchmark | 39.5 ms | 2.054 s |
| Linux top in batch mode | 44.7 ms | 2.151 s |

Measured using per-child wait4 resource usage. These workloads report
different fields and top formats a process table; the comparison is not
evidence that the full GUI is faster than top or a desktop monitor.
