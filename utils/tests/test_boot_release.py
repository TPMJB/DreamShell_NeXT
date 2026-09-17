"""Boot payload safety and the real startup sound player's EOF/failure paths."""
from pathlib import Path
import gzip
import hashlib
import struct
import subprocess
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[2]


class BootTests(unittest.TestCase):
    def test_boot_disc_badge_preserves_bootstrap_code(self):
        badge = (ROOT/'resources/boot-disc-badge.mr').read_bytes()
        from utils.build_boot_disc_branding import inject_badge
        ip = inject_badge((ROOT/'resources/IP.BIN').read_bytes(), badge)
        start, end = 0x3820, 0x3820 + 8192
        self.assertEqual(len(ip), 32768)
        self.assertEqual(badge[:2], b'MR')
        size, _, offset, width, height, _, colors = struct.unpack_from('<7I', badge, 2)
        self.assertEqual(size, len(badge))
        self.assertLessEqual(size, 8192)
        self.assertEqual((width, height), (320, 90))
        self.assertTrue(0 < colors <= 128)
        self.assertEqual(offset, 30 + colors * 4)
        self.assertEqual(ip[start:start+size], badge)
        # Baseline bootstrap from NeXT 1.0: branding cannot alter executable
        # bytes, hardware flags, entry points, or the original disc header.
        self.assertEqual(hashlib.sha256(ip[:start]+ip[end:]).hexdigest(),
                         '44762cb534ef9a5c35a8ad35c29c595841268c86f63f83a281b166e0b7a64274')

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
