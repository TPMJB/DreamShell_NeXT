#!/usr/bin/env python3
"""Verify DreamShell GD Ripper output locally.

The tool checks DreamShell's resume/completion metadata, hashes every track,
compares independent dumps, and understands both Logiqx XML and ClrMamePro
Redump DAT files.  It deliberately distinguishes a full track-set match from
the more common case where only the data tracks match Redump.
"""

from __future__ import annotations

import argparse
import contextlib
import gzip
import hashlib
import re
import shlex
import sys
import zipfile
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, Dict, Iterable, Iterator, List, Optional, Sequence, Tuple
import xml.etree.ElementTree as ET


HASH_CHUNK = 4 * 1024 * 1024
STATE_HEADER = "DreamShell GD Ripper state v1"
TRACK_RE = re.compile(r"\(\s*Track\s*0*(\d+)\s*\)", re.IGNORECASE)
FALLBACK_TRACK_RE = re.compile(r"(?:track|trk)[ _.-]*0*(\d+)", re.IGNORECASE)
ATTR_RE = re.compile(
    r"([A-Za-z_][A-Za-z0-9_-]*)\s+(?:\"((?:\\.|[^\"])*)\"|([^\s()]+))"
)


class VerificationError(Exception):
    """An input cannot be parsed or safely verified."""


@dataclass
class Track:
    number: int
    lba: int
    control: int
    sector_size: int
    filename: str
    file_offset: int
    path: Path
    payload_size: int = 0
    crc32: str = ""
    md5: str = ""
    sha1: str = ""

    @property
    def kind(self) -> str:
        return "data" if self.control == 4 else "audio"

    @property
    def sectors(self) -> Optional[int]:
        if self.sector_size <= 0 or self.payload_size % self.sector_size:
            return None
        return self.payload_size // self.sector_size


@dataclass
class Dump:
    root: Path
    gdi_path: Path
    tracks: List[Track]
    complete: bool = False
    complete_sectors: Optional[int] = None
    state_present: bool = False
    zero_filled_sectors: int = 0
    problems: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)

    @property
    def clean(self) -> bool:
        return self.complete and not self.problems and self.zero_filled_sectors == 0


@dataclass(frozen=True)
class DatRom:
    track_number: int
    name: str
    size: Optional[int]
    crc32: str
    md5: str
    sha1: str


@dataclass
class DatGame:
    name: str
    roms: Dict[int, DatRom]


@dataclass
class Match:
    game: DatGame
    statuses: Dict[int, str]
    exact_tracks: int
    exact_data_tracks: int
    represented_data_tracks: int
    full_match: bool
    all_data_match: bool

    @property
    def rank(self) -> Tuple[int, int, int, int, int]:
        return (
            int(self.full_match),
            int(self.all_data_match),
            self.exact_data_tracks,
            self.exact_tracks,
            self.represented_data_tracks,
        )


def _resolve_gdi(value: str) -> Path:
    supplied = Path(value).expanduser()
    if supplied.is_file():
        if supplied.suffix.lower() != ".gdi":
            raise VerificationError(f"Not a .gdi file: {supplied}")
        return supplied.resolve()
    if not supplied.is_dir():
        raise VerificationError(f"Dump path does not exist: {supplied}")

    candidates = sorted(
        path for path in supplied.iterdir()
        if path.is_file() and path.suffix.lower() == ".gdi"
    )
    if not candidates:
        raise VerificationError(f"No .gdi file found in {supplied}")
    if len(candidates) > 1:
        names = ", ".join(path.name for path in candidates)
        raise VerificationError(
            f"More than one .gdi file found in {supplied}; specify one directly: {names}"
        )
    return candidates[0].resolve()


