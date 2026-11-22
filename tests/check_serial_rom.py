#!/usr/bin/env python3
"""Validate the copied guest ROM in the final linked Android ELF, not just .o files.

A preemptible global branch can resolve through the host PLT in a shared
library. Those bytes become an invalid guest branch after copying the ROM.
No third-party Python dependencies are needed.
"""
import struct
import sys
from pathlib import Path


def check(path):
    data = Path(path).read_bytes()
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    assert header[0][:6] == b'\x7fELF\x02\x01' and header[2] == 183, 'expected AArch64 ELF64 little-endian'
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, header[6] + i * header[11])
                for i in range(header[12])]
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for off in range(section[4], section[4] + section[5], section[9]):
            name, _, _, index, address, _ = struct.unpack_from('<IBBHQQ', data, off)
            name = names[name:names.find(b'\0', name)].decode('utf-8', errors='replace')
            if name in ('hv_serial_rom', 'hv_serial_rom_stop', 'hv_serial_rom_end'):
                symbols[name] = (index, address)
    assert len(symbols) == 3, 'unstripped ELF with all three ROM symbols required'
    index, start = symbols['hv_serial_rom']
    stop_index, stop = symbols['hv_serial_rom_stop']
    end_index, end = symbols['hv_serial_rom_end']
    assert index == stop_index == end_index and start < stop < end
    section = sections[index]
    offset = section[4] + start - section[3]
    rom = data[offset:offset + end - start]
    assert len(rom) == end - start
    branches = 0
    stop_reached = False
    for at in range(0, stop - start + 4, 4):
        word, = struct.unpack_from('<I', rom, at)
        bits = shift = None
        is_branch = True
        if word & 0x7c000000 == 0x14000000:  # B/BL
            bits, shift = 26, 0
        elif word & 0xff000010 == 0x54000000 or word & 0x7e000000 == 0x34000000:
            bits, shift = 19, 5  # B.cond / CBZ / CBNZ
        elif word & 0x7e000000 == 0x36000000:
            bits, shift = 14, 5  # TBZ / TBNZ
        elif word & 0x9f000000 == 0x10000000:  # ADR
            immediate = ((word >> 5) & 0x7ffff) << 2 | ((word >> 29) & 3)
            displacement = (immediate ^ (1 << 20)) - (1 << 20)
            target = start + at + displacement
            assert start <= target < end, f'ADR escapes ROM at +0x{at:x}'
            continue
        elif word & 0x9f000000 == 0x90000000:
            raise AssertionError(f'host-page-relative ADRP in copied ROM at +0x{at:x}')
        elif word & 0x3b000000 == 0x18000000:  # literal load
            bits, shift, is_branch = 19, 5, False
        if bits is None:
            continue
        immediate = (word >> shift) & ((1 << bits) - 1)
        displacement = ((immediate ^ (1 << (bits - 1))) - (1 << (bits - 1))) * 4
        target = start + at + displacement
        limit = stop + 4 if is_branch else end
        assert start <= target < limit, f'PC-relative target escapes guest ROM at +0x{at:x}: 0x{target:x}'
        if is_branch:
            branches += 1
            if at < stop - start and target == stop:
                stop_reached = True
    assert stop_reached, 'no branch reaches the stop marker'
    assert struct.unpack_from('<I', rom, stop - start)[0] == 0x14000000, 'stop must branch to itself'
    print(f'PASS: linked guest ROM, {branches} internal branches, stop +0x{stop-start:x}')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: check_serial_rom.py UNSTRIPPED_AARCH64_ELF')
    try:
        check(sys.argv[1])
    except (AssertionError, IndexError, struct.error) as error:
        raise SystemExit(f'FAIL: {error}')
