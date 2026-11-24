#!/usr/bin/env python3
"""Read CamelSpace's old odc-cpio payload without running its installer.

Only standard-library code is used. The package's scripts are never loaded or
executed. This recovers source bytes; it does not convert or modernise the DSP.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import struct

MAX_ARCHIVE_BYTES = 128 * 1024 * 1024
MAX_FILE_BYTES = 64 * 1024 * 1024
MAX_MEMBERS = 8192


class Entry:
    __slots__ = ("name", "data", "is_directory")

    def __init__(self, name, data, is_directory):
        self.name = name
        self.data = data
        self.is_directory = is_directory


def _safe_name(raw):
    if not raw.endswith(b"\0") or b"\0" in raw[:-1]:
        raise ValueError("Invalid cpio filename terminator")
    try:
        name = raw[:-1].decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ValueError("Invalid UTF-8 cpio filename") from exc
    path = PurePosixPath(name)
    if (not name or path.is_absolute() or ".." in path.parts
            or "\\" in name or any(ord(c) < 32 for c in name)):
        raise ValueError(f"Unsafe cpio filename: {name!r}")
    return path.as_posix()


def parse_odc(data, *, max_file_bytes=MAX_FILE_BYTES):
    """Validate an old ASCII cpio payload fully before materialising anything."""
    if len(data) > MAX_ARCHIVE_BYTES:
        raise ValueError("Archive exceeds recovery size limit")
    entries, seen = [], set()
    position = 0
    while position + 76 <= len(data):
        header = data[position:position + 76]
        if header[:6] != b"070707":
            raise ValueError(f"Expected odc-cpio header at byte {position}")
        if any(c not in b"01234567" for c in header[6:]):
            raise ValueError("Invalid non-octal cpio header field")
        try:
            fields = [int(header[a:b], 8) for a, b in
                      ((6, 12), (12, 18), (18, 24), (24, 30), (30, 36),
                       (36, 42), (42, 48), (48, 59), (59, 65), (65, 76))]
        except ValueError as exc:
            raise ValueError("Invalid octal cpio header field") from exc
        mode, nlink, name_size, file_size = fields[2], fields[5], fields[8], fields[9]
        if not 1 <= name_size <= 4096 or file_size > max_file_bytes:
            raise ValueError("Archive member exceeds recovery limits")
        name_start = position + 76
        file_start = name_start + name_size
        end = file_start + file_size
        if end > len(data):
            raise ValueError("Truncated archive member")
        name = _safe_name(data[name_start:file_start])
        if name == "TRAILER!!!":
            if file_size or any(data[end:]):
                raise ValueError("Invalid archive trailer or nonzero trailing bytes")
            return entries
        if name in seen:
            raise ValueError(f"Duplicate archive path: {name}")
        seen.add(name)
        kind = stat.S_IFMT(mode)
        if kind not in (stat.S_IFREG, stat.S_IFDIR):
            raise ValueError(f"Unsupported member type at {name}; no links or devices allowed")
        if (kind == stat.S_IFDIR and file_size) or (kind == stat.S_IFREG and nlink != 1):
            raise ValueError(f"Invalid directory data or hardlink member at {name}")
        if name == "." and kind != stat.S_IFDIR:
            raise ValueError("Archive root must be a directory")
        entries.append(Entry(name, data[file_start:end], kind == stat.S_IFDIR))
        if len(entries) > MAX_MEMBERS:
            raise ValueError("Archive exceeds member count limit")
        position = end
    raise ValueError("Missing or truncated cpio archive trailer")


def inspect_fxb(data):
    """Inventory the observed CamelSpace FBCh layout, without loading code.

    This is a byte-validated format inference from the supplied ten banks:
    big-endian VST bank header, then records of a little-endian version float,
    NUL-terminated name, little-endian float count and that many 32-bit values.
    Serialized float counts are not a claim about exposed host parameters.
    """
    if len(data) < 160 or data[:4] != b"CcnK" or data[8:12] != b"FBCh":
        raise ValueError("Expected a VST chunk bank (CcnK/FBCh)")
    byte_size, = struct.unpack_from(">I", data, 4)
    version, fx_id, fx_version, program_count = struct.unpack_from(">IIII", data, 12)
    chunk_size, = struct.unpack_from(">I", data, 156)
    if byte_size not in (len(data) - 8, len(data)) or chunk_size + 160 != len(data):
        raise ValueError("FXB bank/chunk byte lengths do not match the file")
    if version != 1 or fx_id != 0x43615331 or not 1 <= program_count <= 4096:
        raise ValueError("Unsupported bank version, plug-in ID or program count")
    position, programs = 160, []
    for index in range(program_count):
        start = position
        if position + 4 > len(data):
            raise ValueError("Truncated bank program version")
        program_version, = struct.unpack_from("<f", data, position)
        if program_version != 1.0:
            raise ValueError("Unsupported CamelSpace program record version")
        name_start = position + 4
        name_end = data.find(b"\0", name_start, min(name_start + 1024, len(data)))
        if name_end < 0 or name_end + 5 > len(data):
            raise ValueError("Truncated bank program name/count")
        try:
            name = data[name_start:name_end].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ValueError("Invalid bank program name encoding") from exc
        count, = struct.unpack_from("<I", data, name_end + 1)
        end = name_end + 5 + 4 * count
        if not 1 <= count <= 4096 or end > len(data):
            raise ValueError("Invalid or truncated program float payload")
        programs.append({
            "index": index, "name": name, "record_version": program_version,
            "serialized_float_count": count, "file_offset": start,
            "byte_length": end - start,
            "sha256": hashlib.sha256(data[start:end]).hexdigest(),
        })
        position = end
    if position != len(data):
        raise ValueError("Unparsed bytes remain after the declared bank programs")
    return {
        "sha256": hashlib.sha256(data).hexdigest(), "format": "FBCh",
        "declared_byte_size": byte_size,
        "byte_size_convention": ("whole-file (legacy CamelSpace)"
                                 if byte_size == len(data) else "excluding-eight-byte-header"),
        "fx_id": fx_id.to_bytes(4, "big").decode("ascii"),
        "format_version": version, "fx_version": fx_version,
        "chunk_offset": 160, "chunk_byte_length": chunk_size,
        "program_count": program_count,
        "named_program_count": sum(bool(p["name"]) for p in programs),
        "programs": programs,
        "interpretation": "Observed CamelSpace record structure, not original source documentation",
    }


def _directory_fd(root_fd, parts):
    """Open/create beneath the fresh root without traversing symlinks."""
    fd = os.dup(root_fd)
    try:
        for part in parts:
            try:
                os.mkdir(part, mode=0o755, dir_fd=fd)
            except FileExistsError:
                pass
            next_fd = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            os.close(fd)
            fd = next_fd
        return fd
    except BaseException:
        os.close(fd)
        raise


def _write_file(root_fd, name, data):
    parts = PurePosixPath(name).parts
    parent_fd = _directory_fd(root_fd, parts[:-1])
    try:
        fd = os.open(parts[-1], os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o644, dir_fd=parent_fd)
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
    finally:
        os.close(parent_fd)


def recover(archive_path, destination):
    """Recover to a new directory, refusing replacement of existing output."""
    archive_path, destination = Path(archive_path), Path(destination)
    if archive_path.stat().st_size > MAX_ARCHIVE_BYTES:
        raise ValueError("Archive exceeds recovery size limit")
    with archive_path.open("rb") as stream:
        data = stream.read(MAX_ARCHIVE_BYTES + 1)
    entries = parse_odc(data)
    files = [e for e in entries if not e.is_directory]
    banks = []
    for entry in files:
        if entry.name.lower().endswith(".fxb"):
            banks.append({"path": entry.name, **inspect_fxb(entry.data)})
    inventory = [{"path": e.name, "size": len(e.data),
                  "sha256": hashlib.sha256(e.data).hexdigest()} for e in files]
    duplicates = defaultdict(list)
    for item in inventory:
        duplicates[item["sha256"]].append(item["path"])
    report = {
        "source_archive": str(archive_path.resolve()),
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "archive_format": "ASCII odc cpio (070707)",
        "archive_member_count": len(entries), "regular_file_count": len(files),
        "directory_count": len(entries) - len(files),
        "extensions": dict(sorted(Counter(PurePosixPath(e.name).suffix for e in files).items())),
        "duplicate_file_groups": [v for v in duplicates.values() if len(v) > 1],
        "files": inventory, "banks": banks,
        "limits": {"max_archive_bytes": MAX_ARCHIVE_BYTES,
                   "max_file_bytes": MAX_FILE_BYTES, "max_members": MAX_MEMBERS},
        "scope": "Payload bytes and static preset inventory only; no installer or plug-in execution",
    }
    # Validate everything before creating output. A fresh root plus dir-fd and
    # O_NOFOLLOW operations prevents source paths from writing outside it.
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.mkdir(exist_ok=False)
    root_fd = os.open(destination, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        for entry in entries:
            if entry.name == ".":
                continue
            if entry.is_directory:
                fd = _directory_fd(root_fd, PurePosixPath(entry.name).parts)
                os.close(fd)
            else:
                _write_file(root_fd, entry.name, entry.data)
        _write_file(root_fd, "recovery-inventory.json",
                    (json.dumps(report, indent=2, ensure_ascii=False) + "\n").encode())
        rows = ["# Recovered CamelSpace support files", "",
                f"Recovered {len(files)} files from {len(entries)} archive members.",
                "Original file bytes and SHA-256 digests are in recovery-inventory.json.",
                "No installer or plug-in code was executed.", "",
                "| Bank file | Slots | Named slots |", "| --- | ---: | ---: |"]
        rows.extend(f"| {PurePosixPath(b['path']).name} | {b['program_count']} | {b['named_program_count']} |"
                    for b in banks)
        rows.extend(["", "Blank slots and duplicate banks are preserved.",
                     "This inventory does not prove plug-in audio or GUI compatibility.", ""])
        _write_file(root_fd, "RECOVERY.md", "\n".join(rows).encode())
    finally:
        os.close(root_fd)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help="CS Support Files.pkg/Contents/Archive.pax")
    parser.add_argument("destination", type=Path, help="New output directory (must not exist)")
    args = parser.parse_args()
    report = recover(args.archive, args.destination)
    print(json.dumps({"destination": str(args.destination.resolve()),
                      "files": report["regular_file_count"],
                      "extensions": report["extensions"],
                      "banks": len(report["banks"]),
                      "named_slots_including_duplicate_banks":
                          sum(b["named_program_count"] for b in report["banks"])}, indent=2))


if __name__ == "__main__":
    main()
