#!/usr/bin/env python3
"""Downloads the X11 development headers GLFW needs to cross-compile the Linux build from another OS.

    fetch-x11-headers.py <out-dir>        # -> <out-dir>/include/X11/..., <out-dir>/include/...

Only headers are taken (from Debian's libx11-dev, x11proto-dev, libxrandr-dev, libxinerama-dev,
libxcursor-dev, libxi-dev, libxext-dev, libxrender-dev and libxfixes-dev packages). GLFW opens the
X11 libraries with dlopen when the program starts, so nothing from them is linked: the finished
binary runs on any desktop that has X11 or XWayland.
"""
import io
import lzma
import os
import sys
import tarfile
import urllib.request

MIRROR = "https://deb.debian.org/debian/"
SUITE = "bookworm"
PACKAGES = ["libx11-dev", "x11proto-dev", "libxrandr-dev", "libxinerama-dev", "libxcursor-dev",
            "libxi-dev", "libxext-dev", "libxrender-dev", "libxfixes-dev"]


def fetch(url):
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def package_index():
    data = lzma.decompress(fetch("%sdists/%s/main/binary-amd64/Packages.xz" % (MIRROR, SUITE)))
    entries = {}
    for block in data.decode("utf-8", "replace").split("\n\n"):
        fields = {}
        for line in block.splitlines():
            if ": " in line and not line.startswith(" "):
                key, value = line.split(": ", 1)
                fields[key] = value
        if "Package" in fields:
            entries[fields["Package"]] = fields
    return entries


def ar_members(blob):
    assert blob[:8] == b"!<arch>\n"
    pos = 8
    while pos + 60 <= len(blob):
        name = blob[pos:pos + 16].decode().strip()
        size = int(blob[pos + 48:pos + 58].decode())
        yield name, blob[pos + 60:pos + 60 + size]
        pos += 60 + size + (size & 1)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        raise SystemExit(1)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    index = package_index()
    for package in PACKAGES:
        entry = index[package]
        blob = fetch(MIRROR + entry["Filename"])
        for name, payload in ar_members(blob):
            if not name.startswith("data.tar"):
                continue
            if name.endswith(".xz"):
                payload = lzma.decompress(payload)
            tar = tarfile.open(fileobj=io.BytesIO(payload))
            count = 0
            for member in tar.getmembers():
                path = member.name.lstrip("./")
                if not path.startswith("usr/include/") or not member.isfile():
                    continue
                target = os.path.join(out, path[len("usr/"):])
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, "wb") as f:
                    f.write(tar.extractfile(member).read())
                count += 1
            print("%s %s: %d headers" % (package, entry["Version"], count))


if __name__ == "__main__":
    main()
