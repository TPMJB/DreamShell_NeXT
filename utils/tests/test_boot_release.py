"""Boot payload safety and the real startup sound player's EOF/failure paths."""
from pathlib import Path
import gzip
import struct
import subprocess
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[2]


class BootTests(unittest.TestCase):
    def test_splash_fits_pvr_texture_and_640x480_viewport(self):
        data = gzip.decompress((ROOT/'romdisk/logo.kmg.gz').read_bytes())
        magic, version, platform, fmt, width, height, size = struct.unpack_from('<7I', data)
        self.assertEqual((magic, version, platform, fmt), (0x474d4b, 1, 1, 0x203))
        self.assertEqual((width, height), (1024, 512))
        self.assertEqual(size, width * height * 2)
        self.assertEqual(len(data), size + 64)
        self.assertLessEqual(size, 1024**2)

    def test_chime_format_and_silent_tail(self):
        with wave.open(str(ROOT/'resources/sfx/startup.wav')) as sound:
            self.assertEqual((sound.getnchannels(), sound.getsampwidth(), sound.getframerate()), (2, 2, 44100))
            samples = sound.readframes(sound.getnframes())
        raw = gzip.decompress((ROOT/'romdisk/startup.raw.gz').read_bytes())
        self.assertEqual(len(raw), 196608)
        self.assertEqual(len(raw) % 32, 0)
        self.assertEqual(len(samples), len(raw)*4)
        # At least a full AICA ring of silence avoids clipping the audible tail.
        self.assertEqual(samples[-32704*2*4:], bytes(32704*2*4))
        values = struct.unpack('<'+'h'*(len(samples)//2), samples)
        self.assertLess(max(abs(n) for n in values), 20000)
        self.assertGreater(max(abs(n) for n in values), 4000)

    def test_startup_stream_bounds_and_cleanup(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(Path(tmp)/'sfx')
            subprocess.run(['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                            '-Werror', '-Wno-sign-compare', '-fsanitize=address,undefined',
                            '-fno-pie', '-no-pie', '-Iutils/tests/sfx_shim', '-Iinclude',
                            'utils/tests/sfx_harness.c', '-o', exe], cwd=ROOT, check=True)
            self.assertIn('passed', subprocess.check_output([exe], text=True))
