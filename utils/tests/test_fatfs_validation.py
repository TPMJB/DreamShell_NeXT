"""Behavioral regression cases for official FatFs R0.16 patches 1 and 2."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FatValidationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fat = ROOT / 'lib/fatfs/fatfs/src'
        cls.build = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.build.cleanup)
        cls.exe = Path(cls.build.name) / 'fat-validation-test'
        subprocess.run([
            'gcc', '-std=gnu11', '-O1', '-Wall', '-Wextra',
            '-include', 'utils/tests/fatfs_host_types.h', '-I' + str(fat),
            'utils/tests/fatfs_validation_harness.c', str(fat / 'ff.c'),
            str(fat / 'ffunicode.c'), str(fat / 'ffsystem.c'),
            '-o', str(cls.exe),
        ], cwd=ROOT, check=True)

    def run_case(self, case):
        subprocess.run([str(self.exe), case], check=True, timeout=10)

    def test_small_fat12_volume_round_trip(self):
        self.run_case('small-volume')

    def test_reject_too_few_fat_clusters(self):
        self.run_case('fat-clusters')

    def test_reject_fat_size_overflow(self):
        self.run_case('fat-size')

    def test_reject_too_few_exfat_clusters(self):
        self.run_case('exfat-clusters')

    def test_bound_corrupt_exfat_label_length(self):
        self.run_case('exfat-label')
