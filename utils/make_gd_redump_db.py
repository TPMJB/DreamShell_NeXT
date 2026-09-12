#!/usr/bin/env python3
"""Build the compact CRC catalog consumed by DreamShell GD Ripper."""

from __future__ import annotations

import argparse
import itertools
import re
from pathlib import Path
import sys
from typing import Iterable, Optional, Sequence, TextIO

from verify_gd_dump import DatGame, VerificationError, iter_dat_games


DB_HEADER = "DREAMSHELL_REDUMP_CRC_V1"


def _safe_field(value: str, limit: int = 240) -> str:
    """Return a single-line, Dreamcast-friendly database field."""
    value = " ".join(value.replace("\t", " ").split())
    value = value.encode("ascii", errors="replace").decode("ascii")
    return value[:limit] or "Unnamed DAT entry"


def write_database(
    games: Iterable[DatGame],
    output: TextIO,
    *,
    source: str = "unknown",
    catalog_format: str = "unknown",
) -> tuple[int, int]:
    """Write *games* and return ``(game_count, track_count)``."""
    output.write(DB_HEADER + "\n")
    output.write(f"# source\t{_safe_field(source)}\n")
    output.write(f"# format\t{_safe_field(catalog_format)}\n")

    game_count = 0
    track_count = 0
    for game in games:
        roms = [
            rom for _number, rom in sorted(game.roms.items())
            if rom.size is not None and rom.size > 0 and 1 <= rom.track_number <= 99
            and re.fullmatch(r"[0-9a-fA-F]{8}", rom.crc32)
        ]
        if not roms:
            continue

        output.write(f"G\t{len(roms)}\t{_safe_field(game.name)}\n")
        for rom in roms:
            output.write(
                f"T\t{rom.track_number}\t{rom.size}\t{rom.crc32.lower()}\n"
            )
        output.write("E\n")
        game_count += 1
        track_count += len(roms)

    return game_count, track_count


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Convert Redump or TOSEC Dreamcast DATs into the compact CRC "
            "database used by GD Ripper on a Dreamcast."
        )
    )
    parser.add_argument("dat", nargs="+", help="Redump/TOSEC DAT(s), optionally .zip or .gz")
    parser.add_argument("-o", "--output", required=True, help="Output redump.db or tosec.db path")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    output_path = Path(args.output).expanduser()
    try:
        catalogs = [iter_dat_games(dat) for dat in args.dat]
        games = itertools.chain.from_iterable(catalog[0] for catalog in catalogs)
        source = "; ".join(catalog[1] for catalog in catalogs)
        catalog_format = "; ".join(dict.fromkeys(catalog[2] for catalog in catalogs))
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", encoding="ascii", newline="\n") as output:
            game_count, track_count = write_database(
                games, output, source=source, catalog_format=catalog_format
            )
        print(
            f"Wrote {game_count} game entries and {track_count} track entries "
            f"to {output_path}"
        )
        return 0
    except (OSError, VerificationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
