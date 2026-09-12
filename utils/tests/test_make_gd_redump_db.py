import io
from pathlib import Path
import sys
import tempfile
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import make_gd_redump_db as converter  # noqa: E402
import verify_gd_dump as verifier  # noqa: E402


class DreamShellDatabaseTests(unittest.TestCase):
    def test_database_is_compact_and_deterministic(self):
        games = [
            verifier.DatGame(
                "Sample\tGame \u00e9dition",
                {
                    3: verifier.DatRom(3, "Sample (Track 3).bin", 2352, "A1B2C3D4", "", ""),
                    1: verifier.DatRom(1, "Sample (Track 1).bin", 4704, "01020304", "", ""),
                },
            )
        ]
        output = io.StringIO()
        counts = converter.write_database(
            games, output, source="fixture.dat", catalog_format="Logiqx XML"
        )

        self.assertEqual(counts, (1, 2))
        self.assertEqual(
            output.getvalue(),
            "DREAMSHELL_REDUMP_CRC_V1\n"
            "# source\tfixture.dat\n"
            "# format\tLogiqx XML\n"
            "G\t2\tSample Game ?dition\n"
            "T\t1\t4704\t01020304\n"
            "T\t3\t2352\ta1b2c3d4\n"
            "E\n",
        )

    def test_cli_converts_clrmamepro_dat(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            dat = root / "dreamcast.dat"
            output = root / "redump.db"
            dat.write_text(
                'clrmamepro ( name "Dreamcast" )\n'
                'game (\n name "Example"\n'
                ' rom ( name "Example (Track 3).bin" size 1185760800 '
                'crc FAD606CB md5 00 sha1 00 )\n)\n',
                encoding="utf-8",
            )

            self.assertEqual(converter.main([str(dat), "-o", str(output)]), 0)
            text = output.read_text(encoding="ascii")
            self.assertIn("G\t1\tExample\n", text)
            self.assertIn("T\t3\t1185760800\tfad606cb\n", text)


if __name__ == "__main__":
    unittest.main()
