#!/usr/bin/env python3
"""Build and run native service-port checks without launching a game or audio device."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CHECKS = [
    ("shader_dimensions", [], True),
    ("clock", [], True),
    ("lse_atomics", [], True),
    ("service_ports", ["runtime/ipc.c", "runtime/audio.c", "runtime/audio_effects.c"], False),
    ("ipc_sessions", ["runtime/ipc.c", "runtime/kernel.c"], True),
    ("audio_effects", ["runtime/audio_effects.c"], True),
    ("audio_routing", ["runtime/audio_effects.c"], True),
    ("audren_update", ["runtime/audio.c", "runtime/audio_effects.c", "runtime/ipc.c"], True),
    ("hle_fonts", [], True),
    ("hle_heaps", [], True),
    ("fs_paths", ["runtime/ipc.c", "runtime/plat.c"], True),
]


def main():
    subprocess.run([sys.executable, "scripts/test_lse_decode.py"], cwd=ROOT, check=True)
    with tempfile.TemporaryDirectory(prefix="swr-runtime-checks-") as directory:
        for name, sources, address_sanitizer in CHECKS:
            print(f"Checking {name}...", flush=True)
            binary = str(Path(directory) / name)
            sanitizer = "address,undefined,float-cast-overflow" if address_sanitizer else "undefined"
            command = [os.environ.get("CC", "gcc"), "-I", "runtime", "-I", "runtime/opus/include",
                       "-ffunction-sections", "-fdata-sections", f"-fsanitize={sanitizer}", "-g",
                       f"scripts/test_{name}.c", *sources, "-Wl,--gc-sections", "-lm", "-pthread", "-ldl", "-o", binary]
            subprocess.run(command, cwd=ROOT, check=True)
            subprocess.run([binary, directory] if name == "shader_dimensions" else [binary], cwd=ROOT, check=True)
            if name == "shader_dimensions":
                import shutil
                validator = shutil.which("glslangValidator")
                if validator:
                    for shader in Path(directory).glob("*.frag"):
                        subprocess.run([validator, "-S", "frag", str(shader)], check=True)
    print(f"All {len(CHECKS)} native runtime check groups passed.")


if __name__ == "__main__":
    main()
