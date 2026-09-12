import contextlib
import hashlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
import zlib
import zipfile


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import verify_gd_dump as verifier  # noqa: E402


def digests(data):
    return {
        "size": str(len(data)),
        "crc": f"{zlib.crc32(data) & 0xffffffff:08x}",
        "md5": hashlib.md5(data).hexdigest(),
        "sha1": hashlib.sha1(data).hexdigest(),
    }


class VerifierTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.payloads = {
            1: b"A" * 2352,
            2: b"B" * 2352,
            3: b"C" * 2352,
        }
        self._make_dump(self.root / "dump-a", self.payloads)

    def tearDown(self):
        self.temp.cleanup()

    def _make_dump(self, root, payloads, sector_size=2352, complete=True):
        root.mkdir()
        rows = ["3"]
        state = [
            verifier.STATE_HEADER,
            "disc_type 128",
            f"use_bin {int(sector_size == 2352)}",
            "tracks 3",
        ]
        controls = {1: 4, 2: 0, 3: 4}
        starts = {1: 150, 2: 2010, 3: 45150}
        for number in (1, 2, 3):
            suffix = "raw" if number == 2 else "bin"
            filename = f"track{number:02d}.{suffix}"
            data = payloads[number]
            (root / filename).write_bytes(data)
            rows.append(f"{number} {starts[number] - 150} {controls[number]} {sector_size} {filename} 0")
            count = len(data) // sector_size
            state.append(
                f"{number} {starts[number]} {count} {controls[number]} {sector_size} {filename}"
            )
        (root / "sample.gdi").write_text("\n".join(rows) + "\n", encoding="utf-8")
        (root / "rip.state").write_text("\n".join(state) + "\n", encoding="utf-8")
        if complete:
            (root / "rip.complete").write_text("Complete: 3 sectors\n", encoding="utf-8")

    def _xml_dat(self, path, payloads):
        roms = []
        for number in (1, 2, 3):
            values = digests(payloads[number])
            roms.append(
                '<rom name="Sample (Track {0:02d}).bin" size="{size}" crc="{crc}" '
                'md5="{md5}" sha1="{sha1}"/>'.format(number, **values)
            )
        path.write_text(
            "<?xml version=\"1.0\"?><datafile><game name=\"Sample Game\">"
            + "".join(roms)
            + "</game></datafile>",
            encoding="utf-8",
        )

    def test_full_xml_match(self):
        dump = verifier.inspect_dump(str(self.root / "dump-a"))
        dat = self.root / "full.dat"
        self._xml_dat(dat, self.payloads)
        matches, _label, kind = verifier.find_dat_matches(dump, str(dat))
        self.assertEqual(kind, "Logiqx XML")
        self.assertTrue(matches[0].full_match)
        self.assertEqual(verifier._dat_verdict(dump, matches[0])[0], "FULL TRACK MATCH")

    def test_data_tracks_match_when_audio_differs(self):
        dump = verifier.inspect_dump(str(self.root / "dump-a"))
        dat_payloads = dict(self.payloads)
        dat_payloads[2] = b"Z" * 2352
        dat = self.root / "data-only-match.dat"
        self._xml_dat(dat, dat_payloads)
        matches, _label, _kind = verifier.find_dat_matches(dump, str(dat))
        self.assertFalse(matches[0].full_match)
        self.assertTrue(matches[0].all_data_match)
        self.assertIn("DATA TRACKS MATCH", verifier._dat_verdict(dump, matches[0])[0])

    def test_reduced_clrmamepro_dat_is_identification_only(self):
        dump = verifier.inspect_dump(str(self.root / "dump-a"))
        values = digests(self.payloads[3])
        dat = self.root / "reduced.dat"
        dat.write_text(
            "clrmamepro ( name \"Test\" )\n"
            "game (\n"
            "  name \"Sample Game\"\n"
            "  description \"Sample Game\"\n"
            "  rom ( name \"Sample Game (Track 3).bin\" size {size} crc {crc} "
            "md5 {md5} sha1 {sha1} )\n"
            ")\n".format(**values),
            encoding="utf-8",
        )
        matches, _label, kind = verifier.find_dat_matches(dump, str(dat))
        verdict, accepted = verifier._dat_verdict(dump, matches[0])
        self.assertEqual(kind, "ClrMamePro")
        self.assertTrue(accepted)
        self.assertIn("IDENTIFIED BY DATA TRACK", verdict)

    def test_bad_sector_map_makes_dump_unclean(self):
        sidecar = self.root / "dump-a" / "track03.bin.bad"
        sidecar.write_text(
            "track,track_sector,disc_lba,disc_fad,file_offset\n3,0,45000,45150,0\n",
            encoding="utf-8",
        )
        dump = verifier.inspect_dump(str(self.root / "dump-a"))
        self.assertFalse(dump.clean)
        self.assertEqual(dump.zero_filled_sectors, 1)

    def test_missing_completion_marker_makes_dump_unclean(self):
        (self.root / "dump-a" / "rip.complete").unlink()
        dump = verifier.inspect_dump(str(self.root / "dump-a"))
        self.assertFalse(dump.clean)
        self.assertTrue(any("rip.complete" in item for item in dump.problems))

    def test_completion_sector_count_is_checked(self):
        (self.root / "dump-a" / "rip.complete").write_text(
            "Complete: 999 sectors\n", encoding="ascii"
        )
        dump = verifier.inspect_dump(str(self.root / "dump-a"))
        self.assertFalse(dump.clean)
        self.assertTrue(any("track files contain 3" in item for item in dump.problems))

    def test_zipped_xml_dat(self):
        dump = verifier.inspect_dump(str(self.root / "dump-a"))
        dat = self.root / "full.xml"
        archive = self.root / "full.zip"
        self._xml_dat(dat, self.payloads)
        with zipfile.ZipFile(archive, "w") as output:
            output.write(dat, "Dreamcast.dat")
        matches, label, kind = verifier.find_dat_matches(dump, str(archive))
        self.assertEqual(kind, "Logiqx XML")
        self.assertIn("Dreamcast.dat", label)
        self.assertTrue(matches[0].full_match)

    def test_cli_integrity_check_passes(self):
        with contextlib.redirect_stdout(io.StringIO()):
            result = verifier.main([str(self.root / "dump-a")])
        self.assertEqual(result, 0)

    def test_independent_dumps_match(self):
        self._make_dump(self.root / "dump-b", self.payloads)
        left = verifier.inspect_dump(str(self.root / "dump-a"))
        right = verifier.inspect_dump(str(self.root / "dump-b"))
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(verifier.compare_dumps(left, right))

    def test_first_difference_reports_sector_and_byte(self):
        changed = dict(self.payloads)
        changed[3] = bytearray(changed[3])
        changed[3][2351] = ord("D")
        changed[3] = bytes(changed[3])
        self._make_dump(self.root / "dump-b", changed)
        left = verifier.inspect_dump(str(self.root / "dump-a"))
        right = verifier.inspect_dump(str(self.root / "dump-b"))
        result = verifier._first_difference(left.tracks[2], right.tracks[2])
        self.assertEqual(result, (2351, 0, 2351))


if __name__ == "__main__":
    unittest.main()