def parse_gdi(value: str) -> Dump:
    gdi_path = _resolve_gdi(value)
    root = gdi_path.parent
    try:
        lines = [line.strip() for line in gdi_path.read_text(
            encoding="utf-8-sig", errors="strict").splitlines() if line.strip()]
    except (OSError, UnicodeError) as exc:
        raise VerificationError(f"Cannot read {gdi_path}: {exc}") from exc

    if not lines:
        raise VerificationError(f"Empty GDI file: {gdi_path}")
    try:
        declared_count = int(lines[0])
    except ValueError as exc:
        raise VerificationError(f"Invalid first line in {gdi_path}: {lines[0]!r}") from exc

    tracks: List[Track] = []
    seen_numbers = set()
    root_real = root.resolve()
    for line_number, line in enumerate(lines[1:], start=2):
        try:
            fields = shlex.split(line, posix=True)
        except ValueError as exc:
            raise VerificationError(
                f"Invalid quoting in {gdi_path.name}, line {line_number}: {exc}"
            ) from exc
        if len(fields) != 6:
            raise VerificationError(
                f"Expected six fields in {gdi_path.name}, line {line_number}; got {len(fields)}"
            )
        try:
            number, lba, control, sector_size = map(int, fields[:4])
            file_offset = int(fields[5])
        except ValueError as exc:
            raise VerificationError(
                f"Invalid number in {gdi_path.name}, line {line_number}: {line}"
            ) from exc
        if number in seen_numbers:
            raise VerificationError(f"Duplicate track number {number} in {gdi_path.name}")
        if sector_size <= 0 or file_offset < 0:
            raise VerificationError(f"Invalid size or offset in {gdi_path.name}, line {line_number}")

        filename = fields[4]
        track_path = (root / filename).resolve()
        try:
            track_path.relative_to(root_real)
        except ValueError as exc:
            raise VerificationError(
                f"Track {number} points outside its dump folder: {filename}"
            ) from exc
        tracks.append(Track(number, lba, control, sector_size, filename,
                            file_offset, track_path))
        seen_numbers.add(number)

    if declared_count != len(tracks):
        raise VerificationError(
            f"GDI declares {declared_count} tracks but contains {len(tracks)} track rows"
        )
    if not tracks:
        raise VerificationError(f"GDI contains no tracks: {gdi_path}")
    return Dump(root=root, gdi_path=gdi_path, tracks=tracks)


def _hash_track(track: Track) -> None:
    try:
        file_size = track.path.stat().st_size
    except OSError as exc:
        raise VerificationError(f"Cannot stat track {track.number}: {track.path}: {exc}") from exc
    if track.file_offset > file_size:
        raise VerificationError(
            f"Track {track.number} offset {track.file_offset} exceeds file size {file_size}"
        )

    crc = 0
    md5 = hashlib.md5()
    sha1 = hashlib.sha1()
    payload_size = 0
    try:
        with track.path.open("rb") as stream:
            stream.seek(track.file_offset)
            while True:
                chunk = stream.read(HASH_CHUNK)
                if not chunk:
                    break
                payload_size += len(chunk)
                crc = zlib.crc32(chunk, crc)
                md5.update(chunk)
                sha1.update(chunk)
    except OSError as exc:
        raise VerificationError(f"Cannot hash {track.path}: {exc}") from exc

    track.payload_size = payload_size
    track.crc32 = f"{crc & 0xffffffff:08x}"
    track.md5 = md5.hexdigest()
    track.sha1 = sha1.hexdigest()


