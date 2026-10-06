"""Mac image preparation and mount-command tests; never touch real disks."""
import contextlib
import importlib.machinery
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import stat
import struct
import sys
import tempfile
import shutil
import subprocess
from types import SimpleNamespace
import unittest
from unittest.mock import patch


def load(name, path):
    loader = importlib.machinery.SourceFileLoader(name, path)
    spec = importlib.util.spec_from_loader(name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


images = load("mac_disks", sys.argv.pop(1))
helper = load("mac_mount", sys.argv.pop(1))


class ImageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "output"
        self.output.mkdir()

    def test_raw_image_is_not_modified_or_copied(self):
        image = self.root / "HD.img"
        image.write_bytes(bytes(4096))
        self.assertEqual(images.prepare_image(image, self.output), image)
        self.assertEqual(image.read_bytes(), bytes(4096))
        self.assertEqual(list(self.output.iterdir()), [])

    def test_diskcopy_strips_header_and_tags(self):
        image = self.root / "Floppy.image"
        header = bytearray(84)
        header[0] = 4
        header[1:5] = b"Disk"
        struct.pack_into(">II", header, 64, 4096, 12)
        header[82:84] = b"\x01\x00"
        data = bytes(4096)
        original = bytes(header) + data + b"tag" * 4
        image.write_bytes(original)
        prepared = images.prepare_image(image, self.output)
        self.assertEqual(prepared.read_bytes(), data)
        self.assertEqual(image.read_bytes(), original)
        image.write_bytes(original[:-1])
        with self.assertRaisesRegex(ValueError, "Disk Copy"):
            images.prepare_image(image, self.output)

    def bundle(self):
        bundle = self.root / "HD.sparsebundle"
        bundle.mkdir()
        (bundle / "bands").mkdir()
        (bundle / "Info.plist").write_bytes(plistlib.dumps({"size": 8192, "band-size": 4096}))
        return bundle

    def test_sparse_bundle_preserves_holes_and_source(self):
        bundle = self.bundle()
        (bundle / "bands/1").write_bytes(b"data")
        prepared = images.prepare_image(bundle, self.output)
        self.assertEqual(prepared.stat().st_size, 8192)
        self.assertEqual(prepared.read_bytes(), bytes(4096) + b"data" + bytes(4092))
        self.assertEqual((bundle / "bands/1").read_bytes(), b"data")

    def test_sparse_bundle_rejects_unsafe_and_out_of_bounds_bands(self):
        for name, data in (("not-a-band", b"a"), ("2", b"a"), ("0", bytes(4097))):
            with self.subTest(name=name):
                bundle = self.bundle()
                (bundle / "bands" / name).write_bytes(data)
                with self.assertRaisesRegex(ValueError, "band"):
                    images.prepare_image(bundle, self.output)
                import shutil
                shutil.rmtree(bundle)
        bundle = self.bundle()
        (bundle / "bands/0").symlink_to("/etc/passwd")
        with self.assertRaisesRegex(ValueError, "band"):
            images.prepare_image(bundle, self.output)

    def test_sparse_image_backend_and_sparse_copy(self):
        image = self.root / "HD.sparseimage"
        image.write_bytes(b"sprs" + bytes(4096))
        data = bytes(1024**2) + b"data"

        @contextlib.contextmanager
        def reader(source):
            self.assertEqual(source, image)
            yield len(data), lambda offset, size: data[offset:offset + size]

        with patch.object(images, "modi_image", reader):
            prepared = images.prepare_image(image, self.output)
            self.assertEqual(prepared.read_bytes(), data)

    def test_real_libmodi_sparse_image_reader(self):
        try:
            images.ctypes.CDLL("libmodi.so.1")
        except OSError:
            self.skipTest("libmodi1 is not installed")
        header = bytearray(4096)
        struct.pack_into(">4sIIII", header, 0, b"sprs", 3, 8, 1, 16)
        struct.pack_into(">I", header, 64, 1)
        band = b"data" + bytes(4092)
        original = bytes(header) + band
        image = self.root / "HD.sparseimage"
        image.write_bytes(original)
        prepared = images.prepare_image(image, self.output)
        self.assertEqual(prepared.read_bytes(), band + bytes(4096))
        self.assertEqual(image.read_bytes(), original)

    def test_dmg_converter_argv_and_failure(self):
        image = self.root / "a file.dmg"
        trailer = bytearray(512)
        trailer[:4] = b"koly"
        struct.pack_into(">Q", trailer, 492, 8)
        image.write_bytes(bytes(1024) + trailer)
        with patch.object(images, "run") as run:
            self.assertEqual(images.prepare_image(image, self.output), self.output / "image.raw")
            run.assert_called_once_with(["dmg2img", str(image), str(self.output / "image.raw")])
        with patch.object(images, "run", side_effect=ValueError("bad dmg")):
            with self.assertRaisesRegex(ValueError, "bad dmg"):
                images.prepare_image(image, self.output)

    def test_scan_all_mac_types_not_other_filesystems(self):
        records = {
            str(i): {"org.freedesktop.UDisks2.Block": {
                "IdType": kind, "Device": list(f"/dev/sdz{i}\0".encode())}}
            for i, kind in enumerate(("hfs", "hfsplus", "apfs", "ext4"))
        }
        with patch.object(images, "managed_objects", return_value=records):
            self.assertEqual(images.scan(), ["/dev/sdz0", "/dev/sdz1", "/dev/sdz2"])
        records["0"]["org.freedesktop.UDisks2.Filesystem"] = {"MountPoints": ["/media/mounted"]}
        records["1"]["org.freedesktop.UDisks2.Block"]["HintIgnore"] = True
        with patch.object(images, "managed_objects", return_value=records):
            self.assertEqual(images.scan(), ["/dev/sdz2"])

    def test_loop_setup_passes_readonly_fd_and_option(self):
        from gi.repository import Gio, GLib
        image = self.root / "HD.img"
        image.write_bytes(bytes(4096))
        loop = "/org/freedesktop/UDisks2/block_devices/loop0"

        def call(*args):
            parameters, descriptors = args[4], args[8]
            handle, options = parameters.unpack()
            self.assertTrue(options["read-only"])
            fd = descriptors.get(handle)
            try:
                import fcntl
                self.assertEqual(fcntl.fcntl(fd, fcntl.F_GETFL) & os.O_ACCMODE, os.O_RDONLY)
            finally:
                os.close(fd)
            return GLib.Variant("(o)", (loop,)), None

        bus = SimpleNamespace(call_with_unix_fd_list_sync=call)
        with patch.object(Gio, "bus_get_sync", return_value=bus):
            self.assertEqual(images.loop_setup(image), loop)

    def test_image_without_mac_volume_deletes_loop_and_cache(self):
        image = self.root / "HD.img"
        image.write_bytes(bytes(4096))
        with patch.dict(os.environ, {"XDG_CACHE_HOME": str(self.root / "cache")}), \
                patch.object(images, "loop_setup", return_value="/org/freedesktop/UDisks2/block_devices/loop0"), \
                patch.object(images, "managed_objects", return_value={}), \
                patch.object(images, "block_call") as call:
            with self.assertRaisesRegex(ValueError, "No supported Mac"):
                images.mount(image)
            call.assert_called_once()
            self.assertEqual(list((self.root / "cache/zacos9/mac-images").iterdir()), [])

    def test_partition_mounts_and_eject_cleanup(self):
        image = self.root / "HD.img"
        image.write_bytes(bytes(4096))
        loop = "/org/freedesktop/UDisks2/block_devices/loop0"
        records = {loop + "p1": {"org.freedesktop.UDisks2.Block": {
            "IdType": "apfs", "Device": list(b"/dev/loop0p1\0")}}}
        result = {"path": "/run/media/zacos9-mac/1001/7-1", "device": "/dev/loop0p1",
                  "paths": ["/run/media/zacos9-mac/1001/7-1/1",
                            "/run/media/zacos9-mac/1001/7-1/2"]}
        with patch.dict(os.environ, {"XDG_CACHE_HOME": str(self.root / "cache")}), \
                patch.object(images, "loop_setup", return_value=loop), \
                patch.object(images, "managed_objects", return_value=records), \
                patch.object(images, "run", return_value=json.dumps(result)) as run, \
                patch.object(images, "block_call") as call, \
                patch.object(images, "mounted_paths", return_value=set()) as mounts:
            info = images.mount(image)
            self.assertEqual(info["paths"], result["paths"])
            run.assert_called_once_with(["pkexec", images.HELPER, "mount", "/dev/loop0p1"])
            mounts.return_value = set(result["paths"])
            images.release_image("/dev/loop0p1")
            call.assert_not_called()
            mounts.return_value = set()
            images.release_image("/dev/loop0p1")
            call.assert_called_once_with("Delete", loop, "Loop", "(a{sv})", ({},))
            self.assertEqual(list((self.root / "cache/zacos9/mac-images").iterdir()), [])

    def test_partial_mount_failure_rolls_back_and_preserves_source(self):
        image = self.root / "HD.img"
        image.write_bytes(bytes(4096))
        loop = "/org/freedesktop/UDisks2/block_devices/loop0"
        records = {
            loop + f"p{i}": {"org.freedesktop.UDisks2.Block": {
                "IdType": "hfsplus", "Device": list(f"/dev/loop0p{i}\0".encode())}}
            for i in (1, 2)
        }
        result = {"path": "/run/media/zacos9-mac/1001/7-1", "device": "/dev/loop0p1"}
        with patch.dict(os.environ, {"XDG_CACHE_HOME": str(self.root / "cache")}), \
                patch.object(images, "loop_setup", return_value=loop), \
                patch.object(images, "managed_objects", return_value=records), \
                patch.object(images, "run", side_effect=[
                    json.dumps(result), ValueError("second partition failed"), json.dumps(result)]) as run, \
                patch.object(images, "block_call") as call:
            with self.assertRaisesRegex(ValueError, "second partition"):
                images.mount(image)
            self.assertEqual(run.call_args_list[-1].args[0],
                             ["pkexec", images.HELPER, "unmount", "/dev/loop0p1"])
            call.assert_called_once_with("Delete", loop, "Loop", "(a{sv})", ({},))
            self.assertEqual(image.read_bytes(), bytes(4096))


class PrivilegedTests(unittest.TestCase):
    def test_mount_table_detects_mounts_even_when_stat_cannot(self):
        table = (
            "50 1 0:50 / /run/media/zacos9-mac/1001/8-18/4 ro - fuse /dev/fuse ro\n"
            "51 1 0:51 / /media/A\\040Disk ro - hfs /dev/sdz ro\n")
        for module in (helper, images):
            with self.subTest(module=module.__name__), \
                    patch.object(Path, "read_text", return_value=table), \
                    patch.object(os.path, "ismount", return_value=False):
                self.assertEqual(module.mounted_paths(),
                                 {"/run/media/zacos9-mac/1001/8-18/4", "/media/A Disk"})
        with patch.object(Path, "read_text", return_value=table):
            self.assertEqual(helper.container_mounts(Path("/run/media/zacos9-mac/1001/8-18")),
                             ["/run/media/zacos9-mac/1001/8-18/4"])

    def test_reject_paths_before_opening(self):
        for device in ("/etc/passwd", "/dev/../etc/passwd", "/dev/mapper/root", "/dev/sdb;id"):
            with self.subTest(device=device), patch.object(helper.os, "open") as opened:
                with self.assertRaises(ValueError):
                    helper.mount_device("mount", device, 1001)
                opened.assert_not_called()

    def test_regular_files_cannot_be_mounted(self):
        with patch.object(helper.os, "open", return_value=3), \
                patch.object(helper.os, "fstat", return_value=SimpleNamespace(st_mode=stat.S_IFREG)), \
                patch.object(helper.os, "close") as close:
            with self.assertRaisesRegex(ValueError, "block device"):
                helper.mount_device("mount", "/dev/sdb", 1001)
            close.assert_called_once_with(3)

    def test_root_mount_directory_rejects_symlinks_and_user_writable_paths(self):
        for mode, uid in ((stat.S_IFLNK | 0o777, 0), (stat.S_IFDIR | 0o777, 0),
                          (stat.S_IFDIR | 0o755, 1001)):
            with self.subTest(mode=mode, uid=uid), \
                    patch.object(Path, "mkdir"), \
                    patch.object(Path, "lstat", return_value=SimpleNamespace(
                        st_mode=mode, st_uid=uid)):
                with self.assertRaisesRegex(ValueError, "Unsafe"):
                    helper.root_directory(Path("/run/media/fake"))

    def test_mount_root_keeps_caller_access_private(self):
        with patch.object(helper, "root_directory"), \
                patch.object(helper, "run") as run, \
                patch.object(helper.os, "open", return_value=3), \
                patch.object(helper.os, "fstat", return_value=SimpleNamespace(
                    st_mode=stat.S_IFREG | 0o600, st_uid=0)), \
                patch.object(helper.os, "close"), \
                patch.object(helper.fcntl, "flock"):
            with helper.mount_root(1001) as directory:
                self.assertEqual(directory, helper.ROOT / "1001")
            run.assert_called_once_with([
                "/usr/bin/setfacl", "--set", "u::rwx,u:1001:r-x,g::---,m::r-x,o::---",
                str(helper.ROOT / "1001")])

    def test_readonly_mount_options_and_pinned_descriptor(self):
        for kind in ("hfs", "hfsplus", "apfs", "ext4"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as directory:
                @contextlib.contextmanager
                def root(uid):
                    yield Path(directory)
                calls = []

                def run(args, pass_fds=()):
                    calls.append((args, pass_fds))
                    return kind

                with patch.object(helper.os, "open", return_value=3) as opened, \
                        patch.object(helper.os, "fstat", return_value=SimpleNamespace(
                            st_mode=stat.S_IFBLK, st_rdev=os.makedev(8, 17))), \
                        patch.object(helper.os, "close"), \
                        patch.object(helper, "mount_root", root), \
                        patch.object(helper, "root_directory"), \
                        patch.object(helper, "mount_apfs",
                                     return_value=[{"name": "Chromasystem SSD",
                                                    "path": str(Path(directory) / "8-17")}]) as apfs, \
                        patch.object(helper, "container_mounts", return_value=[]), \
                        patch.object(helper, "mounted_paths",
                                     return_value={str(Path(directory) / "8-17")}), \
                        patch.object(helper, "run", side_effect=run):
                    if kind == "ext4":
                        with self.assertRaisesRegex(ValueError, "Not an HFS"):
                            helper.mount_device("mount", "/dev/sdb1", 1001)
                        self.assertEqual(len(calls), 1)
                        continue
                    result = helper.mount_device("mount", "/dev/sdb1", 1001)
                    self.assertEqual(result["device"], "/dev/sdb1")
                    flags = opened.call_args.args[1]
                    self.assertEqual(flags & os.O_ACCMODE, os.O_RDONLY)
                    self.assertTrue(flags & os.O_NOFOLLOW)
                    if kind == "apfs":
                        apfs.assert_called_once_with("/proc/self/fd/3", Path(directory) / "8-17", 3)
                        self.assertEqual(len(calls), 1)
                        self.assertEqual(json.loads((Path(directory) / "8-17.json").read_text()), result)
                        continue
                    args, descriptors = calls[1]
                    self.assertIn("/proc/self/fd/3", args)
                    self.assertEqual(descriptors, (3,))
                    options = args[args.index("-X" if kind == "apfs" else "-o") + 1].split(",")
                    self.assertTrue({"ro", "nodev", "nosuid", "noexec"} <= set(options))
                    self.assertEqual(json.loads((Path(directory) / "8-17.json").read_text()), result)

    def test_apfs_uses_numeric_indexes_for_every_volume(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            volumes = [{"index": i, "name": f"Disk {i}"} for i in (1, 2, 3)]
            with patch.object(helper, "apfs_volumes", return_value=volumes), \
                    patch.object(helper, "root_directory"), \
                    patch.object(helper, "run") as run, \
                    patch.object(helper, "mounted_paths",
                                 return_value={str(target / str(i)) for i in (1, 2, 3)}):
                self.assertEqual(helper.mount_apfs("/proc/self/fd/3", target, 3),
                                 [{"name": f"Disk {i}", "path": str(target / str(i))}
                                  for i in (1, 2, 3)])
                self.assertEqual([call.args[0][1:3] for call in run.call_args_list],
                                 [["-f", "1"], ["-f", "2"], ["-f", "3"]])
                for call in run.call_args_list:
                    self.assertEqual(call.args[1], (3,))
                    self.assertIn("ro,nodev,nosuid,noexec,allow_other", call.args[0])

    def test_apfs_helper_volumes_are_not_mounted_and_names_preserved(self):
        names = ["Preboot", "Recovery", "VM", "Chromasystem SSD", "Update",
                 "Chromasystem SSD - Data", "iSCPreboot", "xART", "Hardware"]
        volumes = [{"index": i, "name": name} for i, name in enumerate(names, 1)]
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            with patch.object(helper, "apfs_volumes", return_value=volumes), \
                    patch.object(helper, "root_directory"), \
                    patch.object(helper, "mounted_paths",
                                 return_value={str(target / "4"), str(target / "6")}), \
                    patch.object(helper, "run") as run:
                result = helper.mount_apfs("/proc/self/fd/3", target, 3)
                self.assertEqual(result, [
                    {"name": "Chromasystem SSD", "path": str(target / "4")},
                    {"name": "Chromasystem SSD - Data", "path": str(target / "6")}])
                self.assertEqual([call.args[0][1:3] for call in run.call_args_list],
                                 [["-f", "4"], ["-f", "6"]])

    def test_apfs_filter_uses_exact_names_not_substrings(self):
        names = [" recovery ", "PREBOOT", "VM", "Update", "Recovery Documents",
                 "VM Projects", "Update Archive", "Personal SSD"]
        volumes = [{"index": i, "name": name} for i, name in enumerate(names, 1)]
        self.assertEqual([volume["name"] for volume in helper.user_apfs_volumes(volumes)],
                         names[4:])
        with self.assertRaisesRegex(ValueError, "no normal"):
            helper.user_apfs_volumes(volumes[:4])

    def test_apfs_library_reads_each_stored_name_and_frees_handles(self):
        names = [b"Preboot", b"Chromasystem SSD", b"Chromasystem SSD - Data"]
        freed = []
        indexes = []

        def initialize(container, error):
            container._obj.value = 100
            return 1

        def get_count(container, count, error):
            count._obj.value = len(names)
            return 1

        def get_volume(container, index, volume, error):
            indexes.append(index)
            volume._obj.value = index + 1
            return 1

        def get_name_size(volume, size, error):
            size._obj.value = len(names[volume.value - 1]) + 1
            return 1

        def get_name(volume, buffer, size, error):
            buffer.value = names[volume.value - 1]
            return 1

        def free_volume(volume, error):
            freed.append(volume._obj.value)
            volume._obj.value = None
            return 1

        def free_container(container, error):
            freed.append(container._obj.value)
            container._obj.value = None
            return 1

        library = SimpleNamespace(
            libfsapfs_container_initialize=initialize,
            libfsapfs_container_open=lambda *args: 1,
            libfsapfs_container_get_number_of_volumes=get_count,
            libfsapfs_container_get_volume_by_index=get_volume,
            libfsapfs_volume_get_utf8_name_size=get_name_size,
            libfsapfs_volume_get_utf8_name=get_name,
            libfsapfs_volume_free=free_volume,
            libfsapfs_container_close=lambda *args: 0,
            libfsapfs_container_free=free_container,
            libfsapfs_error_sprint=lambda *args: 1,
            libfsapfs_error_free=lambda *args: None)
        with patch.object(helper.ctypes, "CDLL", return_value=library):
            self.assertEqual(helper.apfs_volumes("/proc/self/fd/3"), [
                {"index": i, "name": name.decode()} for i, name in enumerate(names, 1)])
        self.assertEqual(indexes, [0, 1, 2])
        self.assertEqual(freed, [1, 2, 3, 100])

    def test_apfs_failure_rolls_back_prior_volumes(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            with patch.object(helper, "apfs_volumes",
                              return_value=[{"index": i, "name": f"Disk {i}"} for i in (1, 2)]), \
                    patch.object(helper, "root_directory", side_effect=lambda path: path.mkdir()), \
                    patch.object(helper, "run", side_effect=[
                        "", ValueError("volume 2 unreadable"), ""]) as run, \
                    patch.object(helper, "mounted_paths", return_value={str(target / "1")}):
                with self.assertRaisesRegex(ValueError, "volume 2 unreadable"):
                    helper.mount_apfs("/proc/self/fd/3", target, 3)
                self.assertEqual(run.call_args_list[-1].args[0],
                                 ["/usr/bin/umount", "--", str(target / "1")])
                self.assertEqual(list(target.iterdir()), [])

    def test_apfs_reader_failure_after_mount_rolls_back_current_volume(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            with patch.object(helper, "apfs_volumes",
                              return_value=[{"index": i, "name": f"Disk {i}"} for i in (1, 2)]), \
                    patch.object(helper, "root_directory", side_effect=lambda path: path.mkdir()), \
                    patch.object(helper, "run", side_effect=[
                        "", ValueError("reader failed after mounting"), "", ""]) as run, \
                    patch.object(helper, "mounted_paths",
                                 return_value={str(target / "1"), str(target / "2")}):
                with self.assertRaisesRegex(ValueError, "^reader failed after mounting$"):
                    helper.mount_apfs("/proc/self/fd/3", target, 3)
                self.assertEqual([call.args[0] for call in run.call_args_list[-2:]],
                                 [["/usr/bin/umount", "--", str(target / str(i))]
                                  for i in (2, 1)])
                self.assertEqual(list(target.iterdir()), [])

    def test_apfs_rollback_failure_preserves_original_error_and_mount_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            with patch.object(helper, "apfs_volumes", return_value=[{"index": 1, "name": "Disk"}]), \
                    patch.object(helper, "root_directory", side_effect=lambda path: path.mkdir()), \
                    patch.object(helper, "run", side_effect=[
                        ValueError("reader error"), ValueError("busy unmount")]), \
                    patch.object(helper, "mounted_paths", return_value={str(target / "1")}):
                with self.assertRaisesRegex(ValueError, "reader error; APFS rollback failed: busy unmount"):
                    helper.mount_apfs("/proc/self/fd/3", target, 3)
                self.assertTrue((target / "1").is_dir())

    def test_apfs_container_reader_reports_invalid_source(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "invalid.img"
            source.write_bytes(bytes(4096))
            try:
                helper.ctypes.CDLL("libfsapfs.so.1")
            except OSError:
                self.skipTest("libfsapfs1 is not installed")
            with self.assertRaisesRegex(ValueError, "APFS container"):
                helper.apfs_volumes(str(source))

    def test_apfs_eject_unmounts_container_children(self):
        self.check_apfs_eject(recorded=True)

    def test_apfs_eject_recovers_mounts_without_record(self):
        self.check_apfs_eject(recorded=False)

    def test_orphan_mount_is_recorded_for_eject_not_reused_as_success(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "8-17"
            active = [str(target / "4")]

            @contextlib.contextmanager
            def mount_root(uid):
                yield root

            with patch.object(helper.os, "open", return_value=3), \
                    patch.object(helper.os, "fstat", return_value=SimpleNamespace(
                        st_mode=stat.S_IFBLK, st_rdev=os.makedev(8, 17))), \
                    patch.object(helper.os, "close"), \
                    patch.object(helper, "mount_root", mount_root), \
                    patch.object(helper, "container_mounts", return_value=active):
                for attempt in range(2):
                    with self.subTest(attempt=attempt), \
                            self.assertRaisesRegex(ValueError, "leftover mounts"):
                        helper.mount_device("mount", "/dev/sdb1", 1001)
                record = json.loads((root / "8-17.json").read_text())
                self.assertEqual(record["paths"], active)
                self.assertTrue(record["recovery"])

    def check_apfs_eject(self, recorded):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "8-17"
            target.mkdir()
            paths = [str(target / str(i)) for i in (1, 2)]
            for path in paths:
                Path(path).mkdir()
            record = root / "8-17.json"
            if recorded:
                record.write_text(json.dumps({"path": str(target), "device": "/dev/sdb1",
                                              "paths": paths}))

            @contextlib.contextmanager
            def mount_root(uid):
                yield root

            with patch.object(helper.os, "open", return_value=3), \
                    patch.object(helper.os, "fstat", return_value=SimpleNamespace(
                        st_mode=stat.S_IFBLK, st_rdev=os.makedev(8, 17))), \
                    patch.object(helper.os, "close"), \
                    patch.object(helper, "mount_root", mount_root), \
                    patch.object(helper, "container_mounts", side_effect=[paths, []]), \
                    patch.object(helper, "root_directory"), \
                    patch.object(helper, "run") as run:
                helper.mount_device("unmount", "/dev/sdb1", 1001)
                self.assertEqual([call.args[0] for call in run.call_args_list],
                                 [["/usr/bin/umount", "--", path] for path in reversed(paths)])
                self.assertFalse(record.exists())
                self.assertFalse(target.exists())

    def test_installed_apfs_reader_accepts_numeric_selection(self):
        reader = shutil.which("fsapfsmount")
        if not reader:
            self.skipTest("libfsapfs-utils is not installed")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "invalid-apfs.img"
            source.write_bytes(bytes(4096))
            target = Path(directory) / "mount"
            target.mkdir()
            result = subprocess.run([reader, "-f", "1", str(source), str(target)],
                                    capture_output=True, text=True, timeout=10, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("unsupported character value", result.stderr.lower())
            self.assertNotIn("invalid volume index", result.stderr.lower())
            self.assertIn("Unable to open source", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
