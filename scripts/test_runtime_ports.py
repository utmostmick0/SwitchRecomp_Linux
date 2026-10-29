#!/usr/bin/env python3
"""Build and run native service-port checks without launching a game or audio device."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CHECKS = [
    ("shader_dimensions", [], True),
    ("shader_pervertex", [], True),
    ("clock", [], True),
    ("guest_clock", [], True),
    ("platform_thread", [], True),
    ("clock_context", ["runtime/ipc.c"], True),
    ("service_ports", ["runtime/ipc.c", "runtime/audio.c", "runtime/audio_effects.c"], False),
    ("ipc_sessions", ["runtime/ipc.c", "runtime/kernel.c"], True),
    ("audio_effects", ["runtime/audio_effects.c"], True),
    ("audio_routing", ["runtime/audio_effects.c"], True),
    ("splitter_versions", ["runtime/audio_effects.c"], True),
    ("audren_update", ["runtime/audio.c", "runtime/audio_effects.c", "runtime/ipc.c"], True),
    ("hle_fonts", [], True),
    ("font_buttons", [], True),
    ("hle_heaps", [], True),
    ("fs_paths", ["runtime/ipc.c", "runtime/plat.c"], True),
]


def main():
    with tempfile.TemporaryDirectory(prefix="swr-runtime-checks-") as directory:
        for name, sources, address_sanitizer in CHECKS:
            print(f"Checking {name}...", flush=True)
            binary = str(Path(directory) / name)
            sanitizer = "address,undefined,float-cast-overflow" if address_sanitizer else "undefined"
            command = [os.environ.get("CC", "gcc"), "-I", "runtime", "-I", "runtime/opus/include",
                       "-ffunction-sections", "-fdata-sections", f"-fsanitize={sanitizer}", "-g",
                       f"scripts/test_{name}.c", *sources, "-Wl,--gc-sections", "-lm", "-pthread", "-ldl", "-o", binary]
            subprocess.run(command, cwd=ROOT, check=True)
            subprocess.run([binary, directory] if name in ("shader_dimensions", "shader_pervertex") else [binary], cwd=ROOT, check=True)
            if name in ("shader_dimensions", "shader_pervertex"):
                import shutil
                validator = shutil.which("glslangValidator")
                if validator:
                    stage = "vert" if name == "shader_pervertex" else "frag"
                    for shader in Path(directory).glob(f"*.{stage}"):
                        subprocess.run([validator, "-S", stage, str(shader)], check=True)
    print(f"All {len(CHECKS)} native runtime check groups passed.")


if __name__ == "__main__":
    main()
