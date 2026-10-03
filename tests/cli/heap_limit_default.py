#!/usr/bin/env python3
"""CLI check: the default heap ceiling is 75 % of the memory protoJS may use.

protojs prints its effective ceiling (protoCore.gcStats().heapLimitCells) and
this script computes the expected value on its own: 75 % of physical memory,
or of the cgroup memory limit on Linux when one is set and smaller, in cells
of 64 bytes (src/HeapLimit.h). It also checks that PROTOCORE_HEAP_LIMIT_CELLS
overrides the default.

Usage: heap_limit_default.py <path-to-protojs>
"""
import os
import subprocess
import sys

INT_MAX = 2**31 - 1


def physical_bytes():
    if sys.platform.startswith("win"):
        import ctypes
        from ctypes import wintypes

        class MEMORYSTATUSEX(ctypes.Structure):
            _fields_ = [("dwLength", wintypes.DWORD), ("dwMemoryLoad", wintypes.DWORD),
                        ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                        ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong),
                        ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong),
                        ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]
        st = MEMORYSTATUSEX()
        st.dwLength = ctypes.sizeof(st)
        ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(st))
        return st.ullTotalPhys
    if sys.platform == "darwin":
        out = subprocess.run(["sysctl", "-n", "hw.memsize"], capture_output=True, text=True)
        return int(out.stdout.strip())
    return os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE")


def cgroup_limit_bytes():
    if not sys.platform.startswith("linux"):
        return 0
    best = 0
    try:
        lines = open("/proc/self/cgroup").read().splitlines()
    except OSError:
        return 0
    for line in lines:
        parts = line.split(":", 2)
        if len(parts) != 3:
            continue
        controllers, path = parts[1], parts[2]
        if controllers == "":
            root, name = "/sys/fs/cgroup", "memory.max"
        elif "memory" in controllers.split(","):
            root, name = "/sys/fs/cgroup/memory", "memory.limit_in_bytes"
        else:
            continue
        while True:
            f = root + path + ("" if path == "/" else "/") + name
            try:
                text = open(f).read().split()[0]
                if text != "max":
                    v = int(text)
                    if 0 < v < 2**60 and (best == 0 or v < best):
                        best = v
            except (OSError, ValueError, IndexError):
                pass
            if path in ("", "/"):
                break
            path = path.rsplit("/", 1)[0] or "/"
    return best


def effective_limit(protojs, env):
    out = subprocess.run([protojs, "-e", "console.log(protoCore.gcStats().heapLimitCells)"],
                         capture_output=True, text=True, env=env, timeout=120)
    if out.returncode != 0:
        print("FAIL: protojs exited %d: %s" % (out.returncode, out.stderr.strip()))
        sys.exit(1)
    return int(out.stdout.strip().splitlines()[-1])


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    protojs = sys.argv[1]
    env = dict(os.environ)
    env.pop("PROTOCORE_HEAP_LIMIT_CELLS", None)

    phys, cg = physical_bytes(), cgroup_limit_bytes()
    base = phys if cg == 0 or (phys and phys < cg) else cg
    expected = min(base // 4 * 3 // 64, INT_MAX) if base else 10_000_000
    got = effective_limit(protojs, env)
    print("physical %d bytes, cgroup limit %d bytes -> expected %d cells, protojs %d cells"
          % (phys, cg, expected, got))
    if got != expected:
        print("FAIL: the default heap ceiling is not 75 % of the available memory")
        return 1

    env["PROTOCORE_HEAP_LIMIT_CELLS"] = "1234567"
    got = effective_limit(protojs, env)
    if got != 1234567:
        print("FAIL: PROTOCORE_HEAP_LIMIT_CELLS=1234567 gave %d" % got)
        return 1
    print("heap-limit-default: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
