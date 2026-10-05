"""Synthetic sound-theme tests; optionally verify local real metadata, never audio."""
import hashlib
import importlib.machinery
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
import uuid
import wave

TOOL = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else (
    Path(__file__).resolve().parents[2] / "shell/panels/zacos9-soundthemes")
REAL_SOUNDS = Path(sys.argv[2]) if len(sys.argv) > 2 else None
loader = importlib.machinery.SourceFileLoader("soundthemes", str(TOOL))
spec = importlib.util.spec_from_loader(loader.name, loader)
sounds = importlib.util.module_from_spec(spec)
loader.exec_module(sounds)


def resource_file(items):
    body, names, references = bytearray(), bytearray(), bytearray()
    kinds = sorted({item[0] for item in items})
    table = bytearray(struct.pack(">H", len(kinds) - 1))
    for kind in kinds:
        group = [item for item in items if item[0] == kind]
        table.extend(struct.pack(">4sHH", kind, len(group) - 1,
                                 2 + 8 * len(kinds) + len(references)))
        for _, number, payload in group:
            offset = len(body)
            body.extend(struct.pack(">I", len(payload)) + payload)
            references.extend(struct.pack(">hh", number, -1) + b"\0" +
                              offset.to_bytes(3, "big") + b"\0" * 4)
    mapping = bytearray(28) + table + references + names
    struct.pack_into(">HH", mapping, 24, 28, 28 + len(table) + len(references))
    header = struct.pack(">IIII", 256, 256 + len(body), len(body), len(mapping))
    mapping[:16] = header
    return header + bytes(240) + body + mapping


