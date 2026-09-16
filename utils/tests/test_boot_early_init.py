"""Exercise the production hooks that run before KOS clears BSS."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('firmware/bootloader/src/main.c', 'src/main.c')


class EarlyBootTests(unittest.TestCase):
    def setUp(self):
        if not shutil.which('gcc') or not shutil.which('nm'):
            self.skipTest('gcc and nm are required for early boot checks')
        self.temp = tempfile.TemporaryDirectory(prefix='boot-early-')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def compile_hook(self, source):
        text = (ROOT/source).read_text()
        # Compile the actual hook, without the rest of either KOS application.
        hook = text[text.index('static void early_init(void)'):text.index(
            'KOS_INIT_EARLY(early_init);')]
        unit = self.directory/'hook.c'
        unit.write_text('''#include <stdint.h>
#define HW_TYPE_RETAIL 0x0
#define G1_ATA_MASTER 0x00
uint8_t g1_ata_select_device(uint8_t dev);
''' + hook + '\nvoid run_early_init(void) { early_init(); }\n')
        obj = self.directory/'hook.o'
        subprocess.run([
            'gcc', '-std=c11', '-O2', '-ffreestanding', '-fno-stack-protector',
            '-Wall', '-Wextra', '-Werror', '-c', str(unit), '-o', str(obj),
        ], check=True, capture_output=True, text=True)
        return obj

    def test_hooks_do_not_require_runtime_or_writable_globals(self):
        for source in SOURCES:
            with self.subTest(source=source):
                obj = self.compile_hook(source)
                undefined = subprocess.check_output(['nm', '-u', str(obj)], text=True)
                self.assertEqual(undefined.strip(), '',
                                 'Early startup must not call the KOS runtime')
                symbols = subprocess.check_output(
                    ['nm', '--defined-only', '--format=posix', str(obj)], text=True)
                writable = [line for line in symbols.splitlines()
                            if line.split()[1] in 'bBdDgGsSC']
                self.assertEqual(writable, [], 'BSS is not initialized yet')

    @unittest.skipUnless(sys.platform.startswith('linux'),
                         'the MMIO harness uses Linux mmap')
    def test_hooks_preserve_other_hardware_and_registers(self):
        for source in SOURCES:
            with self.subTest(source=source):
                obj = self.compile_hook(source)
                binary = self.directory/'early-boot-test'
                subprocess.run([
                    'gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(ROOT/'utils/tests/boot_early_init_harness.c'), str(obj),
                    '-o', str(binary),
                ], check=True, capture_output=True, text=True)
                subprocess.run([str(binary)], check=True, timeout=5,
                               capture_output=True, text=True)


if __name__ == '__main__':
    unittest.main()
