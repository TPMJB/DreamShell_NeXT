"""Test the restricted recovery against independently encoded sectors."""
from pathlib import Path
import sys
import tempfile
import unittest
import zlib

from test_console_ripper import make_sector

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import repair_timestalkers as repair


class BurstRecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.good = b''.join(make_sector(45150+i, i) for i in range(32))
        data = bytearray(cls.good)
        signatures = sorted(repair.SIGNATURES)
        for i, index in enumerate((12, 28)):
            data[index*2352+368:index*2352+376] = signatures[i]
        cls.bad = bytes(data)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)/'original.bin'
        self.output = Path(self.temp.name)/'repaired.bin'
        self.source.write_bytes(self.bad)
        self.options = dict(size=len(self.bad), source_crc=zlib.crc32(self.bad),
                            expected_crc=zlib.crc32(self.good), repairs=2)

    def test_parity_recovers_original_nonzero_bytes_without_changing_checksums(self):
        for index in (12, 28):
            sector = self.bad[index*2352:(index+1)*2352]
            fixed = repair.recover_sector(sector, 45150+index)
            self.assertEqual(fixed, self.good[index*2352:(index+1)*2352])
            self.assertEqual(fixed[:368], sector[:368])
            self.assertEqual(fixed[376:], sector[376:])

    def test_verified_copy_preserves_original(self):
        self.assertEqual(repair.repair_file(self.source, self.output, **self.options),
                         (2, zlib.crc32(self.good)))
        self.assertEqual(self.output.read_bytes(), self.good)
        self.assertEqual(self.source.read_bytes(), self.bad)

    def test_source_alias_and_existing_output_cannot_be_overwritten(self):
        with self.assertRaises(FileExistsError):
            repair.repair_file(self.source, self.source, **self.options)
        self.output.write_bytes(b'keep this')
        with self.assertRaises(FileExistsError):
            repair.repair_file(self.source, self.output, **self.options)
        self.assertEqual(self.output.read_bytes(), b'keep this')
        self.assertEqual(self.source.read_bytes(), self.bad)

    def test_unexpected_input_crc_output_crc_size_or_count_rejects_new_copy(self):
        for key, value in (('source_crc', 1), ('expected_crc', 1),
                           ('size', len(self.bad)+2352), ('repairs', 3)):
            options = dict(self.options, **{key: value})
            with self.assertRaises(ValueError):
                repair.repair_file(self.source, self.output, **options)
            self.assertFalse(self.output.exists())
            self.assertEqual(self.source.read_bytes(), self.bad)

    def test_extra_payload_or_parity_damage_is_not_guessed_away(self):
        sector = self.bad[12*2352:13*2352]
        for pos in (100, 2064, 2076+12, 2248, 12):
            damaged = bytearray(sector)
            damaged[pos] ^= 1
            with self.assertRaises(ValueError):
                repair.recover_sector(bytes(damaged), 45162)

    def test_unknown_signature_is_unchanged_and_bad_block_position_is_rejected(self):
        sector = bytearray(self.bad[12*2352:13*2352])
        sector[368] ^= 1
        self.assertEqual(repair.recover_sector(bytes(sector), 45162), sector)
        data = bytearray(self.good)
        data[368:376] = next(iter(repair.SIGNATURES))
        self.source.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'read-block position'):
            repair.repair_file(self.source, self.output, **self.options)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
