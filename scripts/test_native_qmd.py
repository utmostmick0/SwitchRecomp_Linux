#!/usr/bin/env python3
"""Build actual native worker/QMD3/memory code with a controlled GPU callback."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    cc = os.environ.get("CC", "gcc")
    flags = ["-fsanitize=address,undefined", "-g", "-ffunction-sections", "-fdata-sections"]
    with tempfile.TemporaryDirectory(prefix="swr-qmd3-checks-") as directory:
        obj = str(Path(directory) / "qmd.o")
        test = str(Path(directory) / "test.o")
        binary = str(Path(directory) / "checks")
        subprocess.run([cc, "-x", "c++", "-std=c++20", *flags,
                        "-I", str(ROOT / "vendor/nx2_imports/gpu_dispatch"),
                        "-c", str(ROOT / "runtime/native_qmd.cpp"), "-o", obj], check=True)
        subprocess.run([cc, "-std=gnu17", *flags, "-I", str(ROOT / "runtime"),
                        "-c", str(ROOT / "scripts/test_native_qmd.c"), "-o", test], check=True)
        subprocess.run([cc, *flags, test, obj, "-Wl,--gc-sections", "-lstdc++", "-pthread", "-lm", "-o", binary], check=True)
        subprocess.run([binary], check=True)


if __name__ == "__main__":
    main()
