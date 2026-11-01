#!/usr/bin/env python3
"""Check LSE encodings through static and interpreter instruction generation."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import a64

SAMPLES = {
    "CAS": (0x08A87C14, 0x48A87C14, 0x88A87C14, 0x08E87C14, 0x48E87C14, 0x88E87C14,
            0x08A8FC14, 0x48A8FC14, 0x88A8FC14, 0x08E8FC14, 0x48E8FC14, 0xC8E3FCA4),
    "LSE": (0x38210062, 0x78211062, 0xF8212062, 0xF8213062, 0xF8214062,
            0xF8215062, 0xF8216062, 0xF8217062, 0xF82080A4),
    "LDAPR": (0x38BFC108, 0x78BFC108, 0xB8BFC108, 0xF8BFC108),
}


def main():
    for handler, words in SAMPLES.items():
        helper = {"CAS": "swr_cas_value", "LSE": "swr_lse_op", "LDAPR": "LDAR_("}[handler]
        for word in words:
            index = a64.decode(word)
            assert index >= 0 and a64.TABLE[index][0] == handler, (hex(word), handler)
            assert helper in a64.emit_static(word, 0)
            spec = {name: a64.field(word, name) for name in a64.TABLE[index][3]}
            generator = a64.G(False, spec=spec)
            body = a64.TABLE[index][4](generator)
            assert helper in body, (hex(word), body)
    a64.gen_interp_cases()
    print(f"Decoded {sum(map(len, SAMPLES.values()))} atomic and acquire-load encodings in both code paths.")


if __name__ == "__main__":
    main()
