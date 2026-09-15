"""Harmless build records and explicit checks in the release packager."""
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('build_provenance', ROOT / 'utils/build_provenance.py')
provenance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provenance)
COMMIT = 'a' * 40


class BuildProvenanceTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        for name in ('NOTICE', 'LICENSE', 'VERSION'):
            shutil.copyfile(ROOT / name, self.root / name)
        for name in (*provenance.BRANDING_FILES, 'resources/next-branding.sha256'):
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, self.root / name)
        self.version = (self.root / 'VERSION').read_text().strip()

    def entries(self):
        record = provenance.build_record(self.root)
        record.update(source_commit=COMMIT, tracked_changes=False)
        data = provenance.encode_record(record)
        return {
            'DS/doc/NOTICE': (self.root / 'NOTICE').read_bytes(),
            'DS/doc/LICENSE': (self.root / 'LICENSE').read_bytes(),
            provenance.RECORD: data,
            **{'DS/' + name: b'core-prefix\0' + data + b'\0core-suffix'
               for name in provenance.CORE_NAMES},
        }

    def validate(self, entries, commit=COMMIT):
        buffer = io.BytesIO()
        with ZipFile(buffer, 'w') as archive:
            for name, data in entries.items():
                archive.writestr(name, data)
        buffer.seek(0)
        with ZipFile(buffer) as archive:
            return provenance.validate_archive(archive, self.root, commit, self.version)

    def test_matching_attribution_is_accepted(self):
        record = self.validate(self.entries())
        self.assertEqual(record['maintainer'], 'TPMJB')
        self.assertEqual(record['source_commit'], COMMIT)

    def test_changed_branding_fails_explicitly_during_packaging(self):
        for name in provenance.BRANDING_FILES:
            with self.subTest(name=name):
                path = self.root / name
                original = path.read_bytes()
                path.write_bytes(b'replaced artwork')
                with self.assertRaisesRegex(ValueError, 'Official NeXT branding changed'):
                    self.validate(self.entries())
                path.write_bytes(original)

    def test_missing_source_attribution_is_rejected_for_each_contributor(self):
        original = (self.root / 'NOTICE').read_text()
        for notice in (*provenance.UPSTREAM_NOTICES, provenance.NEXT_NOTICE):
            with self.subTest(notice=notice):
                (self.root / 'NOTICE').write_text(original.replace(notice, ''))
                with self.assertRaisesRegex(ValueError, 'missing attribution'):
                    self.validate(self.entries())
        (self.root / 'NOTICE').write_text(original)

    def test_stale_or_stripped_packaged_legal_notices_are_rejected(self):
        for name in ('NOTICE', 'LICENSE'):
            with self.subTest(name=name):
                entries = self.entries()
                entries['DS/doc/' + name] = b'old or stripped documentation'
                with self.assertRaisesRegex(ValueError, f'Packaged {name} differs'):
                    self.validate(entries)

    def test_missing_embedded_record_is_rejected_for_each_core(self):
        for name in provenance.CORE_NAMES:
            with self.subTest(name=name):
                entries = self.entries()
                entries['DS/' + name] = b'core with the record stripped out'
                with self.assertRaisesRegex(ValueError, 'missing matching NeXT build attribution'):
                    self.validate(entries)

    def test_relabelled_metadata_is_rejected(self):
        for key, value in (('maintainer', 'Someone else'), ('project', 'Another shell'),
                           ('source_commit', 'b' * 40), ('version', '9.9.9')):
            with self.subTest(key=key):
                entries = self.entries()
                record = json.loads(entries[provenance.RECORD])
                record[key] = value
                entries[provenance.RECORD] = provenance.encode_record(record)
                with self.assertRaisesRegex(ValueError, 'attribution mismatch: ' + key):
                    self.validate(entries)

    def test_missing_or_malformed_packaged_record_is_rejected(self):
        entries = self.entries()
        del entries[provenance.RECORD]
        with self.assertRaisesRegex(ValueError, 'missing DS/doc/next-build.json'):
            self.validate(entries)
        for invalid in (b'not JSON', b'[]'):
            entries = self.entries()
            entries[provenance.RECORD] = invalid
            with self.assertRaises(ValueError):
                self.validate(entries)

    def test_source_archive_generates_an_explicit_unknown_revision(self):
        data = provenance.generate(self.root)
        record = json.loads(data)
        self.assertEqual(record['source_commit'], 'unknown')
        self.assertIsNone(record['tracked_changes'])
        target = self.root / 'romdisk/next-build.json'
        timestamp = target.stat().st_mtime_ns
        self.assertEqual(provenance.generate(self.root), data)
        self.assertEqual(target.stat().st_mtime_ns, timestamp)

    def test_git_revision_and_tracked_changes_are_recorded(self):
        def git(*args):
            return subprocess.check_output([
                'git', '-c', 'user.name=Provenance test',
                '-c', 'user.email=test@example.invalid', *args,
            ], cwd=self.root, text=True, stderr=subprocess.DEVNULL).strip()
        git('init', '-q')
        git('add', 'NOTICE', 'LICENSE', 'VERSION')
        git('commit', '-qm', 'Test source')
        record = json.loads(provenance.generate(self.root))
        self.assertEqual(record['source_commit'], git('rev-parse', 'HEAD'))
        self.assertFalse(record['tracked_changes'])
        (self.root / 'VERSION').write_text('9.9.9\n')
        record = json.loads(provenance.generate(self.root))
        self.assertEqual(record['version'], '9.9.9')
        self.assertTrue(record['tracked_changes'])

    def test_real_makefile_orders_generation_and_refreshes_incremental_docs(self):
        # Replace only KOS's ROM-image command with a copy. The real project
        # Makefile must generate the record first and refresh existing staging.
        shutil.copyfile(ROOT / 'Makefile', self.root / 'Makefile')
        (self.root / 'utils').mkdir()
        shutil.copyfile(ROOT / 'utils/build_provenance.py', self.root / 'utils/build_provenance.py')
        (self.root / 'sdk').mkdir()
        (self.root / 'sdk/Makefile.cfg').write_text(
            'DS_BASE = $(CURDIR)\nDS_BUILD = $(CURDIR)/build\n'
            'DS_RES = $(CURDIR)/resources\nPYTHON = python3\n'
            'romdisk.img:\n\tcp romdisk/next-build.json $@\n')
        for directory in ('resources/sfx', 'resources/doc', 'resources/lua',
                          'build/sfx', 'build/doc', 'build/lua'):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        (self.root / 'resources/doc/about.txt').write_text('NeXT credits\n')
        for name in ('click', 'click2', 'screenshot', 'move', 'chpage', 'slide',
                     'error', 'success', 'coin'):
            for directory, timestamp in (('resources/sfx', 1000), ('build/sfx', 2000)):
                path = self.root / directory / (name + '.wav')
                path.touch()
                os.utime(path, (timestamp, timestamp))
        for directory, timestamp in (('resources/lua', 1000), ('build/lua', 2000)):
            path = self.root / directory / 'startup.lua'
            path.touch()
            os.utime(path, (timestamp, timestamp))
        (self.root / 'build/doc/NOTICE').write_text('stale notice')
        previous = None
        for iteration in range(2):
            subprocess.run(['make', '--no-print-directory', 'romdisk.img', 'make-build'],
                           cwd=self.root, check=True, stdout=subprocess.DEVNULL,
                           stderr=subprocess.PIPE, timeout=10)
            data = (self.root / 'romdisk.img').read_bytes()
            self.assertEqual(data, (self.root / 'build/doc/next-build.json').read_bytes())
            self.assertEqual((self.root / 'NOTICE').read_bytes(),
                             (self.root / 'build/doc/NOTICE').read_bytes())
            if previous is not None:
                self.assertNotEqual(data, previous)
            previous = data
            with (self.root / 'NOTICE').open('a') as notice:
                notice.write('\nAdditional test attribution.\n')