def snd(samples=b"\0\x80\xff", extended=False, channels=1, version=1):
    header = bytearray(64 if extended else 22)
    struct.pack_into(">III", header, 0, 0, channels if extended else len(samples), 22050 << 16)
    if extended:
        header[20] = 255
        struct.pack_into(">I", header, 22, len(samples) // (channels * 2))
        struct.pack_into(">H", header, 48, 16)
    prefix = struct.pack(">HHHIH", 1, 1, 5, 128, 1) if version == 1 else (
        struct.pack(">HHH", 2, 0, 1))
    return prefix + struct.pack(">HHI", 0x8051, 0, len(prefix) + 8) + header + samples


def snd_list(entries):
    result = bytearray(struct.pack(">HH", 1, len(entries)))
    for code, number in entries:
        entry = bytearray(44)
        entry[:4] = code
        struct.pack_into(">HHHi", entry, 4, 2, 5, 0, number)
        result.extend(entry)
    return bytes(result)


def appledouble(resource):
    return (struct.pack(">II16sH", 0x00051607, 0x00020000, bytes(16), 1) +
            struct.pack(">III", 2, 38, len(resource)) + resource)


def macbinary(resource):
    header = bytearray(128)
    header[1:6] = b"\x04Test"
    struct.pack_into(">II", header, 83, 0, len(resource))
    return bytes(header) + resource


def wav_data(samples=b"\0\x80\xff", channels=1, width=1, rate=22050):
    out = io.BytesIO()
    with wave.open(out, "wb") as writer:
        writer.setnchannels(channels)
        writer.setsampwidth(width)
        writer.setframerate(rate)
        writer.writeframes(samples)
    return out.getvalue()


class SoundTests(unittest.TestCase):
    def setUp(self):
        self.workspace = Path.cwd() / (".soundthemes-test-" + uuid.uuid4().hex)
        self.workspace.mkdir(mode=0o700)
        self.source, self.cache = self.workspace / "source", self.workspace / "cache"
        self.source.mkdir()

    def tearDown(self):
        shutil.rmtree(self.workspace)

    def theme(self, events=None):
        folder = self.source / "Synthetic"
        folder.mkdir()
        (folder / "click.wav").write_bytes(wav_data())
        (folder / "theme.json").write_text(json.dumps({
            "version": 1, "name": "Synthetic Theme",
            "events": events or {"button-click": "click.wav"}}))
        return folder

    def test_folder_cli_and_cache_lifetime(self):
        folder = self.theme()
        before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.iterdir()}
        output = subprocess.check_output([sys.executable, str(TOOL),
                                          str(self.source), str(self.cache)], text=True)
        catalog = json.loads(output)
        self.assertEqual(set(catalog), {"themes", "errors"})
        self.assertEqual(catalog["errors"], [])
        theme = catalog["themes"][0]
        self.assertEqual(theme["name"], "Synthetic Theme")
        self.assertLessEqual(len(theme["id"]), 128)
        cached = Path(theme["events"]["button-click"])
        self.assertTrue(cached.is_absolute())
        self.assertEqual(cached.parent, self.cache)
        self.assertEqual(cached.read_bytes(), wav_data())
        self.assertEqual(before, {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in folder.iterdir()})
        again = sounds.scan(self.source, self.cache)
        self.assertEqual(catalog, again)
        (folder / "click.wav").write_bytes(wav_data(b"\xff\x80\0"))
        changed = sounds.scan(self.source, self.cache)["themes"][0]
        self.assertEqual(theme["id"], changed["id"])
        self.assertNotEqual(theme["events"], changed["events"])
        shutil.rmtree(folder)
        self.assertEqual(sounds.scan(self.source, self.cache)["themes"], [])
        self.assertEqual(cached.read_bytes(), wav_data())

    def test_all_folder_events_and_nested_pcm(self):
        folder = self.theme({event: "nested/stereo.wav" for event in sounds.EVENTS})
        (folder / "nested").mkdir()
        (folder / "nested/stereo.wav").write_bytes(wav_data(b"\0\x80\xff\x7f", 2, 2))
        result = sounds.scan(self.source, self.cache)
        self.assertEqual(result["errors"], [])
        self.assertEqual(set(result["themes"][0]["events"]), sounds.EVENTS)

    def test_pcm_unsigned_and_big_endian(self):
        for version in (1, 2):
            result = sounds.decode_sound(snd(version=version))
            with wave.open(io.BytesIO(result)) as reader:
                self.assertEqual(reader.readframes(3), b"\0\x80\xff")
                self.assertEqual(reader.getsampwidth(), 1)
        result = sounds.decode_sound(snd(b"\x80\0\x7f\xff\0\x01\xff\xff", True, 2))
        with wave.open(io.BytesIO(result)) as reader:
            self.assertEqual(reader.getnchannels(), 2)
            self.assertEqual(reader.getsampwidth(), 2)
            self.assertEqual(reader.readframes(2), b"\0\x80\xff\x7f\x01\0\xff\xff")

    def test_unsupported_sound_headers(self):
        for encoding in (254, 42):
            data = bytearray(snd())
            data[40] = encoding
            with self.assertRaisesRegex(ValueError, "compressed|unsupported"):
                sounds.decode_sound(bytes(data))
        for mutate in (
                lambda d: struct.pack_into(">H", d, 0, 3),
                lambda d: struct.pack_into(">H", d, 10, 2),
                lambda d: struct.pack_into(">H", d, 12, 0x51),
                lambda d: struct.pack_into(">I", d, 20, 1),
                lambda d: struct.pack_into(">I", d, 24, 0xffffffff)):
            data = bytearray(snd())
            mutate(data)
            with self.assertRaises(ValueError):
                sounds.decode_sound(bytes(data))
        with self.assertRaises(ValueError):
            sounds.decode_sound(snd()[:-1])

    def test_sdid_and_sound_list_not_resource_id_guessing(self):
        records = {(b"snd ", 42): snd(),
                   (b"sdid", 42): b"btnp#Synthetic#ui#Button press",
                   (b"snd ", 548): snd(),
                   (b"sdid", 548): b"aopn#Synthetic#wind#Alert open",
                   (b"snd ", 700): snd(),
                   (b"snd#", 1000): snd_list([(b"wopn", 548), (b"mnus", 700)])}
        self.assertEqual(sounds.classic_mapping(records),
                         {"button-click": 42, "menu-command": 700})
        self.assertEqual(sounds.classic_mapping({(b"snd ", 304): snd()}), {})
        self.assertNotIn("trash-move", sounds.CLASSIC_EVENTS.values())
        records[b"sdid", 42] = b"malformed"
        self.assertNotIn("button-click", sounds.classic_mapping(records))
        records[b"sdid", 42] = b"btnp#Original"
        self.assertEqual(sounds.classic_mapping(records)["button-click"], 42)

    def test_metadata_bounds_and_duplicate_events(self):
        for payload in (snd_list([])[:3], b"\0\2\0\0", b"\0\1\xff\xff"):
            with self.assertRaises(ValueError):
                sounds.classic_mapping({(b"snd#", 1): payload})
        records = {(b"snd ", 1): snd(), (b"snd ", 2): snd(),
                   (b"sdid", 1): b"btnp#one#ui#Press",
                   (b"sdid", 2): b"btnp#two#ui#Press"}
        self.assertEqual(sounds.classic_mapping(records), {})
        records[b"snd#", 1000] = snd_list([(b"btnp", 2)])
        self.assertEqual(sounds.classic_mapping(records), {"button-click": 2})

    def test_classic_window_actions_and_drag_pcm(self):
        entries = [(b"wcol", 10), (b"wexp", 11), (b"wmov", 12)]
        records = {(b"snd ", number): snd() for _, number in entries}
        records[b"snd#", 1000] = snd_list(entries)
        expected = {"window-collapse": 10, "window-expand": 11, "window-drag": 12}
        self.assertEqual(sounds.classic_mapping(records), expected)
        self.assertNotIn("window-drag-end", sounds.CLASSIC_EVENTS.values())
        items = [(b"snd ", number, snd()) for _, number in entries]
        items += [(b"sdid", number, code + b"#Original#wind#Window action")
                  for code, number in entries]
        (self.source / "Window actions").write_bytes(resource_file(items))
        result = sounds.scan(self.source, self.cache)
        self.assertEqual(result["errors"], [])
        self.assertEqual(set(result["themes"][0]["events"]), set(expected))
        for path in result["themes"][0]["events"].values():
            with wave.open(path) as reader:
                self.assertEqual(reader.readframes(3), b"\0\x80\xff")

    def test_containers_and_silent_missing_events(self):
        resource = resource_file([(b"snd ", 42, snd()),
                                  (b"sdid", 42, b"chkp#Original#ui#Checkbox press")])
        for name, data in (("Raw", resource), ("._Double", appledouble(resource)),
                           ("Binary.bin", macbinary(resource))):
            (self.source / name).write_bytes(data)
        (self.source / "Double").write_bytes(b"ignored data fork")
        result = sounds.scan(self.source, self.cache)
        self.assertEqual(result["errors"], [])
        self.assertEqual(len(result["themes"]), 3)
        self.assertEqual(len({t["id"] for t in result["themes"]}), 3)
        for theme in result["themes"]:
            self.assertEqual(set(theme["events"]), {"checkbox-toggle"})

    def test_per_event_compression_error_keeps_pcm(self):
        compressed = bytearray(snd())
        compressed[40] = 254
        resource = resource_file([(b"snd ", 1, snd()), (b"snd ", 2, bytes(compressed)),
                                  (b"sdid", 1, b"btnp#One#ui#Press"),
                                  (b"sdid", 2, b"chkp#Two#ui#Press")])
        (self.source / "Partial").write_bytes(resource)
        result = sounds.scan(self.source, self.cache)
        self.assertEqual(set(result["themes"][0]["events"]), {"button-click"})
        self.assertIn("compressed", result["errors"][0])

    def test_group_folder_classic_and_sidecar_only(self):
        resource = resource_file([(b"snd ", 42, snd()),
                                  (b"sdid", 42, b"btnp#Original#ui#Button press")])
        (self.source / "Same").write_bytes(resource)
        group = self.source / "Sounds"
        group.mkdir()
        (group / "._Same").write_bytes(appledouble(resource))
        (group / "Raw").write_bytes(resource)
        result = sounds.scan(self.source, self.cache)
        self.assertEqual(result["errors"], [])
        self.assertEqual(len(result["themes"]), 3)
        self.assertEqual(len({theme["id"] for theme in result["themes"]}), 3)
        self.assertEqual([theme["name"] for theme in result["themes"]].count("Same"), 2)
        ids = {theme["id"] for theme in result["themes"]}
        (group / "Same").write_bytes(b"unused data fork")
        self.assertEqual(ids, {theme["id"] for theme in
                              sounds.scan(self.source, self.cache)["themes"]})
        (group / "Deeper").mkdir()
        (group / "Deeper/Hidden").write_bytes(resource)
        result = sounds.scan(self.source, self.cache)
        self.assertEqual(len(result["themes"]), 3)
        self.assertTrue(any("beyond one level" in e for e in result["errors"]))

    def test_group_folder_symlink_and_scan_limit(self):
        group = self.source / "Sounds"
        group.mkdir()
        (group / "Link").symlink_to(self.source)
        self.assertTrue(any("symlinks" in e for e in
                            sounds.scan(self.source, self.cache)["errors"]))
        (group / "Link").unlink()
        (group / "Ignored").write_bytes(b"not a resource")
        previous = sounds.MAX_SCAN_ENTRIES
        try:
            sounds.MAX_SCAN_ENTRIES = 1
            self.assertTrue(any("scan exceeds" in e for e in
                                sounds.scan(self.source, self.cache)["errors"]))
        finally:
            sounds.MAX_SCAN_ENTRIES = previous

    def test_unsafe_paths_and_symlinks(self):
        folder = self.theme()
        manifest = folder / "theme.json"
        for path in ("../escape.wav", "/etc/test.wav", "nested/../click.wav",
                     "./click.wav", "nested//click.wav", "bad\\click.wav", "click.mp3"):
            manifest.write_text(json.dumps({"version": 1, "name": "Bad",
                                           "events": {"button-click": path}}))
            result = sounds.scan(self.source, self.cache)
            self.assertEqual(result["themes"], [])
            self.assertTrue(result["errors"], path)
        manifest.write_text(json.dumps({"version": 1, "name": "Bad",
                                       "events": {"button-click": "link.wav"}}))
        (folder / "link.wav").symlink_to(folder / "click.wav")
        self.assertIn("symlinks", sounds.scan(self.source, self.cache)["errors"][0])
        manifest.unlink()
        manifest.symlink_to(folder / "click.wav")
        self.assertIn("symlinks", sounds.scan(self.source, self.cache)["errors"][0])

    def test_invalid_manifests(self):
        folder = self.theme()
        for theme in ([], {"version": True}, {"version": 2},
                      {"version": 1, "name": "", "events": {}},
                      {"version": 1, "name": "x", "events": {"unknown": "click.wav"}},
                      {"version": 1, "name": "x", "events": []}):
            (folder / "theme.json").write_text(json.dumps(theme))
            self.assertEqual(sounds.scan(self.source, self.cache)["themes"], [])

    def test_wav_and_source_limits(self):
        for data in (b"not WAV", wav_data(b"\0" * 22050 * 11), wav_data(rate=4000),
                     wav_data(width=3), wav_data(channels=3), wav_data()[:-1]):
            with self.assertRaises(ValueError):
                sounds.decode_wav(data)
        folder = self.theme()
        with (folder / "click.wav").open("wb") as writer:
            writer.truncate(sounds.MAX_SOURCE + 1)
        self.assertEqual(sounds.scan(self.source, self.cache)["themes"], [])
        with (self.source / "Huge").open("wb") as writer:
            writer.truncate(sounds.MAX_SOURCE + 1)
        self.assertTrue(any("limit" in error for error in
                            sounds.scan(self.source, self.cache)["errors"]))

    def test_cache_protection(self):
        self.theme()
        result = sounds.scan(self.source, self.source / "cache")
        self.assertIn("outside", result["errors"][0])
        self.assertFalse((self.source / "cache").exists())
        result = sounds.scan(self.source, self.cache)
        cached = Path(result["themes"][0]["events"]["button-click"])
        cached.unlink()
        cached.symlink_to(self.source / "Synthetic/click.wav")
        self.assertIn("symlinks", sounds.scan(self.source, self.cache)["errors"][0])
        cached.unlink()
        cached.write_bytes(b"corruption")
        self.assertIn("modified", sounds.scan(self.source, self.cache)["errors"][0])
        cached.unlink()
        self.cache.chmod(0o755)
        self.assertEqual(sounds.scan(self.source, self.cache)["errors"], [])
        self.assertEqual(self.cache.stat().st_mode & 0o777, 0o700)

    def test_resource_bounds(self):
        resource = resource_file([(b"snd ", 1, snd())])
        for data in (b"", resource[:15], resource[:-1]):
            with self.assertRaises(ValueError):
                sounds.resource_records(data)
        damaged = bytearray(resource)
        struct.pack_into(">I", damaged, 4, 0xffffffff)
        with self.assertRaises(ValueError):
            sounds.resource_records(bytes(damaged))
        compressed = bytearray(resource)
        map_start = struct.unpack_from(">I", compressed, 4)[0]
        compressed[map_start + 42] = 1
        with self.assertRaisesRegex(ValueError, "compressed resource"):
            sounds.resource_records(bytes(compressed))

    def test_catalog_limits(self):
        self.theme()
        previous = sounds.MAX_THEMES
        try:
            sounds.MAX_THEMES = 0
            result = sounds.scan(self.source, self.cache)
            self.assertEqual(result["themes"], [])
            self.assertIn("catalog exceeds", result["errors"][0])
        finally:
            sounds.MAX_THEMES = previous

    @unittest.skipUnless(REAL_SOUNDS, "pass extracted Sounds path for local metadata verification")
    def test_real_fixture_metadata_only(self):
        # Do not decode, play, cache, or copy the supplied sample audio.
        records = sounds.resource_records(sounds.containers(
            (REAL_SOUNDS / "._Clickz 1.0").read_bytes(), "")[1])
        self.assertEqual(records[b"sdid", 304], b"btnp#B Press#ui#Button press")
        self.assertEqual(records[b"sdid", 515], b"mnuo#Menu1#menu#Menu open")
        self.assertEqual(sounds.classic_mapping(records), {
            "button-click": 304, "menu-open": 515, "menu-command": 518,
            "window-open": 548, "window-close": 550, "trash-empty": 290})
        payload = records[b"snd#", 1000]
        self.assertEqual(struct.unpack_from(">HH", payload), (1, 116))
        self.assertEqual(len(payload), 4 + 116 * 44)
        self.assertEqual(payload[4:8], b"mnuo")
        self.assertEqual(struct.unpack_from(">i", payload, 14)[0], 515)
        clinton = sounds.resource_records(sounds.containers(
            (REAL_SOUNDS / "._Clinton SS").read_bytes(), "")[1])
        self.assertTrue(clinton[b"sdid", 548].startswith(b"aopn#"))
        self.assertNotIn("window-open", sounds.classic_mapping(clinton))
        platinum = sounds.resource_records(sounds.containers(
            (REAL_SOUNDS / "._Platinum Sounds").read_bytes(), "")[1])
        self.assertEqual(sounds.classic_mapping(platinum)["checkbox-toggle"], 330)
        count = 0
        for path in REAL_SOUNDS.glob("._*"):
            records = sounds.resource_records(sounds.containers(path.read_bytes(), "")[1])
            self.assertLessEqual(set(sounds.classic_mapping(records)), sounds.EVENTS)
            count += 1
        self.assertEqual(count, 75)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
