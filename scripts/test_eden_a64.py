#!/usr/bin/env python3
"""Check Eden's ARMv8.1-8.5 instruction additions in both A64 code paths."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import a64


def encode(name, fields):
    index = next(i for i, entry in enumerate(a64.TABLE) if entry[0] == name)
    _, _, value, _, _ = a64.TABLE[index]
    for field, part in fields.items():
        lo, width = a64.FIELDS[field][:2]
        value |= (part & ((1 << width) - 1)) << lo
    assert a64.decode(value) == index, (name, hex(value), a64.TABLE[a64.decode(value)][0])
    return value, index


def check(name, fields, snippets=(), dynamic_snippets=()):
    word, index = encode(name, fields)
    code = a64.emit_static(word, 0x1000)
    assert code is not None, (name, hex(word))
    for snippet in snippets:
        assert snippet in code, (name, code, snippet)
    spec = {field: a64.field(word, field) for field in a64.TABLE[index][3]}
    dynamic = a64.TABLE[index][4](a64.G(False, spec=spec))
    assert dynamic is not None, (name, hex(word))
    for snippet in dynamic_snippets:
        assert snippet in dynamic, (name, dynamic, snippet)
    return code, dynamic


def main():
    check('CASP', {'casp_sz': 0, 'L': 1, 'casp_o0': 1, 'rs': 2, 'rn': 3, 'rt': 4},
          ('swr_cas_value',))
    check('CASP', {'casp_sz': 1, 'L': 1, 'casp_o0': 1, 'rs': 2, 'rn': 3, 'rt': 4},
          ('swr_cas128_value',))
    check('PACAUT_REG', {'pac_z': 1, 'pac_op': 0, 'rn': 31, 'rd': 2})
    check('XPAC_1', {'xpac_d': 1, 'rd': 2})
    check('PACGA', {'rn': 2, 'rm': 3, 'rd': 4}, ('0xFFFFFFFF00000000ull',))
    check('LDRA', {'auth_s': 1, 'auth_w': 1, 'imm9': 0, 'rn': 3, 'rt': 4},
          ('0xFFFFFFFFFFFFF000ull',), ('f_imm9', '^512)-512)*8'))
    check('STG_FAMILY', {'stg_opc': 1, 'stg_mode': 0, 'rn': 3, 'rt': 4})
    check('STG_FAMILY', {'stg_opc': 3, 'stg_mode': 3, 'imm9': 1, 'rn': 3, 'rt': 4},
          ('WR64', 'c->x[3]='))
    check('STGP', {'stgp_mode': 3, 'imm7': 1, 'rn': 3, 'rt': 4, 'rt2': 5},
          ('WR64(a_', 'c->x[3]='))
    check('IRG', {'rn': 3, 'rd': 4})
    check('GMI', {'rm': 3, 'rd': 4}, ('|1ull',))
    check('ADDG', {'tag_imm6': 2, 'rn': 3, 'rd': 4})
    check('SUBG', {'tag_imm6': 2, 'rn': 3, 'rd': 4})
    invalid_pac, _ = encode('PACAUT_REG', {'pac_z': 1, 'rn': 3, 'rd': 2})
    assert a64.emit_static(invalid_pac, 0) is None
    invalid_casp, _ = encode('CASP', {'casp_sz': 0, 'rs': 3, 'rn': 3, 'rt': 4})
    assert a64.emit_static(invalid_casp, 0) is None
    covered = {entry[0] for entry in a64.TABLE}
    assert {'RETAA', 'RETAB', 'BRAA', 'BRAB', 'BRAAZ', 'BRABZ', 'BLRAA', 'BLRAB',
            'BLRAAZ', 'BLRABZ'} <= covered
    a64.gen_interp_cases()
    print('Decoded Eden ARMv8.1-8.5 additions in static and interpreter code paths.')


if __name__ == '__main__':
    main()
