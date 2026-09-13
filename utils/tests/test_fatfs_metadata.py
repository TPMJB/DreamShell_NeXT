"""Exercise metadata writers on the release's actual FAT implementation."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FatMetadataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fat = ROOT / 'lib/fatfs/fatfs/src'
        if not (fat / 'ff.c').is_file():
            raise RuntimeError('Initialize the pinned submodule: git submodule update --init lib/fatfs')
        cls.build = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.build.cleanup)
        cls.exe = Path(cls.build.name) / 'fat-metadata-test'
        subprocess.run([
            'gcc', '-std=gnu11', '-O1', '-Wno-format-truncation',
            '-ffunction-sections', '-fdata-sections',
            '-include', 'utils/tests/fatfs_host_types.h',
            '-Iutils/tests/console_shim', '-Iinclude/SDL', '-I' + str(fat),
            'utils/tests/fatfs_harness.c', 'applications/gd_ripper/modules/checksum.c',
            str(fat / 'ff.c'), str(fat / 'option/unicode.c'),
            '-Wl,--gc-sections', '-lz', '-o', str(cls.exe),
        ], cwd=ROOT, check=True)

    def test_log_and_crc_creation_on_fat16(self):
        subprocess.run([str(self.exe), '16'], check=True)

    def test_log_and_crc_creation_on_fat32(self):
        subprocess.run([str(self.exe), '32'], check=True)
