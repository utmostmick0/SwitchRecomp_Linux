#!/usr/bin/env python3
"""Compile the real imported frontend and validate its C runtime boundary."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FRONTEND = ROOT / "vendor/nx2_imports/ampere"


def main():
    cc = os.environ.get("CC", "gcc")
    with tempfile.TemporaryDirectory(prefix="swr-ns2-checks-") as directory:
        output = Path(directory)
        objects = []
        flags = ["-fsanitize=address,undefined", "-g", "-ffunction-sections", "-fdata-sections"]
        sources = [ROOT / "runtime/ns2_shader.cpp"] + [
            FRONTEND / "shader_recompiler/frontend/ampere" / name
            for name in ("probe.cpp", "lower_bridge.cpp")]
        for index, source in enumerate(sources):
            obj = output / f"{index}.o"
            subprocess.run([cc, "-x", "c++", "-std=c++20", *flags,
                            "-I", str(ROOT / "runtime"), "-I", str(ROOT / "runtime/ns2_compat"),
                            "-I", str(FRONTEND), "-c", str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        test_obj = output / "test.o"
        subprocess.run([cc, "-std=gnu17", *flags, "-c", str(ROOT / "scripts/test_ns2_shader.c"),
                        "-o", str(test_obj)], check=True)
        binary = output / "ns2_shader"
        subprocess.run([cc, *flags, str(test_obj), *objects, "-Wl,--gc-sections",
                        "-lstdc++", "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary), directory], check=True)
        validator = shutil.which("glslangValidator")
        if validator:
            for suffix in ("vert", "frag", "comp"):
                subprocess.run([validator, str(output / f"native.{suffix}")], check=True)


if __name__ == "__main__":
    main()