def _parse_state(dump: Dump) -> Dict[int, Tuple[int, int, int, int, str]]:
    state_path = dump.root / "rip.state"
    if not state_path.is_file():
        dump.warnings.append(
            "rip.state is absent; this may be a legacy dump, so expected track lengths cannot be checked"
        )
        return {}
    dump.state_present = True
    try:
        lines = [line.strip() for line in state_path.read_text(
            encoding="utf-8", errors="replace").splitlines() if line.strip()]
    except OSError as exc:
        dump.problems.append(f"Cannot read rip.state: {exc}")
        return {}
    if not lines or lines[0] != STATE_HEADER:
        dump.problems.append("rip.state has an unknown or invalid header")
        return {}

    expected_count: Optional[int] = None
    state_tracks: Dict[int, Tuple[int, int, int, int, str]] = {}
    for line in lines[1:]:
        fields = shlex.split(line)
        if not fields:
            continue
        if fields[0] == "tracks" and len(fields) == 2:
            try:
                expected_count = int(fields[1])
            except ValueError:
                dump.problems.append(f"Invalid rip.state track count: {line}")
        elif fields[0].isdigit() and len(fields) == 6:
            try:
                number, fad, count, control, sector_size = map(int, fields[:5])
            except ValueError:
                dump.problems.append(f"Invalid rip.state track row: {line}")
                continue
            state_tracks[number] = (fad, count, control, sector_size, fields[5])

    if expected_count is not None and expected_count != len(state_tracks):
        dump.problems.append(
            f"rip.state declares {expected_count} tracks but describes {len(state_tracks)}"
        )
    return state_tracks


def _count_bad_sectors(dump: Dump) -> int:
    count = 0
    for track in dump.tracks:
        sidecar = Path(str(track.path) + ".bad")
        if not sidecar.is_file():
            continue
        try:
            for line in sidecar.read_text(encoding="utf-8", errors="replace").splitlines():
                stripped = line.strip()
                if stripped and not stripped.lower().startswith("track,"):
                    count += 1
        except OSError as exc:
            dump.problems.append(f"Cannot read bad-sector map {sidecar.name}: {exc}")
    return count


def inspect_dump(value: str, announce: bool = False) -> Dump:
    dump = parse_gdi(value)
    state_tracks = _parse_state(dump)
    complete_path = dump.root / "rip.complete"
    dump.complete = complete_path.is_file()
    if not dump.complete:
        dump.problems.append(
            "rip.complete is absent; the rip may be unfinished (or may predate completion markers)"
        )
    else:
        try:
            marker = complete_path.read_text(encoding="ascii", errors="replace").strip()
            marker_match = re.fullmatch(r"Complete:\s*(\d+)\s+sectors", marker)
            if marker_match:
                dump.complete_sectors = int(marker_match.group(1))
            else:
                dump.problems.append("rip.complete has an invalid or unknown format")
        except OSError as exc:
            dump.problems.append(f"Cannot read rip.complete: {exc}")

    if announce:
        print(f"Hashing {len(dump.tracks)} track(s) from {dump.root} ...", flush=True)
    for track in dump.tracks:
        if not track.path.is_file():
            raise VerificationError(f"Missing track file: {track.path}")
        _hash_track(track)
        if track.file_offset:
            dump.warnings.append(
                f"Track {track.number} has a nonzero GDI file offset ({track.file_offset}); hashes cover only its payload"
            )
        if track.payload_size % track.sector_size:
            dump.problems.append(
                f"Track {track.number} size {track.payload_size} is not aligned to {track.sector_size}-byte sectors"
            )

        expected = state_tracks.get(track.number)
        if expected:
            _fad, sector_count, control, sector_size, filename = expected
            expected_size = sector_count * sector_size
            if track.control != control or track.sector_size != sector_size:
                dump.problems.append(
                    f"Track {track.number} GDI format disagrees with rip.state"
                )
            if track.filename != filename:
                dump.problems.append(
                    f"Track {track.number} filename disagrees with rip.state ({track.filename!r} vs {filename!r})"
                )
            if track.payload_size != expected_size:
                dump.problems.append(
                    f"Track {track.number} is {track.payload_size} bytes; rip.state expects {expected_size}"
                )
        elif state_tracks:
            dump.problems.append(f"Track {track.number} is missing from rip.state")

    dump.zero_filled_sectors = _count_bad_sectors(dump)
    if dump.zero_filled_sectors:
        dump.problems.append(
            f"Bad-sector maps record {dump.zero_filled_sectors} zero-filled sector(s)"
        )
    copied_sectors = sum(track.sectors or 0 for track in dump.tracks)
    if dump.complete_sectors is not None and dump.complete_sectors != copied_sectors:
        dump.problems.append(
            f"rip.complete records {dump.complete_sectors} sectors, but track files contain {copied_sectors}"
        )
    return dump


