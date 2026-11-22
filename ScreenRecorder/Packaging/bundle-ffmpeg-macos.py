"""Bundle an installed FFmpeg and recursively relocate non-system dylibs.

Run after macdeployqt, before signing the .app.
"""
import pathlib
import re
import shutil
import subprocess
import sys


def output(*args):
    return subprocess.check_output(args, text=True)


def rpaths(binary):
    text = output("otool", "-l", str(binary))
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset \d+\)", text)


def resolve(dependency, source, initial):
    def expand(name):
        return name.replace("@loader_path", str(source.parent)).replace("@executable_path", str(initial.parent))

    if dependency.startswith("@rpath/"):
        suffix = dependency.removeprefix("@rpath/")
        names = [pathlib.Path(expand(path)) / suffix for path in rpaths(source) + rpaths(initial)]
        names += [source.parent / suffix, pathlib.Path("/opt/homebrew/lib") / suffix]
    else:
        names = [pathlib.Path(expand(dependency))]
    for name in names:
        if name.is_file():
            return name.resolve()
    raise RuntimeError(f"Couldn't resolve {dependency} from {source}")


def dependencies(binary):
    for line in output("otool", "-L", str(binary)).splitlines()[1:]:
        name = line.strip().split(" (compatibility version ", 1)[0]
        if name:
            yield name


def main():
    initial = pathlib.Path(sys.argv[1]).resolve(strict=True)
    app = pathlib.Path(sys.argv[2]).resolve(strict=True)
    dest_dir = app / "Contents/Frameworks/ffmpeg"
    dest_dir.mkdir(parents=True, exist_ok=True)
    executable = app / "Contents/MacOS/ffmpeg"
    queue = [(initial, executable)]
    resolved = {}
    while queue:
        source, dest = queue.pop()
        if source in resolved:
            continue
        resolved[source] = dest
        shutil.copy2(source, dest)
        dest.chmod(0o755)
        # Keep the existing signature until after relocation. Removing it
        # first can leave padding in __LINKEDIT that Apple's tool rejects.
        changes = []
        dylib = dest != executable
        install_id = None
        if dylib:
            ids = output("otool", "-D", str(source)).splitlines()
            install_id = ids[1].strip() if len(ids) > 1 else None
            changes += ["-id", "@rpath/" + dest.name]
        for dep in dependencies(source):
            if dep == install_id or dep.startswith(("/usr/lib/", "/System/Library/")):
                continue
            actual = resolve(dep, source, initial)
            if actual in resolved:
                lib = resolved[actual]
            else:
                lib = dest_dir / actual.name
                for queued_src, queued_dest in queue:
                    if queued_dest == lib and queued_src != actual:
                        raise RuntimeError(f"Dylib filename collision: {actual}")
                if lib.exists():
                    raise RuntimeError(f"Dylib filename collision: {actual}")
                queue.append((actual, lib))
            relocated = ("@loader_path/" if dylib else "@loader_path/../Frameworks/ffmpeg/") + lib.name
            changes += ["-change", dep, relocated]
        if changes:
            subprocess.check_call(["install_name_tool", *changes, str(dest)])
        # Each copied Mach-O is signed individually, including libraries in
        # the nested ffmpeg directory, before the containing app is signed.
        subprocess.check_call(["codesign", "--force", "--sign", "-", str(dest)])
        subprocess.check_call(["codesign", "--verify", "--strict", str(dest)])
    print(f"Bundled FFmpeg with {len(resolved)-1} non-system dylibs")


if __name__ == "__main__":
    main()
