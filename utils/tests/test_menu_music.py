from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import wave

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

    def test_bundled_track_matches_playback_budget(self):
        path = ROOT/'applications/launch_app/music/menu.wav'
        if not path.exists():
            subprocess.run([sys.executable,str(ROOT/'utils/generate_menu_music.py')],check=True)
        self.assertLess(path.stat().st_size,2*1024*1024)
        with wave.open(str(path)) as track:
            self.assertEqual((track.getnchannels(),track.getsampwidth(),track.getframerate()),(1,2,22050))
            self.assertEqual(track.getnframes(),24*22050)