@contextlib.contextmanager
def _open_catalog(path: Path) -> Iterator[Tuple[BinaryIO, str]]:
    suffix = path.suffix.lower()
    if suffix == ".zip":
        archive = zipfile.ZipFile(path, "r")
        try:
            members = [entry for entry in archive.infolist()
                       if not entry.is_dir() and Path(entry.filename).suffix.lower()
                       in {".dat", ".xml"}]
            if not members:
                raise VerificationError(f"No .dat or .xml file found inside {path}")
            members.sort(key=lambda entry: entry.file_size, reverse=True)
            stream = archive.open(members[0], "r")
            try:
                yield stream, f"{path}::{members[0].filename}"
            finally:
                stream.close()
        finally:
            archive.close()
    elif suffix == ".gz":
        with gzip.open(path, "rb") as stream:
            yield stream, str(path)
    else:
        with path.open("rb") as stream:
            yield stream, str(path)


def _catalog_kind(path: Path) -> Tuple[str, str]:
    try:
        with _open_catalog(path) as (stream, label):
            head = stream.read(4096).lstrip(b"\xef\xbb\xbf \t\r\n")
    except (OSError, zipfile.BadZipFile) as exc:
        raise VerificationError(f"Cannot open DAT {path}: {exc}") from exc
    if head.startswith(b"<") or head.startswith((b"\xff\xfe<\x00", b"\xfe\xff\x00<")):
        return "xml", label
    return "clrmamepro", label


def _track_number(name: str) -> Optional[int]:
    match = TRACK_RE.search(name)
    if not match:
        match = FALLBACK_TRACK_RE.search(Path(name).stem)
    return int(match.group(1)) if match else None


def _rom_from_attrs(attrs: Dict[str, str]) -> Optional[DatRom]:
    normalized = {key.lower(): value for key, value in attrs.items()}
    name = normalized.get("name", "")
    number = _track_number(name)
    if number is None or Path(name).suffix.lower() in {".cue", ".gdi"}:
        return None
    size: Optional[int] = None
    if normalized.get("size"):
        try:
            size = int(normalized["size"])
        except ValueError:
            pass
    return DatRom(
        track_number=number,
        name=name,
        size=size,
        crc32=normalized.get("crc", "").lower(),
        md5=normalized.get("md5", "").lower(),
        sha1=normalized.get("sha1", "").lower(),
    )


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1].lower()


def _iter_xml_games(path: Path) -> Iterator[DatGame]:
    try:
        with _open_catalog(path) as (stream, _label):
            for _event, elem in ET.iterparse(stream, events=("end",)):
                if _local_name(elem.tag) not in {"game", "machine"}:
                    continue
                roms: Dict[int, DatRom] = {}
                for child in elem.iter():
                    if _local_name(child.tag) != "rom":
                        continue
                    rom = _rom_from_attrs(dict(child.attrib))
                    if rom:
                        roms[rom.track_number] = rom
                if roms:
                    yield DatGame(elem.attrib.get("name", "Unnamed DAT entry"), roms)
                elem.clear()
    except ET.ParseError as exc:
        raise VerificationError(f"Invalid XML DAT: {exc}") from exc
    except OSError as exc:
        raise VerificationError(f"Cannot read DAT {path}: {exc}") from exc


def _balanced_blocks(text: str, keyword: str) -> Iterator[str]:
    pattern = re.compile(r"\b" + re.escape(keyword) + r"\s*\(", re.IGNORECASE)
    position = 0
    while True:
        match = pattern.search(text, position)
        if not match:
            return
        opening = text.find("(", match.start())
        depth = 0
        quoted = False
        escaped = False
        index = opening
        while index < len(text):
            char = text[index]
            if quoted:
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == '"':
                    quoted = False
            elif char == '"':
                quoted = True
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    yield text[opening + 1:index]
                    position = index + 1
                    break
            index += 1
        else:
            raise VerificationError(f"Unclosed {keyword} block in ClrMamePro DAT")


