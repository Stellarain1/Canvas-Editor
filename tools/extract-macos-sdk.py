#!/usr/bin/env python3
"""Unpacks a MacOSX<version>.sdk.tar.xz on Windows, where symbolic links cannot be made.

    extract-macos-sdk.py <MacOSX11.3.sdk.tar.xz> <out-dir>

Links become copies, documentation and man pages are skipped (nothing in them is read when
compiling), and Windows-illegal file names are ignored. The result is a sysroot for
cmake/zig-toolchain.cmake (-DCANVAS_MACOS_SDK=<out-dir>/MacOSX11.3.sdk). The SDK is Apple's: it is
only used to compile, never shipped.
"""
import os
import posixpath
import shutil
import sys
import tarfile

SKIP = ("/usr/share/", "/usr/libexec/", "/usr/bin/")


def resolve(name, target):
    if target.startswith("/"):
        return target.lstrip("/")
    return posixpath.normpath(posixpath.join(posixpath.dirname(name), target))


def local(dest, name):
    return os.path.join(dest, *name.split("/"))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        raise SystemExit(1)
    src, dest = sys.argv[1], sys.argv[2]
    os.makedirs(dest, exist_ok=True)
    tar = tarfile.open(src, "r:xz")
    links, count = [], 0
    for member in tar:
        if any(part in member.name for part in SKIP):
            continue
        path = local(dest, member.name)
        if member.isdir():
            os.makedirs(path, exist_ok=True)
        elif member.isreg():
            try:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "wb") as f:
                    shutil.copyfileobj(tar.extractfile(member), f)
                count += 1
            except OSError:
                pass	# a file name Windows refuses (man page names with ::)
        elif member.issym():
            links.append((member.name, resolve(member.name, member.linkname)))
    print("files", count, "symlinks", len(links))

    # copy what each link points at; links can point at links, so go round until nothing changes
    pending = links
    for _ in range(8):
        left = []
        for name, target in pending:
            s, d = local(dest, target), local(dest, name)
            if os.path.exists(d):
                continue
            if os.path.isdir(s):
                shutil.copytree(s, d)
            elif os.path.isfile(s):
                os.makedirs(os.path.dirname(d), exist_ok=True)
                shutil.copyfile(s, d)
            else:
                left.append((name, target))
        if len(left) == len(pending):
            pending = left
            break
        pending = left
    print("unresolved links:", len(pending))

    # a directory link copied early was copied before the links inside it existed: copy it again
    for _ in range(2):
        for name, target in links:
            s, d = local(dest, target), local(dest, name)
            if os.path.isdir(s) and os.path.isdir(d):
                shutil.rmtree(d)
                shutil.copytree(s, d)
    print("done")


if __name__ == "__main__":
    main()
