"""Run real launcher C with recording graphics, shipped font metrics and Mini-XML."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class LauncherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.temp.name)/'launcher-test'
        cls.ctype_exe = Path(cls.temp.name)/'launcher-ctype-test'
        mxml = [str(p.relative_to(ROOT)) for p in (ROOT/'lib/mxml').glob('mxml-*.c')]
        command = ['gcc', '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-Wno-format-truncation', '-Wno-unused-parameter',
                        '-Wno-implicit-fallthrough', '-Wno-sign-compare',
                        '-Iutils/tests/launcher_shim', '-Iinclude/SDL', '-Ilib/mxml',
                        'utils/tests/launcher_harness.c', *mxml, '-pthread', '-o', str(cls.exe)]
        subprocess.run(command, cwd=ROOT, check=True)
        command.insert(1, '-DHOST_KLF_CTYPE_OFFSET')
        command[-1] = str(cls.ctype_exe)
        subprocess.run(command, cwd=ROOT, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_c(self, mode):
        with tempfile.TemporaryDirectory() as tmp:
            return subprocess.check_output([str(self.exe),mode,tmp],cwd=ROOT,text=True)

    def test_controller_mouse_navigation_and_safe_deletion(self):
        self.assertIn('passed', self.run_c('behavior'))

    def test_delayed_bounded_previews_and_stale_loads(self):
        self.assertIn('passed', self.run_c('previews'))

    def test_return_to_selected_app_after_unload(self):
        self.assertIn('passed', self.run_c('lifecycle'))

    def test_missing_artwork_unknown_apps_and_empty_list(self):
        self.assertIn('passed', self.run_c('faults'))

    def test_startup_fallback_and_native_texture_dimensions(self):
        self.assertIn('passed', self.run_c('textures'))

    def test_list_stays_inside_panel_when_scrolling(self):
        self.assertIn('passed', self.run_c('list-bounds'))

    def test_description_wraps_at_word_boundaries(self):
        self.assertIn('passed', self.run_c('word-wrap'))

    def test_word_wrap_with_shipped_klf_ctype_offset(self):
        output = subprocess.check_output([str(self.ctype_exe), 'word-wrap'],
                                         cwd=ROOT, text=True)
        self.assertIn('passed', output)
