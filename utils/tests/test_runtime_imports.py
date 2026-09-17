"""Regression coverage for the complete KOS runtime export-table selection."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from utils.package_menu_music import check_imports


class RuntimeImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.kos = Path(self.temp.name)
        files = {
            'kernel/exports.txt': 'kernel_fixture\n',
            'kernel/arch/dreamcast/exports.txt': 'arch_fixture\n',
            'kernel/arch/dreamcast/exports-pristine.txt':
                'include dc/cdrom.h\n# Drive API\ncdrom_get_status\n',
            'kernel/arch/dreamcast/exports-naomi.txt': 'naomi_fixture\n',
        }
        for name, content in files.items():
            file = self.kos/name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(content)

    def check(self, names, subarch='pristine'):
        output = ''.join(f'         U _{name}\n' for name in names)
        with patch.dict('os.environ', {'KOS_SUBARCH': subarch}), patch(
                'utils.package_menu_music.subprocess.check_output', return_value=output):
            check_imports(Path('module.klf'), self.kos, 'nm')

    def test_dreamcast_subarchitecture_exports_are_available(self):
        self.check(['kernel_fixture', 'arch_fixture', 'cdrom_get_status'])

    def test_unavailable_import_is_still_rejected(self):
        with self.assertRaisesRegex(SystemExit, '_missing_fixture'):
            self.check(['cdrom_get_status', 'missing_fixture'])

    def test_other_subarchitecture_does_not_allow_dreamcast_exports(self):
        self.check(['naomi_fixture'], 'naomi')
        with self.assertRaisesRegex(SystemExit, '_cdrom_get_status'):
            self.check(['cdrom_get_status'], 'naomi')

    def test_missing_selected_table_fails_closed(self):
        (self.kos/'kernel/arch/dreamcast/exports-pristine.txt').unlink()
        with self.assertRaises(FileNotFoundError):
            self.check(['kernel_fixture'])