def _parse_attrs(text: str) -> Dict[str, str]:
    result: Dict[str, str] = {}
    for match in ATTR_RE.finditer(text):
        value = match.group(2) if match.group(2) is not None else match.group(3)
        result[match.group(1).lower()] = value.replace(r'\"', '"').replace(r"\\", "\\")
    return result


def _iter_clrmamepro_games(path: Path) -> Iterator[DatGame]:
    try:
        with _open_catalog(path) as (stream, _label):
            text = stream.read().decode("utf-8-sig", errors="replace")
    except OSError as exc:
        raise VerificationError(f"Cannot read DAT {path}: {exc}") from exc

    for block in _balanced_blocks(text, "game"):
        rom_blocks = list(_balanced_blocks(block, "rom"))
        first_rom = re.search(r"\brom\s*\(", block, re.IGNORECASE)
        header = block[:first_rom.start()] if first_rom else block
        game_name = _parse_attrs(header).get("name", "Unnamed DAT entry")
        roms: Dict[int, DatRom] = {}
        for rom_block in rom_blocks:
            rom = _rom_from_attrs(_parse_attrs(rom_block))
            if rom:
                roms[rom.track_number] = rom
        if roms:
            yield DatGame(game_name, roms)


def iter_dat_games(path_value: str) -> Tuple[Iterable[DatGame], str, str]:
    path = Path(path_value).expanduser().resolve()
    if not path.is_file():
        raise VerificationError(f"DAT does not exist: {path}")
    kind, label = _catalog_kind(path)
    if kind == "xml":
        return _iter_xml_games(path), label, "Logiqx XML"
    return _iter_clrmamepro_games(path), label, "ClrMamePro"


def _digest_matches(track: Track, rom: DatRom) -> bool:
    if rom.size is not None and rom.size != track.payload_size:
        return False
    if rom.sha1:
        return rom.sha1 == track.sha1
    if rom.md5:
        return rom.md5 == track.md5
    if rom.crc32:
        return rom.crc32 == track.crc32
    return False


def _evaluate_game(dump: Dump, game: DatGame) -> Match:
    statuses: Dict[int, str] = {}
    exact = 0
    exact_data = 0
    represented_data = 0
    local_numbers = {track.number for track in dump.tracks}
    for track in dump.tracks:
        rom = game.roms.get(track.number)
        if rom is None:
            statuses[track.number] = "not listed"
            continue
        if track.kind == "data":
            represented_data += 1
        if rom.size is not None and rom.size != track.payload_size:
            statuses[track.number] = f"size differs (DAT {rom.size})"
        elif _digest_matches(track, rom):
            statuses[track.number] = "MATCH"
            exact += 1
            if track.kind == "data":
                exact_data += 1
        else:
            statuses[track.number] = "hash differs"

    data_tracks = [track for track in dump.tracks if track.kind == "data"]
    all_data_match = bool(data_tracks) and all(
        statuses.get(track.number) == "MATCH" for track in data_tracks
    )
    full_match = (
        set(game.roms) == local_numbers
        and all(statuses.get(track.number) == "MATCH" for track in dump.tracks)
    )
    return Match(game, statuses, exact, exact_data, represented_data,
                 full_match, all_data_match)


def find_dat_matches(dump: Dump, dat_value: str) -> Tuple[List[Match], str, str]:
    games, label, kind = iter_dat_games(dat_value)
    matches: List[Match] = []
    for game in games:
        match = _evaluate_game(dump, game)
        # Audio tracks are often shared silence/padding.  Require an exact data
        # track so an audio-only coincidence cannot identify a game.
        if match.exact_data_tracks:
            matches.append(match)
    matches.sort(key=lambda candidate: candidate.rank, reverse=True)
    return matches, label, kind


