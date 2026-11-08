#!/usr/bin/env python3
"""Packs a finished build into a release zip.

    package.py windows <build-dir> <out.zip>
    package.py linux   <build-dir> <out.zip>
    package.py macos   <build-dir-with-CanvasEditor.app> <out.zip> [--cli <canvas-cli>]
    (linux / macos) [--ort <libonnxruntime.so / .dylib>] adds the ONNX Runtime for the tempo finder

The layout matches what the editor expects next to its executable: res/ and the licences. Nothing from the machine
that built it (settings, auto saves, libraries) ever goes in. Executables keep their execute bit
inside the zip so unzipping on Linux / macOS gives something that runs.
"""
import os
import re
import stat
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def version():
    text = open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8").read()
    suffix = re.search(r'set\(CANVAS_VERSION_SUFFIX "([^"]*)"\)', text)
    return re.search(r"project\(CanvasEditor VERSION ([0-9.]+)", text).group(1) + (suffix.group(1) if suffix else "")


class Zip:
    def __init__(self, path, top):
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        if os.path.exists(path):
            os.remove(path)
        self.zip = zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9)
        self.top = top

    def add_file(self, src, dest, executable=False):
        info = zipfile.ZipInfo.from_file(src, os.path.join(self.top, dest).replace("\\", "/"))
        mode = 0o755 if executable else 0o644
        info.external_attr = (stat.S_IFREG | mode) << 16
        info.create_system = 3	# "made on Unix": unzip honours the permission bits (the execute bit)
        info.compress_type = zipfile.ZIP_DEFLATED
        with open(src, "rb") as f:
            self.zip.writestr(info, f.read(), compresslevel=9)

    def add_text(self, dest, text):
        info = zipfile.ZipInfo(os.path.join(self.top, dest).replace("\\", "/"))
        info.external_attr = (stat.S_IFREG | 0o644) << 16
        info.create_system = 3
        info.compress_type = zipfile.ZIP_DEFLATED
        self.zip.writestr(info, text)

    def add_tree(self, src, dest, executable_names=(), skip=()):
        for base, dirs, files in os.walk(src):
            dirs.sort()
            for name in sorted(files):
                path = os.path.join(base, name)
                rel = os.path.relpath(path, src)
                if rel.replace("\\", "/") in skip:
                    continue
                self.add_file(path, os.path.join(dest, rel), executable=name in executable_names)

    def close(self):
        self.zip.close()


def common(z):
    z.add_tree(os.path.join(ROOT, "Licenses"), "Licenses")
    for name in ("LICENSE", "README.md"):
        z.add_file(os.path.join(ROOT, name), name)


def add_ort(z, ort, libname):
    # the Windows onnxruntime.dll is skipped for other systems; their own runtime goes in under the
    # name BeatDetect looks for. Without one the editor just says the tempo finder is not available.
    if ort:
        z.add_file(ort, libname)
    else:
        print("warning: no ONNX Runtime library given, BPM detection will say not available")


def pack_desktop(kind, build, out, ort=None):
    name = "CanvasEditor-%s-%s" % (version(), "windows-x64" if kind == "windows" else "linux-x64")
    z = Zip(out, name)
    exe = ".exe" if kind == "windows" else ""
    for tool in ("CanvasEditor", "canvas-cli"):
        path = os.path.join(build, tool + exe)
        if not os.path.isfile(path):
            raise SystemExit("%s was not built" % path)
        z.add_file(path, tool + exe, executable=True)
    # from the sources, so files left in a build folder by older versions never ship
    z.add_tree(os.path.join(ROOT, "MikuMikuWorld", "res"), "res", skip=() if kind == "windows" else ("ort/onnxruntime.dll",))
    if kind == "linux":
        add_ort(z, ort, "res/ort/libonnxruntime.so")
    common(z)
    if kind == "linux":
        z.add_text("HOW TO RUN.txt",
                   "Needs: a desktop with X11 or XWayland, OpenGL 3.3, and libX11 / libXrandr / libXinerama / libXcursor / libXi / libGL\n"
                   "(any normal Linux desktop already has them).\n\n"
                   "    chmod +x CanvasEditor\n    ./CanvasEditor\n\n"
                   "Video export wants ffmpeg: the export window has an Install ffmpeg button.\n")
    z.close()
    return name


def pack_macos(build, out, cli, ort=None):
    arch = "arm64" if "arm" in os.path.basename(out) else "x86_64"
    name = "CanvasEditor-%s-macos-%s" % (version(), arch)
    z = Zip(out, name)
    app = os.path.join(build, "CanvasEditor.app")
    if not os.path.isdir(app):
        raise SystemExit("%s is missing" % app)
    for base, dirs, files in os.walk(app):
        dirs.sort()
        for fname in sorted(files):
            path = os.path.join(base, fname)
            rel = os.path.relpath(path, build)
            if rel.replace("\\", "/").endswith("res/ort/onnxruntime.dll"):
                continue
            executable = os.path.basename(os.path.dirname(path)) == "MacOS"
            z.add_file(path, rel, executable=executable)
    add_ort(z, ort, "CanvasEditor.app/Contents/MacOS/res/ort/libonnxruntime.dylib")
    if cli:
        z.add_file(cli, "canvas-cli", executable=True)
    common(z)
    z.add_text("HOW TO OPEN.txt",
               "Drag CanvasEditor.app to Applications. The first time, right-click it and choose Open\n"
               "(it is not signed with an Apple developer certificate, so a double click is refused once).\n\n"
               "If macOS says the app is damaged, run this once in Terminal and open it again:\n\n"
               "    xattr -dr com.apple.quarantine /Applications/CanvasEditor.app\n\n"
               "Minimum: macOS 12 (Monterey), on Apple silicon or Intel.\n"
               "Video export wants ffmpeg: the export window has an Install ffmpeg button.\n")
    z.close()
    return name


def main():
    args = sys.argv[1:]
    if len(args) < 3:
        print(__doc__)
        raise SystemExit(1)
    kind, build, out = args[0], args[1], args[2]
    ort = args[args.index("--ort") + 1] if "--ort" in args else None
    if kind in ("windows", "linux"):
        name = pack_desktop(kind, build, out, ort)
    elif kind == "macos":
        cli = None
        if "--cli" in args:
            i = args.index("--cli")
            cli = args[i + 1]
        name = pack_macos(build, out, cli, ort)
    else:
        raise SystemExit("unknown kind " + kind)
    print("wrote %s (%s, %.1f MB)" % (out, name, os.path.getsize(out) / 1048576))


if __name__ == "__main__":
    main()
