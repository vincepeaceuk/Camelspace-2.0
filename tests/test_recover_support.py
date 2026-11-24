"""Recovery acceptance tests; no old installer or plug-in code is executed."""
from pathlib import Path
import importlib.util
import os
import stat
import struct
import tempfile
import unittest

MODULE = Path(__file__).parents[1] / "tools" / "recover_support.py"
spec = importlib.util.spec_from_file_location("recover_support", MODULE)
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)


def odc_entry(name, data=b"", mode=stat.S_IFREG | 0o644, nlink=1):
    encoded = name.encode() + b"\0"
    header = ("070707" + f"{0:06o}{0:06o}{mode:06o}{0:06o}{0:06o}"
              f"{nlink:06o}{0:06o}{0:011o}{len(encoded):06o}{len(data):011o}")
    return header.encode() + encoded + data


def archive(*entries):
    return b"".join(entries) + odc_entry("TRAILER!!!", mode=0)


def bank(names=("Test preset", ""), count=344):
    chunk = b"".join(struct.pack("<f", 1.0) + name.encode() + b"\0"
                     + struct.pack("<I", count) + struct.pack("<f", 0.5) * count
                     for name in names)
    return (b"CcnK" + struct.pack(">I", 152 + len(chunk)) + b"FBCh"
            + struct.pack(">IIII", 1, 0x43615331, 1, len(names)) + bytes(128)
            + struct.pack(">I", len(chunk)) + chunk)


class ArchiveSafetyTests(unittest.TestCase):
    def test_reads_odc_without_alignment_or_installer(self):
        entries = recovery.parse_odc(archive(
            odc_entry(".", mode=stat.S_IFDIR | 0o755),
            odc_entry("./Library/preset.fxb", b"abc")))
        self.assertEqual([e.name for e in entries], [".", "Library/preset.fxb"])
        self.assertEqual(entries[1].data, b"abc")

    def test_rejects_paths_that_escape_output(self):
        for path in ("../escape", "/absolute", "safe/../../escape", "a\\b"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                recovery.parse_odc(archive(odc_entry(path, b"x")))

    def test_rejects_links_devices_and_duplicate_paths(self):
        for mode in (stat.S_IFLNK | 0o777, stat.S_IFIFO | 0o600, stat.S_IFCHR):
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                recovery.parse_odc(archive(odc_entry("unsafe", b"target", mode)))
        with self.assertRaises(ValueError):
            recovery.parse_odc(archive(odc_entry("a"), odc_entry("./a")))
        with self.assertRaises(ValueError):
            recovery.parse_odc(archive(odc_entry("hardlink", nlink=2)))

    def test_rejects_truncation_missing_trailer_and_oversized_member(self):
        valid = archive(odc_entry("a", b"abc"))
        for data in (valid[:20], valid[:79], odc_entry("a", b"abc")):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                recovery.parse_odc(data)
        with self.assertRaises(ValueError):
            recovery.parse_odc(valid, max_file_bytes=2)

    def test_refuses_existing_destination_and_leaves_source_unchanged(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / "Archive.pax"
            original = archive(odc_entry("a", b"contents"))
            source.write_bytes(original)
            target = root / "recovered"
            report = recovery.recover(source, target)
            self.assertEqual((target / "a").read_bytes(), b"contents")
            self.assertEqual(report["regular_file_count"], 1)
            self.assertEqual(source.read_bytes(), original)
            with self.assertRaises(FileExistsError):
                recovery.recover(source, target)


class BankTests(unittest.TestCase):
    def test_reads_chunk_header_and_all_named_and_blank_slots(self):
        info = recovery.inspect_fxb(bank())
        self.assertEqual(info["fx_id"], "CaS1")
        self.assertEqual(info["program_count"], 2)
        self.assertEqual(info["named_program_count"], 1)
        self.assertEqual([p["name"] for p in info["programs"]], ["Test preset", ""])
        self.assertEqual(info["programs"][0]["serialized_float_count"], 344)

    def test_rejects_bad_bank_and_inconsistent_lengths(self):
        for data in (b"", bank()[:-1], bank() + b"trailing"):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                recovery.inspect_fxb(data)

    def test_accepts_original_banks_legacy_whole_file_byte_size(self):
        data = bytearray(bank())
        struct.pack_into(">I", data, 4, len(data))
        info = recovery.inspect_fxb(data)
        self.assertEqual(info["byte_size_convention"], "whole-file (legacy CamelSpace)")

    def test_real_payload_has_preserved_banks_skins_and_sequences(self):
        fixture = os.environ.get("CAMELSPACE_SUPPORT_ARCHIVE")
        if not fixture:
            self.skipTest("Set CAMELSPACE_SUPPORT_ARCHIVE to test your original payload")
        source = Path(fixture).expanduser()
        self.assertTrue(source.is_file(), "CAMELSPACE_SUPPORT_ARCHIVE must name an existing archive")
        entries = recovery.parse_odc(source.read_bytes())
        files = [e for e in entries if not e.is_directory]
        self.assertEqual(len(entries), 88)
        self.assertEqual(len(files), 77)
        banks = {Path(e.name).name: recovery.inspect_fxb(e.data)
                 for e in files if e.name.endswith(".fxb")}
        self.assertEqual(len(banks), 10)
        self.assertTrue(all(b["program_count"] == 64 for b in banks.values()))
        self.assertEqual(sum(b["named_program_count"] for b in banks.values()), 540)
        self.assertEqual(banks["-Default.fxb"]["sha256"], banks["Rhythmic1.fxb"]["sha256"])
        self.assertEqual(sum(e.name.endswith(".csp") for e in files), 25)
        self.assertEqual(sum(e.name.endswith(".png") for e in files), 39)


if __name__ == "__main__":
    unittest.main()