def _format_bytes(value: int) -> str:
    units = ("B", "KiB", "MiB", "GiB", "TiB")
    amount = float(value)
    for unit in units:
        if amount < 1024.0 or unit == units[-1]:
            return f"{amount:.1f} {unit}" if unit != "B" else f"{value} B"
        amount /= 1024.0
    return f"{value} B"


def _print_dump(dump: Dump, title: str = "DreamShell dump") -> None:
    print(f"\n{title}")
    print(f"  GDI: {dump.gdi_path}")
    print(f"  Completion marker: {'present' if dump.complete else 'ABSENT'}")
    print(f"  Resume metadata: {'present' if dump.state_present else 'absent'}")
    print()
    print("  Trk  Type   Sector       Sectors          Bytes  CRC32     SHA-1")
    print("  ---  -----  ------  ------------  -------------  --------  ----------------------------------------")
    for track in dump.tracks:
        sectors = str(track.sectors) if track.sectors is not None else "unaligned"
        print(
            f"  {track.number:>3}  {track.kind:<5}  {track.sector_size:>6}  "
            f"{sectors:>12}  {track.payload_size:>13}  {track.crc32}  {track.sha1}"
        )
    if dump.warnings:
        print("\n  Warnings:")
        for warning in dump.warnings:
            print(f"    - {warning}")
    if dump.problems:
        print("\n  Integrity problems:")
        for problem in dump.problems:
            print(f"    - {problem}")
    else:
        print("\n  DreamShell integrity metadata: CLEAN")


def _dat_verdict(dump: Dump, match: Match) -> Tuple[str, bool]:
    data_tracks = [track for track in dump.tracks if track.kind == "data"]
    if dump.zero_filled_sectors:
        return "NOT VERIFIED - dump contains zero-filled sectors", False
    if match.full_match:
        return "FULL TRACK MATCH", True
    if match.all_data_match:
        return "DATA TRACKS MATCH (audio is not an exact Redump match)", True
    if match.exact_data_tracks and match.represented_data_tracks < len(data_tracks):
        return "IDENTIFIED BY DATA TRACK (catalog does not list every data track)", True
    return "PARTIAL MATCH ONLY", False


def print_dat_report(dump: Dump, matches: List[Match], label: str, kind: str) -> bool:
    print("\nRedump comparison")
    print(f"  Catalog: {label}")
    print(f"  Format: {kind}")
    incompatible = [track for track in dump.tracks
                    if track.kind == "data" and track.sector_size != 2352]
    if incompatible:
        print("  Verdict: INCOMPATIBLE DATA-TRACK FORMAT")
        print("  Redump track hashes use 2352-byte raw BIN data tracks. In DreamShell,")
        print("  enable 'Use bin tracks' and rip again for direct comparison.")
        return False
    if not matches:
        print("  Verdict: NO DATA-TRACK HASH MATCH")
        print("  Check that the DAT contains the same region/revision and is current.")
        return False

    best = matches[0]
    verdict, accepted = _dat_verdict(dump, best)
    print(f"  Candidate: {best.game.name}")
    print(f"  Verdict: {verdict}")
    print()
    for track in dump.tracks:
        print(f"    Track {track.number:02d} ({track.kind}): {best.statuses[track.number]}")

    data_bytes = sum(track.payload_size for track in dump.tracks if track.kind == "data")
    matched_data_bytes = sum(
        track.payload_size for track in dump.tracks
        if track.kind == "data" and best.statuses.get(track.number) == "MATCH"
    )
    if data_bytes:
        percentage = 100.0 * matched_data_bytes / data_bytes
        print(
            f"  Matched data payload: {_format_bytes(matched_data_bytes)} / "
            f"{_format_bytes(data_bytes)} ({percentage:.2f}%)"
        )
    same_rank = [candidate.game.name for candidate in matches[1:]
                 if candidate.rank == best.rank][:4]
    if same_rank:
        print("  Other equally ranked entries: " + "; ".join(same_rank))
    if not best.full_match:
        print("  Note: this is not proof of a Redump-grade physical-disc capture.")
    return accepted


