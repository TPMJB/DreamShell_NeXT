from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import wave
import hashlib
import re
from array import array

ROOT = Path(__file__).resolve().parents[2]


class MenuMusicTests(unittest.TestCase):
    def test_playback_settings_and_failure_lifecycle(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'music-test'
            subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                            '-Wno-format-truncation','-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer','-no-pie','-Iutils/tests/music_shim',
                            'utils/tests/menu_music_harness.c','-o',str(exe)],cwd=ROOT,check=True)
            subprocess.run([str(exe),tmp],cwd=ROOT,check=True)

    def test_input_and_close_during_blocked_io(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'music-threads'
            subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                            '-Wno-format-truncation','-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer','-no-pie','-pthread',
                            '-Iutils/tests/music_shim','utils/tests/menu_music_threads.c',
                            '-o',str(exe)],cwd=ROOT,check=True)
            subprocess.run([str(exe),tmp],cwd=ROOT,check=True,timeout=10)

    def test_bundled_playlist_matches_playback_budget(self):
        from utils.generate_menu_music import TRACKS
        paths = [ROOT/'applications/launch_app/music'/name for name in TRACKS]
        if not all(path.exists() for path in paths):
            subprocess.run([sys.executable,str(ROOT/'utils/generate_menu_music.py')],check=True)
        source = (ROOT/'applications/launch_app/modules/music.c').read_text()
        self.assertEqual(tuple(re.findall(r'"([a-z-]+\.wav)"',source)),TRACKS)
        hashes = set()
        for path in paths:
            with self.subTest(track=path.name):
                self.assertLessEqual(path.stat().st_size,24*22050*2+44)
                with wave.open(str(path)) as track:
                    self.assertEqual((track.getnchannels(),track.getsampwidth(),track.getframerate()),(1,2,22050))
                    self.assertGreater(track.getnframes(),17*22050)
                    pcm = track.readframes(track.getnframes())
                samples = array('h',pcm)
                if sys.byteorder != 'little': samples.byteswap()
                self.assertLess(max(abs(v) for v in samples),15000)
                self.assertLess(abs(samples[0]-samples[-1]),2000)
                hashes.add(hashlib.sha256(pcm).digest())
        self.assertEqual(len(hashes),5)
