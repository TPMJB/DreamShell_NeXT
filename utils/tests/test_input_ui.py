"""Production modal keyboard + event dispatcher, with storage-independent GUI stubs."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class KeyboardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.tmp.name)/'keyboard'
        subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-fno-pie','-no-pie',
                        '-Iutils/tests/input_shim','-Iinclude/SDL',
                        'utils/tests/input_harness.c','-o',str(cls.exe)],cwd=ROOT,check=True)
    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()
    def check(self,mode):
        self.assertIn('passed',subprocess.check_output([str(self.exe),mode],cwd=ROOT,text=True))
    def test_typing_is_modal_single_entry_and_cancel_restores(self): self.check('typing')
    def test_capacity_and_cursor_editing(self): self.check('capacity')
    def test_navigation_repeat_neutral_and_mouse(self): self.check('navigation')
    def test_load_failure_unload_focus_change_and_console(self): self.check('lifecycle')
