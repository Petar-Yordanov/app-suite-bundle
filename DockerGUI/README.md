# DockerGUI

**Local builds:** See [the App Suite build guide](../LOCAL-BUILD.md) for the auto-detecting SDK bootstrap and direct Xmake commands.

Native Qt Quick desktop frontend for an existing Docker engine. No Chromium,
Electron, embedded web UI, engine installer, bundled VM, application account or
telemetry.

## Start here

You need Docker CLI and a reachable engine (and the Docker Compose v2 CLI
plugin for Compose). The installer deliberately does not install Docker.

* **Linux:** Docker Engine with access to its Unix socket (rootless or system
  engine). The GUI can request startup of docker.service using systemd.
* **macOS:** an existing runtime such as Colima. For example, if you already
  installed Colima and Docker CLI, select the `colima` context; the frontend can
  start that profile. It can also invoke the supported Docker Desktop CLI
  start command when a Desktop runtime is explicitly selected.
* **Windows:** an existing Windows Docker service or Docker-compatible Linux
  runtime. For WSL, the distribution must already have Docker installed and
  expose a reachable Docker endpoint. Starting its service does not set up
  network forwarding or expose a new Docker socket. Use the existing context.
  For Desktop contexts, the frontend attempts `docker desktop start`.

Docker on macOS and Windows still needs its existing Linux VM/WSL runtime for
Linux containers. This application does not replace that runtime and cannot
promise lower engine/VM resource consumption. It only replaces the GUI.

Run `docker context ls` and `docker info` to check your setup. If no CLI is
found, configure PATH or install the CLI appropriate for your existing engine.
For SSH remote contexts, configure key-based authentication and host trust
before opening this GUI. Use Docker CLI to configure TLS contexts, certificates
and remote endpoints. The app never opens a daemon port or requests elevation.

## Local development

Prerequisites: C++20 compiler, Xmake (CI pins 3.1.1), Qt 6.8.3 desktop SDK
(Core, Gui, Widgets, Qml, Quick, Quick Controls, Network and Concurrent),
Docker CLI and optionally a test engine. No external SDK for Docker is needed.

Run commands from the repository's **DockerGUI** directory.

### Windows x64 (PowerShell, MSVC 2022)

```powershell
cd DockerGUI
$env:QT_ROOT_DIR = 'C:\Qt\6.8.3\msvc2022_64'
xmake f -c -y -p windows -a x64 -m release --vs=2022 "--qt=$env:QT_ROOT_DIR"
xmake build -y dockergui
xmake run dockergui
```

### Linux x64 (GCC or Clang)

```bash
cd DockerGUI
export QT_ROOT_DIR="$HOME/Qt/6.8.3/gcc_64"
xmake f -c -y -p linux -a x86_64 -m release --qt="$QT_ROOT_DIR"
xmake build -y dockergui
xmake run dockergui
```

### macOS ARM64 (Xcode command-line tools)

```bash
cd DockerGUI
export QT_ROOT_DIR="$HOME/Qt/6.8.3/macos"
xmake f -c -y -p macosx -a arm64 -m release --qt="$QT_ROOT_DIR"
xmake build -y dockergui
xmake run dockergui
```

Other architectures use the corresponding Qt SDK, compiler and Xmake arch.
The included CI and installer scripts intentionally target Windows x64,
Linux x64 and macOS ARM64 only.

## Usage

Choose a Docker context at the top. Selecting a context applies a per-command
`--context` without changing your Docker CLI's global default. The Environment
entry preserves existing DOCKER_HOST/DOCKER_CONTEXT settings. Use **Discover**
after adding a context using the CLI.

* **Containers:** create, run, inspect, start/stop/restart, pause/resume, remove,
  update restart policy, follow live logs, open a native interactive terminal,
  browse directories, copy files in/out, inspect writable-layer and rootfs size.
* **Images:** pull, push, tag, inspect, remove, build from a local Dockerfile and
  context, with streaming command output. Image removal uses the selected tag
  when possible, avoiding unnecessary removal of other tags.
* **Volumes/networks:** create, inspect, list and delete.
* **Compose:** list projects, deploy, stop/start, down and view logs. Supply local
  Compose file paths and an explicit project name. Compose files can be multiple
  newline-separated paths.
* **Statistics:** CPU %, RAM use/limit, network traffic, block I/O and PIDs.
* **Storage:** engine-reported resource counts, active/reclaimable sizes. The
  storage view refreshes at a minimum interval of 30 seconds.
* **Registry:** sign in/out, using an existing Docker credential helper.
* **Prune:** explicit confirmation for a selected resource group or system
  prune. Docker enforces resource-in-use restrictions. The explicit all-unused
  option includes unused tagged images or named volumes. No automatic resource
  deletion.

Logs and build output are limited to the most recent 256 Ki characters, with
batched UI updates. A command's output exceeding 16 MiB for a non-streaming
request returns an error; directory browsing is limited to 50,000 entries.
Cancel operation kills the local CLI client; some engine-side actions may
continue. Refresh to see the actual state.

Container environment variables, mounts, port mappings and command are
configured when creating a container. Docker does not support changing these
in place; explicitly recreate the container when these need to change. The
existing-container Configure action updates the restart policy without data
loss. Inspect displays environment variables, so treat it as potentially
sensitive.

