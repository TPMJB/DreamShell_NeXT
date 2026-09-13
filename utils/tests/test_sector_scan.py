"""Exercise the standalone PC scanner with independent raw-sector fixtures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zlib

from test_console_ripper import make_sector

ROOT = Path(__file__).resolve().parents[2]


class SectorScanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.build.name) / 'scan_gd_sectors'
        compiler = shutil.which('cc') or shutil.which('gcc')
        if not compiler:
            raise RuntimeError('A C compiler is required for sector scanner tests')
        subprocess.run([compiler, '-O3', '-std=c99', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'utils/scan_gd_sectors.c'), '-o', str(cls.exe)],
                       check=True)
        cls.good = b''.join(make_sector(45150 + i, i) for i in range(65))

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'track03.bin'
        self.path.write_bytes(self.good)

    def scan(self, expected=None, fad=45150):
        args = [str(self.exe), str(self.path), str(fad)]
        if expected is not None:
            args.append(f'{expected:08x}')
        return subprocess.run(args, text=True, capture_output=True)

    def test_full_crc_and_sector_checks_across_read_boundary_do_not_edit_input(self):
        result = self.scan(zlib.crc32(self.good))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('sectors 65\n', result.stdout)
        self.assertIn('suspect_sectors 0\n', result.stdout)
        self.assertIn('result SECTOR_CHECKS_AND_CATALOG_CRC_MATCH', result.stdout)
        self.assertEqual(self.path.read_bytes(), self.good)

    def test_damage_ranges_distinguish_content_address_parity_and_unsupported(self):
        data = bytearray(self.good)
        data[2 * 2352 + 100] ^= 1
        data[3 * 2352 + 100] ^= 1
        data[9 * 2352 + 2248] ^= 1
        data[11 * 2352:12 * 2352] = make_sector(45150 + 12, 11)
        data[12 * 2352] ^= 1
        data[13 * 2352 + 15] = 2
        self.path.write_bytes(data)
        result = self.scan(zlib.crc32(data))
        self.assertEqual(result.returncode, 1, result.stderr)
        for line in ('range sector=2..3 fad=45152..45153 flags=12',
                     'range sector=9..9 fad=45159..45159 flags=8',
                     'range sector=11..11 fad=45161..45161 flags=2',
                     'range sector=12..12 fad=45162..45162 flags=1',
                     'range sector=13..13 fad=45163..45163 flags=16',
                     'suspect_sectors 5', 'unsupported_sectors 1',
                     'catalog_crc MATCH', 'result SECTOR_CHECKS_FAILED_OR_UNSUPPORTED'):
            self.assertIn(line + '\n', result.stdout)
        self.assertEqual(self.path.read_bytes(), data)

    def test_valid_sectors_with_different_contents_cannot_pass_catalog(self):
        data = make_sector(45150, 70) + self.good[2352:]
        self.path.write_bytes(data)
        result = self.scan(zlib.crc32(self.good))
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('suspect_sectors 0\n', result.stdout)
        self.assertIn(f'crc32 {zlib.crc32(data):08x}\n', result.stdout)
        self.assertIn('result SECTOR_CHECKS_PASS_BUT_CATALOG_CRC_DIFFERS', result.stdout)

    def test_missing_catalog_is_reported_and_high_density_addresses_are_supported(self):
        self.path.write_bytes(make_sector(549299))
        result = self.scan(fad=549299)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('result SECTOR_CHECKS_PASS_CATALOG_NOT_CHECKED', result.stdout)

    def test_partial_empty_and_missing_files_are_incomplete(self):
        for data in (b'', self.good[:-1], self.good + b'x'):
            self.path.write_bytes(data)
            result = self.scan()
            self.assertEqual(result.returncode, 2)
            self.assertNotIn('result SECTOR_CHECKS_PASS', result.stdout)
        self.path.unlink()
        self.assertEqual(self.scan().returncode, 2)

    def test_invalid_addresses_and_crc_arguments_are_rejected(self):
        for fad in ('-1', '1x', '4294967296', '720000'):
            self.assertEqual(self.scan(fad=fad).returncode, 2)
        for crc in ('1', '3303fcd5x', 'zzzzzzzz', '-0000001'):
            result = subprocess.run([str(self.exe), str(self.path), '45150', crc],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()
