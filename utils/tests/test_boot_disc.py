"""Reject CDI executables that a logo-only bootstrap check cannot catch."""
from pathlib import Path
import struct
import unittest

from utils.boot_disc import SESSION_LBA, read_boot_file, verify_boot_payload
from utils.tests.test_bootloader import scramble
from utils.build_boot_disc_branding import validate_badge

ROOT = Path(__file__).resolve().parents[2]


class BootDiscTests(unittest.TestCase):
    def setUp(self):
        self.raw = bytes((i * 13 + i // 97) & 255 for i in range(2052))
        self.name = '1DS_BOOT.BIN'
        self.data = bytearray(8 + 24 * 2336)
        self.data[8:8 + 31] = b'SEGA SEGAKATANA SEGA ENTERPRISES'
        self.data[8 + 96:8 + 112] = self.name.encode().ljust(16, b' ')
        pvd = 8 + 16 * 2336
        self.data[pvd:pvd + 7] = b'\x01CD001\x01'
        struct.pack_into('<H', self.data, pvd + 128, 2048)
        struct.pack_into('<I', self.data, pvd + 158, SESSION_LBA + 18)
        struct.pack_into('<I', self.data, pvd + 166, 2048)
        self.record = 8 + 18 * 2336
        file_name = (self.name + ';1').encode()
        self.data[self.record] = 48
        struct.pack_into('<I', self.data, self.record + 2, SESSION_LBA + 20)
        struct.pack_into('<I', self.data, self.record + 10, len(self.raw))
        self.data[self.record + 32] = len(file_name)
        self.data[self.record + 33:self.record + 33 + len(file_name)] = file_name
        self.scrambled = scramble(self.raw)
        for i in range(2):
            chunk = self.scrambled[i * 2048:(i + 1) * 2048]
            begin = 8 + (20 + i) * 2336
            self.data[begin:begin + len(chunk)] = chunk

    def test_file_and_descrambling_match_compiled_payload(self):
        self.assertEqual(read_boot_file(self.data, self.name), self.scrambled)
        verify_boot_payload(self.data, self.name, self.raw)

    def test_missing_boot_file_is_rejected(self):
        self.data[self.record + 33] = ord('X')
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            read_boot_file(self.data, self.name)

    def test_boot_header_mismatch_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'filename'):
            read_boot_file(self.data, '1DS_CORE.BIN')

    def test_out_of_bounds_file_extent_is_rejected(self):
        struct.pack_into('<I', self.data, self.record + 2, SESSION_LBA + 99)
        with self.assertRaisesRegex(ValueError, 'outside'):
            read_boot_file(self.data, self.name)

    def test_damaged_or_wrong_executable_is_rejected(self):
        self.data[8 + 20 * 2336] ^= 1
        with self.assertRaisesRegex(ValueError, 'compiled executable'):
            verify_boot_payload(self.data, self.name, self.raw)

    def test_truncated_payload_sector_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'outside'):
            read_boot_file(self.data[:8 + 21 * 2336 + 3], self.name)

    def test_badge_stream_must_decode_exactly_its_dimensions(self):
        data = bytearray((ROOT/'resources/boot-disc-badge.mr').read_bytes())
        validate_badge(data)
        struct.pack_into('<I', data, 14, 1)
        with self.assertRaisesRegex(ValueError, 'bounds'):
            validate_badge(data)

    def test_truncated_badge_run_is_rejected(self):
        data = bytearray((ROOT/'resources/boot-disc-badge.mr').read_bytes())
        offset = struct.unpack_from('<I', data, 10)[0]
        data = data[:offset] + b'\x81'
        struct.pack_into('<I', data, 2, len(data))
        with self.assertRaisesRegex(ValueError, 'Truncated'):
            validate_badge(data)