Container filesystem browsing runs `find` and `sh` inside a running Linux
container and returns only metadata. It supports navigating directories and
explicit copy operations; stopped containers can still be copied using
`docker cp`. Distroless images, Windows containers or containers with
restricted tools/permissions may not support the directory browser. A shell
appropriate for the image can be specified when opening Terminal. Terminal
sessions run in an existing native terminal with a genuine TTY rather than a
simulated QML terminal. macOS may ask for Terminal automation permission.
No FileManager source was available for this build, so the standalone browser
does not link it; a future file manager can use the backend's copy/list logic.

For settings portability, start the installed or unpacked binary with
`--portable`. Settings are saved next to the executable (or beside the macOS
app bundle). Docker contexts and credentials continue using Docker's own
configuration; they are not duplicated in the portable directory.

Default shortcuts: Ctrl+R refresh, Ctrl+, settings. Both are configurable.
Light, Dark and System themes are available.

### Registry credentials

Configure `credsStore` or a registry-specific `credHelpers` entry in Docker's
`config.json` and install the corresponding `docker-credential-*` executable.
For example, the operating-system credential helper for your platform or the
credential helper supplied by your runtime. GUI login refuses to proceed
without a configured, accessible helper; it sends the password/token on
stdin, never as a command argument. No password is written to application
settings or output. Existing Docker CLI credentials are reused as-is.

### Updates

Configure a trusted HTTPS manifest URL in Settings. The app checks automatically
every six hours, downloads only an applicable newer artifact, verifies SHA-256
and offers the installer to the user. There is no public update service bundled
and no installer is executed without confirmation. Use
`Packaging/release-example.json` as a template.

## Tests

```bash
xmake build -y dockergui-tests
xmake run dockergui-tests
xmake build -y dockergui-qt-tests
xmake run dockergui-qt-tests
xmake build -y dockergui-integration-tests
xmake run dockergui --smoke-test
```

The HTTP parser tests need only a C++20 compiler:

```bash
g++ -std=c++20 -Wall -Wextra Tests/HttpTests.cpp -o /tmp/dockergui-tests
/tmp/dockergui-tests
```

For actual integration tests, use an accessible Linux-container **test**
engine, with Docker Compose installed:

```bash
docker info
docker compose version
xmake run dockergui-integration-tests --run
```

This pulls Alpine, builds an image, creates a uniquely named temporary volume,
network, container and Compose project, tests lifecycle, API/CLI listing, logs,
copy, inspect, stats, storage and Compose, then removes only its named resources.
It does not prune or stop unrelated containers. It leaves the Alpine base
image/build cache under Docker control.

## Benchmarks

Build release mode. Run the GUI with normal renderer on the same screen and
same engine as the comparison application.

```bash
python3 Benchmarks/gui.py --exe /path/to/built/dockergui
```

Or sample the actual GUI PID in a connected, idle normal window (also works
for the Docker Desktop GUI process):

```bash
python3 Benchmarks/gui.py --pid 12345 --seconds 30
```

Metrics: GUI startup (ready-to-render QML creation), mean/peak resident
memory, CPU time normalized to one core. The measurement excludes the engine,
underlying VM and CLI children. For a complete comparison with a multi-process
GUI, measure and sum its GUI-related processes separately. RSS may include
shared libraries and is not equivalent to unique/private memory. Compare
identical workloads and refresh intervals, and report platform/runtime versions.
The Qt tests also report initial and unchanged model update time for 50,000 rows.
No unmeasured performance figures are claimed.

## Architecture

The C++ Backend interface supplies async resource listing, commands and
context selection; Controller accepts an injected implementation. DockerBackend
uses read-only Engine HTTP API requests over explicit local Unix sockets for
container/image/volume/network listings, falling back to asynchronously
invoked Docker CLI for Windows named pipes, unsupported routes and remote
contexts. CLI handles credentials, SSH/TLS, Compose, build, exec and mutations.
No raw daemon TCP endpoints are opened by the app, and there is no custom
authentication or TLS implementation.

One periodic refresh in flight; switching the page invalidates stale results.
The resource model preserves IDs, updates changed rows and inserts/removes
delta rows rather than resetting or instantiating one delegate per resource.
Polling pauses while the window is minimized, and live logs stop when leaving
their view. No persistent stats collection or history. Expensive views are
queried only when selected. An alternative backend can implement the interface
and translate the controller's Docker-compatible operations; no Podman backend
is currently exposed.

## Packaging and CI

Extract the source ZIP at your app-suite repository root. Included workflow
`.github/workflows/build.yml` preserves the existing application steps and
adds DockerGUI. It assumes the previous app directories remain in the repo.
It builds and uploads direct installers, with no nested ZIP artifacts.

For packaging locally, after building:

* Windows: Inno Setup 6, MSVC redistributable, Qt SDK; run
  `./Packaging/package-windows.ps1` in PowerShell.
* macOS: Qt SDK, Xcode packaging tools; run
  `bash Packaging/package-macos.sh`. The generated app is ad-hoc signed but
  not notarized.
* Linux: Qt SDK, patchelf, dpkg-deb; run
  `bash Packaging/package-linux.sh`. The Debian package targets Ubuntu 24.04
  or an ABI-compatible distribution.

Optional `PORTABLE=1` creates a separate portable archive (CI leaves it off).
The official CI does native compilation, parser/model tests and QML smoke tests
on the three target platforms, plus a real Docker integration suite, GUI
benchmark and installer deployment smoke test on Linux. Installer smoke tests
are also included on Windows and macOS.
