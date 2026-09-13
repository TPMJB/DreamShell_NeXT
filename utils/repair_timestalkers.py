#!/usr/bin/env python3
"""Recover the observed GD Ripper 2.0.2 Time Stalkers overwrite into a new file.

This is deliberately restricted to the diagnosed input CRC/size and signatures.
Only bytes 368..375 are recovered from the sector's existing P parity. Existing
EDC, P and Q must then all validate; none of them is regenerated or replaced.
The source is read-only. A new copy is accepted only after its catalog CRC is
verified from storage. This is not a general scratched-disc correction tool.
"""
import argparse
import os
from pathlib import Path
import sys
import zlib

SIZE = 1185760800
SOURCE_CRC = 0x3303FCD5
EXPECTED_CRC = 0xF92C1222
REPAIRS = 623
FIRST_FAD = 45150
SECTOR_SIZE = 2352
SIGNATURES = {bytes.fromhex('4013258c03804b0c'), bytes.fromhex('4013258c03c0700c')}
SYNC = b'\0' + b'\xff' * 10 + b'\0'


def _tables():
    edc, forward, backward = [], [], [0] * 256
    for i in range(256):
        v = i
        for _ in range(8):
            v = (v >> 1) ^ (0xd8018001 if v & 1 else 0)
        edc.append(v)
        j = (i << 1) ^ (0x11d if i & 128 else 0)
        forward.append(j)
        backward[i ^ j] = i
    return edc, forward, backward


EDC_TABLE, FORWARD, BACKWARD = _tables()


def _parity_ok(s, majors, minors, mult, inc, dest):
    for major in range(majors):
        index = (major >> 1) * mult + (major & 1)
        a = b = 0
        for _ in range(minors):
            v = s[12 + index]
            index = (index + inc) % (majors * minors)
            a = FORWARD[a ^ v]
            b ^= v
        a = BACKWARD[FORWARD[a] ^ b]
        if s[dest + major] != a or s[dest + majors + major] != a ^ b:
            return False
    return True


def sector_valid(s, fad):
    if len(s) != SECTOR_SIZE or s[:12] != SYNC:
        return False
    bcd = lambda n: (n // 10) * 16 + n % 10
    if s[12:16] != bytes((bcd(fad // 4500), bcd(fad // 75 % 60), bcd(fad % 75), 1)):
        return False
    edc = 0
    for byte in s[:2064]:
        edc = (edc >> 8) ^ EDC_TABLE[(edc ^ byte) & 255]
    return (edc == int.from_bytes(s[2064:2068], 'little') and
            _parity_ok(s, 86, 24, 2, 86, 2076) and
            _parity_ok(s, 52, 43, 86, 88, 2248))


def recover_sector(s, fad):
    if len(s) != SECTOR_SIZE:
        raise ValueError('Incomplete sector')
    if s[368:376] not in SIGNATURES:
        return s
    if sector_valid(s, fad):
        return s  # A legitimate matching byte sequence must not be changed.
    fixed = bytearray(s)
    for pos in range(368, 376):
        # The eight bytes fall in eight distinct P codewords, with one known
        # bad symbol in each. XOR of the two P symbols equals XOR of the data
        # symbols. Solve that missing symbol, then validate against EDC and Q.
        major = (pos - 12) % 86
        delta = s[2076 + major] ^ s[2076 + 86 + major]
        for minor in range(24):
            delta ^= s[12 + major + 86 * minor]
        fixed[pos] ^= delta
    fixed = bytes(fixed)
    if not sector_valid(fixed, fad):
        raise ValueError(f'FAD {fad}: damage is not the diagnosed eight-byte overwrite')
    return fixed


def repair_file(source, output, *, size=SIZE, source_crc=SOURCE_CRC,
                expected_crc=EXPECTED_CRC, repairs=REPAIRS, first_fad=FIRST_FAD):
    source, output = Path(source), Path(output)
    source_hash = output_hash = count = processed = 0
    created = False
    try:
        with source.open('rb') as src:
            if os.fstat(src.fileno()).st_size != size:
                raise ValueError(f'Input must be exactly {size} bytes; original is unchanged')
            # Exclusive creation also rejects the input itself, symlinks and
            # hard links to existing files. Never truncate an existing output.
            with output.open('xb') as dst:
                created = True
                while True:
                    block = src.read(SECTOR_SIZE * 256)
                    if not block:
                        break
                    if len(block) % SECTOR_SIZE:
                        raise ValueError('Input ended in a partial sector')
                    source_hash = zlib.crc32(block, source_hash)
                    changed = bytearray(block)
                    for offset in range(0, len(block), SECTOR_SIZE):
                        sector = block[offset:offset + SECTOR_SIZE]
                        if sector[368:376] not in SIGNATURES:
                            continue
                        index = (processed + offset) // SECTOR_SIZE
                        fixed = recover_sector(sector, first_fad + index)
                        if fixed != sector:
                            if index % 16 != 12:
                                raise ValueError('Signature outside the diagnosed read-block position')
                            changed[offset:offset + SECTOR_SIZE] = fixed
                            count += 1
                    output_hash = zlib.crc32(changed, output_hash)
                    if dst.write(changed) != len(changed):
                        raise OSError('Short write to repaired copy')
                    processed += len(block)
                    if processed % (SECTOR_SIZE * 32768) == 0:
                        print(f'Processed {processed * 100 // size}% — {count} sectors recovered',
                              file=sys.stderr)
                if processed != size or source_hash != source_crc:
                    raise ValueError(f'Input CRC {source_hash:08x} differs from the diagnosed '
                                     f'{source_crc:08x}; original is unchanged')
                if count != repairs or output_hash != expected_crc:
                    raise ValueError(f'Recovery did not reproduce the known dump: '
                                     f'{count} repairs, CRC {output_hash:08x}')
                dst.flush()
                os.fsync(dst.fileno())
        # Check the saved copy too; the desktop is fast enough for this pass.
        saved_crc = saved_bytes = 0
        with output.open('rb') as saved:
            for block in iter(lambda: saved.read(4 * 1024 * 1024), b''):
                saved_crc = zlib.crc32(block, saved_crc)
                saved_bytes += len(block)
        if saved_bytes != size or saved_crc != expected_crc:
            raise ValueError('Repaired copy failed storage read-back')
        return count, saved_crc
    except BaseException:
        if created:
            try:
                output.unlink()
            except OSError:
                print(f'Remove the unverified partial copy: {output}', file=sys.stderr)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='Original TIME_STALKERS/track03.bin (read-only)')
    parser.add_argument('output', type=Path, help='New file on the PC; must not already exist')
    args = parser.parse_args()
    try:
        count, crc = repair_file(args.source, args.output)
    except (OSError, ValueError) as error:
        print(f'Recovery stopped: {error}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('Recovery cancelled; original is unchanged.', file=sys.stderr)
        return 130
    print(f'VERIFIED: recovered {count} sectors; saved Track 3 CRC32 {crc:08x}')
    print(f'New copy: {args.output}')
    print('Original dump is unchanged. Keep its CRC journals with the original track.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