def _first_difference(left: Track, right: Track) -> Optional[Tuple[int, int, int]]:
    compared = 0
    with left.path.open("rb") as left_stream, right.path.open("rb") as right_stream:
        left_stream.seek(left.file_offset)
        right_stream.seek(right.file_offset)
        while True:
            left_chunk = left_stream.read(HASH_CHUNK)
            right_chunk = right_stream.read(HASH_CHUNK)
            if left_chunk == right_chunk:
                if not left_chunk:
                    return None
                compared += len(left_chunk)
                continue
            limit = min(len(left_chunk), len(right_chunk))
            for index in range(limit):
                if left_chunk[index] != right_chunk[index]:
                    offset = compared + index
                    return offset, offset // left.sector_size, offset % left.sector_size
            offset = compared + limit
            return offset, offset // left.sector_size, offset % left.sector_size


def compare_dumps(left: Dump, right: Dump) -> bool:
    print("\nIndependent dump comparison")
    right_by_number = {track.number: track for track in right.tracks}
    all_match = True
    for left_track in left.tracks:
        right_track = right_by_number.get(left_track.number)
        if right_track is None:
            print(f"  Track {left_track.number:02d}: missing from second dump")
            all_match = False
            continue
        if (left_track.control, left_track.sector_size) != (
                right_track.control, right_track.sector_size):
            print(f"  Track {left_track.number:02d}: format differs")
            all_match = False
        elif left_track.sha1 == right_track.sha1 and left_track.payload_size == right_track.payload_size:
            print(f"  Track {left_track.number:02d}: MATCH")
        else:
            all_match = False
            difference = _first_difference(left_track, right_track)
            detail = "content differs"
            if difference:
                offset, sector, within = difference
                detail += f"; first difference at byte {offset} (sector {sector}, byte {within})"
            print(f"  Track {left_track.number:02d}: {detail}")

    extra = sorted(set(right_by_number) - {track.number for track in left.tracks})
    for number in extra:
        print(f"  Track {number:02d}: exists only in second dump")
        all_match = False
    if all_match:
        print("  Verdict: INDEPENDENT DUMPS MATCH")
    else:
        print("  Verdict: DUMPS DIFFER")
    return all_match


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Check a DreamShell GDI dump and compare its tracks with a Redump DAT.",
        epilog=(
            "Examples:\n"
            "  verify_gd_dump.py /media/sd/My_Game --dat 'Sega - Dreamcast.dat'\n"
            "  verify_gd_dump.py first/My_Game --compare second/My_Game"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("dump", help="Dump folder or .gdi file")
    parser.add_argument("--dat", metavar="FILE",
                        help="Redump XML/ClrMamePro DAT, optionally .zip or .gz")
    parser.add_argument("--compare", metavar="DUMP",
                        help="Compare with another DreamShell dump folder or .gdi")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    if not args.dat and not args.compare:
        print("No --dat or --compare supplied; checking DreamShell integrity metadata only.")
    try:
        dump = inspect_dump(args.dump, announce=True)
        _print_dump(dump)
        success = dump.clean

        if args.compare:
            other = inspect_dump(args.compare, announce=True)
            _print_dump(other, "Second DreamShell dump")
            success = compare_dumps(dump, other) and success and other.clean

        if args.dat:
            print(f"\nReading catalog and finding data-track matches ...", flush=True)
            matches, label, kind = find_dat_matches(dump, args.dat)
            success = print_dat_report(dump, matches, label, kind) and success

        print()
        if success:
            print("PASS")
            return 0
        print("FAIL - review the verdict and integrity messages above")
        return 1
    except VerificationError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nCancelled.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
