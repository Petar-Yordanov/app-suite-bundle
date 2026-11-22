# Arky 0.8.2 — rename from StreamArk

This release is the same Qt 6/QML + libarchive project rebranded as **Arky**.

- Xmake project/target is `Arky`, producing `Arky.exe` on Windows.
- Application/window/dialog titles and error logs use Arky.
- Native C++ namespace changed from `streamark` to `arky`, including tests.
- Native clipboard MIME types now include `application/vnd.arky.entries+json` and
  `application/vnd.arky.virtualfiles`. For interoperability, the previous
  `application/vnd.streamark.entries+json` is still emitted as an alias.
- Deployment scripts and CI invoke `Arky` and package under `dist/Arky`.
- The tests, source code, README and existing historical notes are carried over.
- Version advanced to 0.8.2.

Build on Windows in the x64 Native Tools terminal:

```powershell
xmake f -p windows -a x64 -m release -y
xmake -r
xmake run Arky
```

This environment cannot verify the Windows GUI. Check the Qt UI on target Windows after building.
