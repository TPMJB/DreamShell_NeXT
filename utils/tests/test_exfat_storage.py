"""Linux-formatted images through DreamShell's production VFS and ISO reader."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ExfatStorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mkexfat = os.environ.get('DREAMSHELL_MKFS_EXFAT') or shutil.which('mkfs.exfat')
        cls.fsck = os.environ.get('DREAMSHELL_FSCK_EXFAT') or shutil.which('fsck.exfat')
        cls.mkfat = os.environ.get('DREAMSHELL_MKFS_FAT') or shutil.which('mkfs.fat')
        if not cls.mkexfat or not cls.fsck:
            if os.environ.get('REQUIRE_EXFAT_TESTS'):
                raise RuntimeError('Install exfatprogs to run required filesystem tests')
            raise unittest.SkipTest('Install exfatprogs for Linux interoperability tests')
        cls.build = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.build.cleanup)
        cls.vfs = Path(cls.build.name) / 'vfs-test'
        cls.loader = Path(cls.build.name) / 'loader-test'
        cls.writer = Path(cls.build.name) / 'loader-writer-test'
        common = ['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror']
        fat = ROOT / 'lib/fatfs/fatfs/src'
        subprocess.run(common + [
            '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
            '-Wno-format-truncation', '-DFATFS_USE_DMA_BUF=1',
            '-Iutils/tests/fatfs_shim', '-Ilib/fatfs/include', '-I' + str(fat),
            'utils/tests/fatfs_vfs_harness.c', str(fat/'ff.c'),
            str(fat/'ffunicode.c'), str(fat/'ffsystem.c'), '-pthread', '-o', str(cls.vfs),
        ], cwd=ROOT, check=True)
        loader = 'firmware/isoldr/loader/fs/fat/'
        for readonly, target in [(1, cls.loader), (0, cls.writer)]:
            subprocess.run(common + [
                '-DDEV_TYPE_SD=1', '-D_FS_ASYNC=1', f'-D_FS_READONLY={readonly}',
                '-I' + loader + 'include', 'utils/tests/isoldr_fatfs_harness.c',
                'firmware/isoldr/loader/kos/src/strchr.c',
                loader + 'src/ff.c', loader + 'src/ffunicode.c', '-o', str(target),
            ], cwd=ROOT, check=True)

    def exercise(self, kind=64, ide=0, cluster=131072, mbr=False, large=False):
        with tempfile.TemporaryDirectory() as tmp:
            volume = Path(tmp) / 'volume.img'
            size = 6*1024**3 if large else 512*1024**2 if cluster > 131072 else 128*1024**2
            if kind == 64:
                # R0.16 patch 2 requires at least 256 data clusters. Keep the
                # maximum-cluster DMA test valid, allowing room for metadata.
                # truncate creates a sparse image rather than allocating GiBs.
                size = max(size, 256 * cluster + 32 * 1024**2)
            with volume.open('wb') as f:
                f.truncate(size)
            if kind == 64:
                subprocess.run([self.mkexfat, '-c', str(cluster), str(volume)], check=True,
                               stdout=subprocess.DEVNULL)
            else:
                if not self.mkfat:
                    if os.environ.get('REQUIRE_EXFAT_TESTS'):
                        self.fail('Install dosfstools for required FAT regression tests')
                    self.skipTest('Install dosfstools for FAT VFS regression tests')
                subprocess.run([self.mkfat, '-F', str(kind), str(volume)], check=True,
                               stdout=subprocess.DEVNULL)
            image = volume
            offset = 2048*512
            if mbr:
                image = Path(tmp)/'disk.img'
                with image.open('wb') as out:
                    header = bytearray(512)
                    header[446+4] = 0x07 if kind == 64 else 0x0c if kind == 32 else 0x0e
                    struct.pack_into('<II', header, 446+8, 2048, size//512)
                    header[510:] = b'\x55\xaa'
                    out.write(header)
                    out.seek(offset)
                    with volume.open('rb') as src:
                        shutil.copyfileobj(src, out)
            subprocess.run([str(self.vfs), str(image), str(kind), str(ide), str(int(large))],
                           check=True, timeout=120)
            # The VFS wrote fragmented track fixtures. Consume them with the
            # separately configured reader used while running a game.
            if cluster <= 131072:
                subprocess.run([str(self.loader), str(image)], check=True, timeout=60)
                subprocess.run([str(self.writer), str(image)], check=True, timeout=60)
            if kind == 64:
                if mbr:
                    with image.open('rb') as src, volume.open('wb') as out:
                        src.seek(offset)
                        shutil.copyfileobj(src, out)
                subprocess.run([self.fsck, '-n', str(volume)], check=True,
                               stdout=subprocess.DEVNULL, timeout=60)

    def test_serial_sd_exfat(self):
        self.exercise()

    def test_ntfs_and_gpt_are_rejected_without_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'unsupported.img'
            for kind in [0x07, 0xee]:
                with path.open('wb') as f:
                    f.truncate(4 * 1024**2)
                    mbr = bytearray(512)
                    mbr[450] = kind
                    struct.pack_into('<II', mbr, 454, 2048, 4096)
                    mbr[510:] = b'\x55\xaa'
                    f.write(mbr)
                    f.seek(2048 * 512)
                    vbr = bytearray(512)
                    vbr[:11] = b'\xeb\x52\x90NTFS    '
                    vbr[510:] = b'\x55\xaa'
                    f.write(vbr)
                subprocess.run([str(self.vfs), str(path), '0', '0', '0'], check=True)

    def test_ide_exfat(self):
        self.exercise(ide=1)

    def test_exfat_mbr_partition(self):
        self.exercise(mbr=True)

    def test_maximum_exfat_cluster_has_bounded_dma_buffer(self):
        self.exercise(ide=1, cluster=16*1024**2)

    def test_exfat_file_beyond_4gib(self):
        self.exercise(ide=1, large=True)

    def test_fat16_vfs_and_game_reader(self):
        self.exercise(kind=16)

    def test_fat32_vfs_and_game_reader(self):
        self.exercise(kind=32, mbr=True)
