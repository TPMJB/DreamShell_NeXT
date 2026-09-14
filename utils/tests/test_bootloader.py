"""Fault injection against the production core loader and recovery menu."""
import gzip
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def scramble(data):
    """Produce the independent on-disc permutation consumed by the decoder."""
    seed, offset = len(data) & 0xffff, 0
    output = bytearray()
    chunk = 2 * 1024 * 1024
    while chunk >= 32:
        while len(data) - offset >= chunk:
            indices = list(range(chunk // 32))
            for i in range(len(indices) - 1, -1, -1):
                seed = (seed * 2109 + 9273) & 0x7fff
                j = (((seed + 0xc000) & 0xffff) * i) >> 16
                indices[i], indices[j] = indices[j], indices[i]
                begin = offset + indices[i] * 32
                output.extend(data[begin:begin + 32])
            offset += chunk
        chunk //= 2
    output.extend(data[offset:])
    return output


class BootloaderTests(unittest.TestCase):
    def test_loader_recovery_and_config(self):
        if not shutil.which('gcc'):
            self.skipTest('gcc is required for the bootloader harness')
        with tempfile.TemporaryDirectory(prefix='bootloader-test-') as temp:
            directory = Path(temp)
            raw = bytes((i * 7) & 255 for i in range(65540))
            compressed = gzip.compress(raw, mtime=0)
            bad_crc = bytearray(compressed)
            bad_crc[-8] ^= 1
            fixtures = {
                'raw.bin': raw,
                'good.gz': compressed,
                'bad-crc.gz': bad_crc,
                'short.gz': compressed[:-5],
                'overflow.gz': compressed[:-4] + struct.pack('<I', 65536),
                'not-gzip.bin': raw[:-4] + struct.pack('<I', len(raw)),
                'concat.gz': gzip.compress(b'abcd', mtime=0) + compressed,
                'scrambled.bin': scramble(bytes((i * 13 + i // 97) & 255
                                               for i in range(2097192))),
                'boot.cfg': b'boot_order=sd,ide\nboot_delay=3\n',
                'bad.cfg': b'autoboot=0\nunknown=1\n',
                'fallback.cfg': b'core_path=/sd/missing.bin\nfallback_path=/ide/DS/DS_CORE.BIN\n',
                'missing.cfg': b'core_path=/sd/missing.bin\n',
            }
            for name, data in fixtures.items():
                (directory / name).write_bytes(data)
            binary = directory / 'boot-tests'
            subprocess.run([
                'gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                '-Wno-sign-compare', '-Wno-format-truncation',
                '-Wframe-larger-than=8192', '-fsanitize=address,undefined',
                '-fno-pie', '-no-pie', '-Iutils/tests/boot_shim',
                '-Ifirmware/bootloader/include', 'utils/tests/boot_harness.c',
                'firmware/bootloader/src/loader.c',
                'firmware/bootloader/src/boot_config.c',
                'firmware/bootloader/src/descramble.c',
                '-lz', '-lm', '-o', str(binary),
            ], cwd=ROOT, check=True)
            env = dict(os.environ)
            env.setdefault('ASAN_OPTIONS', 'detect_leaks=1')
            env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1')
            result = subprocess.run([str(binary), temp],
                                    capture_output=True, text=True, env=env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('input tests passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
