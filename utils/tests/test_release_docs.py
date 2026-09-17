"""Check that the shipped guides remain usable outside the repository."""
import importlib.util
import posixpath
from pathlib import Path
import tempfile
import unittest
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('release_docs', ROOT / 'utils/release_docs.py')
docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(docs)
COMMIT = 'a' * 40


class ReleaseDocsTests(unittest.TestCase):
    def test_all_packaged_local_links_resolve_to_shipped_files(self):
        outputs = set(docs.GUIDES) | {'DS/doc/LICENSE', 'DS/doc/NOTICE'}
        for output in docs.GUIDES:
            rendered = docs.render_guide(ROOT, output, COMMIT).decode()
            for match in docs.LINK.finditer(rendered):
                parts = urlsplit(match['url'])
                if not parts.scheme and not parts.netloc and parts.path:
                    target = posixpath.normpath(posixpath.join(posixpath.dirname(output), parts.path))
                    self.assertIn(target, outputs, (output, match['url']))

    def test_nested_host_guide_links_back_to_installation_and_history(self):
        rendered = docs.render_guide(ROOT, 'host-tools/README.md', COMMIT).decode()
        self.assertIn('(../installation-guide.md)', rendered)
        self.assertIn('(../timestalkers-recovery.md)', rendered)

    def test_license_links_use_packaged_notices_and_source_links_pin_commit(self):
        rendered = docs.render_guide(ROOT, 'licensing.md', COMMIT).decode()
        self.assertIn('(DS/doc/LICENSE)', rendered)
        self.assertIn('(DS/doc/NOTICE)', rendered)
        self.assertIn(f'/blob/{COMMIT}/applications/gd_ripper/redump-db.README', rendered)

    def test_missing_source_link_stops_packaging(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'docs').mkdir()
            (root / 'docs/installation.md').write_text('[guide](missing.md)\n')
            with self.assertRaisesRegex(ValueError, 'Broken source documentation link'):
                docs.render_guide(root, 'installation-guide.md', COMMIT)

    def test_public_release_notes_pin_links_to_the_built_source(self):
        packaged = docs.render_guide(ROOT, 'README-FIRST.md', COMMIT).decode()
        self.assertIn(f'raw.githubusercontent.com/TPMJB/DreamShell_NeXT/{COMMIT}/docs/launch/1.0/k-ui-release-banner.jpg', packaged)
        rendered = docs.render_guide(ROOT, 'README-FIRST.md', COMMIT, public=True).decode()
        self.assertIn(f'/blob/{COMMIT}/docs/compatibility.md', rendered)
        self.assertIn(f'/blob/{COMMIT}/NOTICE', rendered)
        self.assertIn(f'raw.githubusercontent.com/TPMJB/DreamShell_NeXT/{COMMIT}/docs/launch/1.0/k-ui-release-banner.jpg', rendered)
        for match in docs.LINK.finditer(rendered):
            self.assertTrue(urlsplit(match['url']).scheme, match['url'])
