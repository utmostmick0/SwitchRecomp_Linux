from pathlib import Path
import sys
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import codegen


SYMBOLS = (
    '_ZN2nn3gsv20BootWithRemoteMemoryEv',
    '_ZN2nn5nvsrv20BootWithRemoteMemoryENS_3gsv12MemoryRegionEPDoFNS_2os17RemoteMemoryUsageEvE',
)
SYNCPOINT_SYMBOL = '_ZN2nn6syncpt3sdk10InitializeEv'
NATIVE_MEMORY_SYMBOL = '_ZN2nn2gs3sdk26BootNativeMemoryManagementENS_3gsv19MemoryBlockModeBitsE'
FRAME_MANAGEMENT_SYMBOL = '_ZN2nn2gs3sdk27BootGraphicsFrameManagementEv'
DONATED_TRANSFER_MEMORY_QUERY_SYMBOL = '_ZN2nn3gpu3sdk34TryQueryDonatedTransferMemoryUsageEPmS2_S2_'


def test_remote_memory_boot_is_skipped_only_when_requested():
    for symbol in SYMBOLS:
        mod = SimpleNamespace(name='sdk', base=0x1000, syms={0x20: symbol})
        func = SimpleNamespace(name='F_1020', start=0x1020, mod=mod)

        stub = codegen.emit_function(None, func, {}, skip_switch2_remote_memory=True)
        assert stub == 'void F_1020(CPU* RESTRICT c, u64 e){ (void)e; c->pc=c->x[30]; }'
        assert codegen.switch2_graphics_boot_stub(func, False, False, False, False) is None


def test_syncpoint_init_is_skipped_only_when_requested():
    mod = SimpleNamespace(name='sdk', base=0x1000, syms={0x20: SYNCPOINT_SYMBOL})
    func = SimpleNamespace(name='F_1020', start=0x1020, mod=mod)
    stub = codegen.emit_function(None, func, {}, skip_switch2_syncpoint_init=True)
    assert stub == 'void F_1020(CPU* RESTRICT c, u64 e){ (void)e; c->pc=c->x[30]; }'
    assert codegen.switch2_graphics_boot_stub(func, True, False, False, False) is None


def test_native_memory_init_returns_success_only_when_requested():
    mod = SimpleNamespace(name='sdk', base=0x1000, syms={0x20: NATIVE_MEMORY_SYMBOL})
    func = SimpleNamespace(name='F_1020', start=0x1020, mod=mod)
    stub = codegen.emit_function(None, func, {}, skip_switch2_native_memory_init=True)
    assert stub == 'void F_1020(CPU* RESTRICT c, u64 e){ (void)e; c->x[0]=0; c->pc=c->x[30]; }'
    assert codegen.switch2_graphics_boot_stub(func, True, True, False, False) is None


def test_frame_management_is_skipped_only_when_requested():
    mod = SimpleNamespace(name='sdk', base=0x1000, syms={0x20: FRAME_MANAGEMENT_SYMBOL})
    func = SimpleNamespace(name='F_1020', start=0x1020, mod=mod)
    stub = codegen.emit_function(None, func, {}, skip_switch2_frame_management_init=True)
    assert stub == 'void F_1020(CPU* RESTRICT c, u64 e){ (void)e; c->pc=c->x[30]; }'
    assert codegen.switch2_graphics_boot_stub(func, True, True, True, False) is None


def test_donated_transfer_memory_query_returns_unavailable_only_when_requested():
    mod = SimpleNamespace(name='sdk', base=0x1000, syms={0x20: DONATED_TRANSFER_MEMORY_QUERY_SYMBOL})
    func = SimpleNamespace(name='F_1020', start=0x1020, mod=mod)
    stub = codegen.emit_function(None, func, {}, skip_switch2_donated_transfer_memory_query=True)
    assert stub == ('void F_1020(CPU* RESTRICT c, u64 e){ u8* const M=c->mem; (void)e; '
                    'if(c->x[0] && c->x[0]<0x10000000000ull) WR64(c->x[0],0x10000000ull); '
                    'if(c->x[1] && c->x[1]<0x10000000000ull) WR64(c->x[1],0x10000000ull); '
                    'if(c->x[2] && c->x[2]<0x10000000000ull) WR64(c->x[2],0x10000000ull); '
                    'c->x[0]=1; c->pc=c->x[30]; }')
    assert codegen.switch2_graphics_boot_stub(func, True, True, True, True, False) is None


def test_other_module_and_symbol_are_not_skipped():
    for mod_name, symbol in (
            ('main', SYMBOLS[0]),
            ('sdk', '_ZN2nn3gsv18OtherFunctionEv')):
        mod = SimpleNamespace(name=mod_name, base=0x1000, syms={0x20: symbol})
        func = SimpleNamespace(name='F_1020', start=0x1020, mod=mod)
        assert codegen.switch2_graphics_boot_stub(func, True, False, False, False) is None

    mod = SimpleNamespace(name='sdk', base=0x1000, syms={0x20: SYMBOLS[1] + 'X'})
    func = SimpleNamespace(name='F_1020', start=0x1020, mod=mod)
    assert codegen.switch2_graphics_boot_stub(func, True, False, False, False) is None


if __name__ == '__main__':
    test_remote_memory_boot_is_skipped_only_when_requested()
    test_syncpoint_init_is_skipped_only_when_requested()
    test_native_memory_init_returns_success_only_when_requested()
    test_frame_management_is_skipped_only_when_requested()
    test_donated_transfer_memory_query_returns_unavailable_only_when_requested()
    test_other_module_and_symbol_are_not_skipped()
    print('Remote-memory codegen checks passed.')
