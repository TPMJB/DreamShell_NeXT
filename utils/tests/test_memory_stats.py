from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MemoryStatsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.build.name) / 'memory-test'
        subprocess.run(['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                        '-Werror', '-fsanitize=address,undefined', '-no-pie',
                        '-Iutils/tests/console_shim', '-Iinclude/SDL', '-Iinclude',
                        'utils/tests/memory_harness.c', '-o', str(cls.exe)],
                       cwd=ROOT, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def check_case(self, case):
        with tempfile.TemporaryDirectory() as folder:
            subprocess.run([str(self.exe), case, folder], check=True, timeout=10)

    def test_counter_bounds_concurrency_and_lowest_sample(self):
        self.check_case('counters')

    def test_append_only_log_cap_and_failures(self):
        self.check_case('log')
