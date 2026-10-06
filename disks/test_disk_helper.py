"""Mocked disk safety plus label probes on disposable regular-file images.

No physical storage is unmounted, partitioned or formatted.
"""

import contextlib
import copy
import importlib.machinery
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET


helper_path = (sys.argv.pop(1) if len(sys.argv) > 1 else
               str(Path(__file__).with_name("zacos9-disk-helper")))
loader = importlib.machinery.SourceFileLoader("disk_helper", helper_path)
spec = importlib.util.spec_from_loader(loader.name, loader)
helper = importlib.util.module_from_spec(spec)
loader.exec_module(helper)

TOKEN = "a" * 64


def disk(**changes):
    item = {
        "name": "/dev/sdb", "type": "disk", "size": 8 * 1024**3,
        "ro": False, "tran": "usb", "rm": True, "model": " USB Disk ",
        "serial": "serial-1", "wwn": "", "maj:min": "8:16",
        "fstype": None, "mountpoints": [], "log-sec": 512,
    }
    item.update(changes)
    return item


def part(**changes):
    item = disk(name="/dev/sdb1", type="part", size=8 * 1024**3 - 2048 * 512,
                **{"maj:min": "8:17"})
    item.update(changes)
    return item


def state(*items, mounted=(), swap=(), extra_edges=()):
    if not items:
        items = (disk(),)
    nodes = {item["maj:min"]: copy.deepcopy(item) for item in items}
    graph = {devno: set() for devno in nodes}
    edges = [("8:16", "8:17")] if "8:17" in nodes else []
    for a, b in [*edges, *extra_edges]:
        graph.setdefault(a, set()).add(b)
        graph.setdefault(b, set()).add(a)
    return nodes, graph, set(mounted), set(swap)


def result(stdout="", code=0, stderr=""):
    return SimpleNamespace(stdout=stdout, returncode=code, stderr=stderr)


def mount(devno="8:17", target="/media/user/USB", root="/", identifier="20",
          parent="1"):
    return (identifier, parent, devno, root, target,
            f"{identifier} {parent} {devno} {root} {target} rw - vfat /dev/sdb1 rw")


class SafetyTests(unittest.TestCase):
    def setUp(self):
        self.info = patch.object(helper, "device_info", return_value=TOKEN).start()
        patch.object(helper, "read_text", return_value="0").start()
        patch.object(helper.os, "access", return_value=False).start()
        self.addCleanup(patch.stopall)

    def assert_rejected(self, system, message):
        with self.assertRaisesRegex(helper.Failure, message):
            helper.select(system, "/dev/sdb", TOKEN)

    def test_eligible_whole_usb_disk(self):
        self.assertEqual(helper.select(state(), "/dev/sdb", TOKEN)["type"], "disk")

    def test_mounted_disk_and_child_even_outside_system_paths(self):
        for number in ("8:16", "8:17"):
            with self.subTest(number=number):
                self.assert_rejected(state(disk(), part(), mounted=[number]), "mounted")

    def test_swap_partition_disk_and_swapfile_backing_device(self):
        for number in ("8:16", "8:17"):
            self.assert_rejected(state(disk(), part(), swap=[number]), "swap")

    def test_protected_system_ancestry_through_mapper_lvm_and_raid(self):
        for kind in ("crypt", "lvm", "raid1", "dm"):
            mapped = disk(name="/dev/dm-0", type=kind, **{"maj:min": "253:0"})
            system = state(disk(), part(), mapped, mounted=["253:0"],
                           extra_edges=[("8:17", "253:0")])
            self.assert_rejected(system, "mounted")
            system[2].clear()
            self.assert_rejected(system, "mapped")

    def test_unknown_topology_fails_closed(self):
        self.assert_rejected(state(extra_edges=[("8:16", "253:0")]), "identify")

    def test_read_only_and_invalid_capacities(self):
        for changes, reason in [
            ({"ro": True}, "read-only"), ({"size": 0}, "too small"),
            ({"size": helper.MIN_SIZE - 1}, "too small"),
            ({"size": helper.MAX_SIZE}, "2 TiB"),
            ({"size": helper.MAX_SIZE + 512}, "2 TiB"),
            ({"log-sec": 4096}, "512-byte"),
        ]:
            with self.subTest(changes=changes):
                self.assert_rejected(state(disk(**changes)), reason)

    def test_encrypted_lvm_raid_swap_and_other_storage_signatures(self):
        for fs in ("crypto_LUKS", "BitLocker", "LVM2_member", "linux_raid_member",
                   "isw_raid_member", "swap", "zfs_member", "bcache"):
            for on_part in (False, True):
                with self.subTest(fs=fs, on_part=on_part):
                    items = (disk(), part(fstype=fs)) if on_part else (disk(fstype=fs),)
                    self.assert_rejected(state(*items), "signatures")

    def test_partition_internal_loop_mapper_and_unsanitized_argument(self):
        for device in ("/dev/sdb1", "/dev/loop0", "/dev/mapper/root",
                       "/dev/disk/by-id/usb-x", "/dev/sdb;reboot", "--help"):
            with self.assertRaises(helper.Failure):
                helper.select(state(), device, TOKEN)
        with self.assertRaisesRegex(helper.Failure, "whole"):
            helper.assess(state(disk(type="part")), disk(type="part"))
        self.info.side_effect = helper.Failure("Only physical USB disks")
        self.assert_rejected(state(disk(tran="sata")), "USB")

    def test_changed_identity_and_disappeared_disk(self):
        self.info.return_value = "b" * 64
        self.assert_rejected(state(), "identity changed")
        self.info.return_value = TOKEN
        self.assert_rejected(state(disk(name="/dev/sdc")), "disappeared")

    def test_listing_contract_and_unreadable_semantics(self):
        for fs, unreadable in [(None, True), ("mystery", True), ("vfat", False),
                               ("ext4", False), ("ntfs", False), ("hfsplus", False)]:
            with self.subTest(fs=fs), patch.object(
                helper, "snapshot", return_value=state(disk(), part(fstype=fs)),
            ):
                listing = helper.listing()
                self.assertEqual(listing["version"], 1)
                entry = listing["disks"][0]
                self.assertEqual(entry["identity"], TOKEN)
                self.assertEqual(entry["name"], "USB Disk")
                self.assertEqual(entry["children"], ["/dev/sdb1"])
                self.assertTrue(entry["eligible"])
                self.assertEqual(entry["unreadable"], unreadable)

    def test_known_mounted_fs_is_never_unreadable_or_eligible(self):
        with patch.object(helper, "snapshot", return_value=state(
            disk(), part(fstype="vfat"), mounted=["8:17"],
        )):
            entry = helper.listing()["disks"][0]
            self.assertFalse(entry["unreadable"])
            self.assertFalse(entry["eligible"])
            self.assertIn("mounted", entry["reason"])

    def test_readable_raw_encrypted_signatures_are_rejected_in_listing(self):
        with patch.object(helper, "snapshot", return_value=state(disk(), part())), \
                patch.object(helper.os, "access", return_value=True), \
                patch.object(helper, "run", return_value=result("TYPE=crypto_LUKS\n")):
            entry = helper.listing()["disks"][0]
        self.assertFalse(entry["eligible"])
        self.assertFalse(entry["unreadable"])
        self.assertIn("signatures", entry["reason"])

    def test_raw_probe_failure_does_not_offer_initialization(self):
        with patch.object(helper, "snapshot", return_value=state()), \
                patch.object(helper.os, "access", return_value=True), \
                patch.object(helper, "run", side_effect=helper.Failure("blkid failed")):
            entry = helper.listing()["disks"][0]
        self.assertFalse(entry["eligible"])
        self.assertEqual(entry["reason"], "blkid failed")

    def test_raw_known_fs_corrects_stale_unknown_filesystem_metadata(self):
        with patch.object(helper, "snapshot", return_value=state()), \
                patch.object(helper.os, "access", return_value=True), \
                patch.object(helper, "run", return_value=result("TYPE=vfat\n")):
            entry = helper.listing()["disks"][0]
        self.assertTrue(entry["eligible"])
        self.assertFalse(entry["unreadable"])

    def test_unformattable_disk_retains_identity_and_eject_mapping(self):
        with patch.object(helper, "snapshot", return_value=state(
            disk(size=helper.MAX_SIZE), part(),
        )):
            entry = helper.listing()["disks"][0]
        self.assertFalse(entry["eligible"])
        self.assertEqual(entry["identity"], TOKEN)
        self.assertEqual(entry["children"], ["/dev/sdb1"])
        self.assertIn("2 TiB", entry["reason"])

    def test_list_excludes_internal_and_partitions(self):
        with patch.object(helper, "snapshot", return_value=state(
            disk(tran="sata"), part(),
        )):
            self.assertEqual(helper.listing(), {"version": 1, "disks": []})

    def manual_state(self, targets=("/media/user/USB",), **changes):
        return state(disk(), part(mountpoints=list(targets), fstype="vfat", **changes),
                     mounted=["8:17"])

    def test_manual_listing_same_schema_but_mounted_user_volume_eligible(self):
        for target in ("/media/user/USB", "/run/media/user/USB", "/media/USB space"):
            with patch.object(helper, "snapshot", return_value=self.manual_state((target,))), \
                    patch.object(helper, "mount_table", return_value=[mount(target=target)]), \
                    patch.object(helper.os.path, "realpath", side_effect=lambda path: path):
                automatic = helper.listing()["disks"][0]
                manual = helper.listing(allow_user_mounts=True)["disks"][0]
                self.assertEqual(manual.keys(), automatic.keys())
                self.assertEqual(manual["identity"], TOKEN)
                self.assertEqual(manual["children"], ["/dev/sdb1"])
                self.assertTrue(manual["eligible"])
                self.assertFalse(manual["unreadable"])
                self.assertFalse(automatic["eligible"])

    def test_manual_rejects_all_system_nonuser_and_noncanonical_mounts(self):
        for target in ("/", "/boot", "/boot/efi", "/home", "/var", "/mnt/usb",
                       "/media", "/run/media", "/media-other/usb", "/run/media2/usb",
                       "/media/user/../usb", "/media/user/usb/"):
            with self.subTest(target=target), \
                    patch.object(helper, "mount_table", return_value=[mount(target=target)]):
                with self.assertRaises(helper.Failure):
                    helper.select(self.manual_state((target,)), "/dev/sdb", TOKEN, True)

    def test_manual_rejects_bind_alias_even_when_user_alias_appears_first(self):
        for target in ("/", "/boot", "/home", "/media/user/alias"):
            mounts = [mount(), mount(target=target, identifier="21")]
            with patch.object(helper, "mount_table", return_value=mounts):
                with self.assertRaisesRegex(helper.Failure, "System|bind|topology"):
                    helper.select(self.manual_state(("/media/user/USB", target)),
                                  "/dev/sdb", TOKEN, True)
        with patch.object(helper, "mount_table", return_value=[mount(root="/folder")]):
            with self.assertRaisesRegex(helper.Failure, "bind"):
                helper.select(self.manual_state(), "/dev/sdb", TOKEN, True)

    def test_manual_rejects_nested_foreign_mounts_and_overmounts(self):
        for other in (mount(devno="8:1", target="/media/user/USB/child",
                            identifier="21", parent="20"),
                      mount(devno="8:1", target="/elsewhere", identifier="21", parent="20"),
                      mount(devno="8:1", identifier="21")):
            with patch.object(helper, "mount_table", return_value=[mount(), other]):
                with self.assertRaisesRegex(helper.Failure, "Nested|overmounted"):
                    helper.select(self.manual_state(), "/dev/sdb", TOKEN, True)

    def test_manual_rejects_unknown_or_inconsistent_mount_metadata(self):
        for mounts, targets in [([], ["/media/user/USB"]), ([mount()], []),
                                ([mount()], ["/media/user/other"])]:
            with patch.object(helper, "mount_table", return_value=mounts):
                with self.assertRaisesRegex(helper.Failure, "topology"):
                    helper.select(self.manual_state(targets), "/dev/sdb", TOKEN, True)
        with patch.object(helper, "mount_table", return_value=[mount()]), \
                patch.object(helper.os.path, "realpath", return_value="/elsewhere"):
            with self.assertRaises(helper.Failure):
                helper.select(self.manual_state(), "/dev/sdb", TOKEN, True)

    def test_manual_retains_all_storage_protections(self):
        unsafe = [
            state(disk(ro=True)), state(disk(size=helper.MIN_SIZE - 1)),
            state(disk(size=helper.MAX_SIZE)), state(disk(**{"log-sec": 4096})),
            state(disk(), part(ro=True)),
            state(disk(), part(), swap=["8:17"]),
            state(extra_edges=[("8:16", "253:0")]),
            state(disk(), disk(name="/dev/sdc", **{"maj:min": "8:32"}),
                  extra_edges=[("8:16", "8:32")]),
        ]
        for fs in ("crypto_LUKS", "BitLocker", "LVM2_member", "linux_raid_member",
                   "swap", "zfs_member", "bcache"):
            unsafe.append(state(disk(), part(fstype=fs)))
        for kind in ("crypt", "lvm", "raid1", "dm", "loop"):
            unsafe.append(state(disk(), part(type=kind)))
        with patch.object(helper, "mount_table", return_value=[]):
            for system in unsafe:
                with self.subTest(system=system), self.assertRaises(helper.Failure):
                    helper.select(system, "/dev/sdb", TOKEN, True)
            self.info.side_effect = helper.Failure("Only physical USB disks")
            with self.assertRaisesRegex(helper.Failure, "USB"):
                helper.select(state(disk(tran="sata")), "/dev/sdb", TOKEN, True)

    def test_manual_unmounted_candidate_and_changed_identity(self):
        with patch.object(helper, "mount_table", return_value=[]):
            self.assertEqual(helper.select(state(), "/dev/sdb", TOKEN, True)["name"],
                             "/dev/sdb")
            self.info.return_value = "b" * 64
            with self.assertRaisesRegex(helper.Failure, "identity changed"):
                helper.select(state(), "/dev/sdb", TOKEN, True)


class IdentityTests(unittest.TestCase):
    def setUp(self):
        self.root = Path("/sys/devices/pci/usb1/1-2/host/block/sdb")
        self.device_stat = SimpleNamespace(st_mode=stat.S_IFBLK, st_rdev=os.makedev(8, 16))
        self.fields = {"diskseq": "10", "size": str(8 * 1024**3 // 512)}
        self.usb = True

        def resolve(path, **kwargs):
            if str(path) == "/sys/dev/block/8:16":
                return self.root
            if str(path).endswith("/subsystem"):
                return Path("/sys/bus/usb" if self.usb else "/sys/bus/scsi")
            return path

        patch.object(helper.os.path, "realpath", side_effect=lambda path: path).start()
        patch.object(helper.os, "stat", return_value=self.device_stat).start()
        patch.object(helper.Path, "resolve", autospec=True, side_effect=resolve).start()
        patch.object(helper.Path, "exists", return_value=False).start()
        patch.object(helper, "read_text",
                     side_effect=lambda path: self.fields[Path(path).name]).start()
        self.addCleanup(patch.stopall)

    def test_identity_binds_every_replacement_property(self):
        original = helper.device_info(disk())
        for field, value in [("serial", "different"), ("wwn", "wwn-2"),
                             ("log-sec", 4096)]:
            self.assertNotEqual(original, helper.device_info(disk(**{field: value})))
        self.fields["diskseq"] = "11"
        self.assertNotEqual(original, helper.device_info(disk()))
        self.fields["diskseq"] = "10"
        self.root = Path("/sys/devices/pci/usb2/2-1/host/block/sdb")
        self.assertNotEqual(original, helper.device_info(disk()))
        self.fields["size"] = str(16 * 1024**3 // 512)
        self.assertNotEqual(original, helper.device_info(disk(size=16 * 1024**3)))

    def test_nonusb_sysfs_even_when_transport_claims_usb(self):
        self.usb = False
        with self.assertRaisesRegex(helper.Failure, "USB"):
            helper.device_info(disk())

    def test_invalid_block_node_symlink_partition_size_and_sequence(self):
        for mode, number in [(stat.S_IFREG, os.makedev(8, 16)),
                             (stat.S_IFBLK, os.makedev(8, 32))]:
            self.device_stat.st_mode, self.device_stat.st_rdev = mode, number
            with self.assertRaises(helper.Failure):
                helper.device_info(disk())
        self.device_stat.st_mode, self.device_stat.st_rdev = stat.S_IFBLK, os.makedev(8, 16)
        with patch.object(helper.os.path, "realpath", return_value="/dev/sdc"):
            with self.assertRaises(helper.Failure):
                helper.device_info(disk())
        with patch.object(helper.Path, "exists", return_value=True):
            with self.assertRaisesRegex(helper.Failure, "Partitions"):
                helper.device_info(disk())
        self.fields["size"] = "1"
        with self.assertRaisesRegex(helper.Failure, "size changed"):
            helper.device_info(disk())
        self.fields["size"] = str(8 * 1024**3 // 512)
        self.fields["diskseq"] = ""
        with self.assertRaisesRegex(helper.Failure, "insertion"):
            helper.device_info(disk())


class TopologyTests(unittest.TestCase):
    def setUp(self):
        self.disk_root = Path("/sys/devices/usb1/block/sdb")
        self.part_root = self.disk_root / "sdb1"
        self.mapped_root = Path("/sys/devices/virtual/block/dm-0")
        self.loop_root = Path("/sys/devices/virtual/block/loop0")
        self.roots = {"8:16": self.disk_root, "8:17": self.part_root,
                      "253:0": self.mapped_root, "7:0": self.loop_root}
        self.files = {
            str(root / "dev"): number for number, root in self.roots.items()
        }
        self.files.update({
            "/proc/self/mountinfo": "1 0 253:0 / / rw - ext4 /dev/dm-0 rw\n",
            "/proc/swaps": "Filename\tType\tSize\tUsed\tPriority\n",
        })
        self.exists = {str(self.part_root / "partition")}
        self.directory = {
            str(self.disk_root): [self.part_root],
            str(self.part_root / "holders"): [self.mapped_root],
            str(self.mapped_root / "slaves"): [self.part_root],
        }
        self.items = [
            disk(children=[part(children=[
                disk(name="/dev/dm-0", type="crypt", **{"maj:min": "253:0"}),
            ])]),
        ]

        def resolve(path, **kwargs):
            return self.roots.get(path.name, path)

        patch.object(helper.Path, "resolve", autospec=True, side_effect=resolve).start()
        patch.object(helper.Path, "exists", autospec=True,
                     side_effect=lambda path: str(path) in self.exists
                     or str(path) in self.directory).start()
        patch.object(helper.Path, "iterdir", autospec=True,
                     side_effect=lambda path: iter(self.directory.get(str(path), []))).start()
        patch.object(helper, "read_text", side_effect=lambda path: self.files[str(path)]).start()
        patch.object(helper, "run", side_effect=lambda argv: result(json.dumps(
            {"blockdevices": self.items},
        ))).start()
        self.addCleanup(patch.stopall)

    def test_kernel_and_lsblk_topology_protect_mapped_root_boot_home(self):
        for mountpoint in ("/", "/boot", "/home", "/other"):
            self.files["/proc/self/mountinfo"] = (
                f"1 0 253:0 / {mountpoint} rw - ext4 /dev/dm-0 rw\n"
            )
            system = helper.snapshot()
            self.assertIn("253:0", helper.connected(system[1], "8:16"))
            with patch.object(helper, "device_info", return_value=TOKEN), \
                    patch.object(helper, "read_text", return_value="0"):
                with self.assertRaisesRegex(helper.Failure, "mounted"):
                    helper.select(system, "/dev/sdb", TOKEN)

    def test_kernel_holders_found_even_when_lsblk_tree_omits_relationship(self):
        self.items = [
            disk(children=[part()]),
            disk(name="/dev/dm-0", type="lvm", **{"maj:min": "253:0"}),
        ]
        system = helper.snapshot()
        self.assertEqual(helper.connected(system[1], "8:16"),
                         {"8:16", "8:17", "253:0"})

    def test_swap_block_and_file_and_escaped_filename(self):
        self.files["/proc/self/mountinfo"] = ""
        for filename, mode, rdev, dev in [
            ("/dev/sdb1", stat.S_IFBLK, os.makedev(8, 17), 0),
            ("/home/swap file", stat.S_IFREG, 0, os.makedev(253, 0)),
        ]:
            escaped = filename.replace(" ", r"\040")
            self.files["/proc/swaps"] = (
                f"Filename\tType\tSize\tUsed\tPriority\n{escaped} file 1 0 -2\n"
            )
            with patch.object(helper.os, "stat", return_value=SimpleNamespace(
                st_mode=mode, st_rdev=rdev, st_dev=dev,
            )) as mocked:
                system = helper.snapshot()
            mocked.assert_called_once_with(filename)
            self.assertTrue(helper.connected(system[1], "8:16") & system[3])

    def test_loop_backing_disk_or_file_protects_usb_ancestor(self):
        self.items.append(disk(name="/dev/loop0", type="loop", **{"maj:min": "7:0"}))
        self.exists.add(str(self.loop_root / "loop" / "backing_file"))
        self.files[str(self.loop_root / "loop" / "backing_file")] = "/dev/sdb1"
        self.files["/proc/self/mountinfo"] = "1 0 7:0 / / rw - ext4 /dev/loop0 rw\n"
        with patch.object(helper.os, "stat", return_value=SimpleNamespace(
            st_mode=stat.S_IFBLK, st_rdev=os.makedev(8, 17),
        )):
            system = helper.snapshot()
        self.assertIn("7:0", helper.connected(system[1], "8:16"))
        self.assertTrue(helper.connected(system[1], "8:16") & system[2])

    def test_changed_sysfs_or_unreadable_swap_fails_closed(self):
        self.files[str(self.mapped_root / "dev")] = "253:9"
        with self.assertRaisesRegex(helper.Failure, "changed"):
            helper.snapshot()
        self.files[str(self.mapped_root / "dev")] = "253:0"
        self.files["/proc/swaps"] = ""
        with self.assertRaisesRegex(helper.Failure, "swap"):
            helper.snapshot()


class InitializeTests(unittest.TestCase):
    def setUp(self):
        self.formatted = False
        self.partitioned = False
        self.commands = []
        self.mock_snapshot = patch.object(
            helper, "snapshot", side_effect=self.snapshot,
        ).start()
        patch.object(helper, "device_info", return_value=TOKEN).start()
        patch.object(helper, "read_text", return_value="0").start()
        patch.object(helper.os, "geteuid", return_value=0).start()
        patch.object(helper.os, "fsync").start()
        self.exclusive_open = False
        self.mock_ioctl = patch.object(helper.fcntl, "ioctl", side_effect=self.ioctl).start()
        patch.object(helper.os.path, "realpath", side_effect=lambda path: path).start()
        self.mock_run = patch.object(helper, "run", side_effect=self.command).start()
        self.mock_open = patch.object(helper, "opened", side_effect=self.opened).start()
        self.addCleanup(patch.stopall)

    def snapshot(self):
        return state(disk(), part()) if self.partitioned else state()

    @contextlib.contextmanager
    def opened(self, device, devno, exclusive=False, locked=False):
        if exclusive:
            self.exclusive_open = True
        try:
            yield 41 if exclusive else 42
        finally:
            if exclusive:
                self.exclusive_open = False

    def ioctl(self, fd, request):
        self.assertEqual((fd, request), (41, helper.BLKRRPART))
        self.assertTrue(self.exclusive_open, "kernel refresh must use our exclusive claim")

    def command(self, argv, **kwargs):
        self.commands.append((argv, kwargs))
        if argv[0] == helper.SFDISK:
            if "--json" in argv:
                return result(json.dumps({"partitiontable": {
                    "label": "dos", "sectorsize": 512, "partitions": [
                        {"start": 2048, "size": 8 * 1024**3 // 512 - 2048, "type": "c"},
                    ],
                }}))
            self.assertTrue(self.exclusive_open)
            self.assertIn("--no-reread", argv,
                          "sfdisk must not try to acquire our exclusive claim again")
            self.assertIn("--no-tell-kernel", argv,
                          "kernel refresh must use the already-exclusive descriptor")
            self.partitioned = True
        if argv[0] == helper.MKFS:
            self.assertTrue(self.mock_ioctl.called, "refresh kernel partitions before mkfs")
            self.formatted = True
        if argv[0] == helper.BLKID and self.formatted:
            return result("TYPE=vfat\nVERSION=FAT32\nLABEL=Untitled\n")
        return result()

    def invoke(self, expected=TOKEN):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            helper.initialize("/dev/sdb", expected)
        return json.loads(output.getvalue())

    def destructive(self):
        return [argv for argv, kwargs in self.commands
                if argv[0] == helper.MKFS or
                (argv[0] == helper.SFDISK and "--json" not in argv)]

    def test_order_forced_fat32_mbr_default_name_pinned_descriptors(self):
        self.assertTrue(self.invoke()["initialized"])
        destructive = self.destructive()
        self.assertEqual(len(destructive), 2)
        self.assertEqual(destructive[0][0], helper.SFDISK)
        self.assertEqual(destructive[0][-1], "/proc/self/fd/41")
        self.assertEqual(destructive[1],
                         [helper.MKFS, "-F", "32", "-n", "Untitled", "/proc/self/fd/42"])
        partition_call = next(kwargs for argv, kwargs in self.commands
                              if argv == destructive[0])
        self.assertEqual(partition_call["input"],
                         "label: dos\nunit: sectors\n\nstart=2048, type=c\n")
        self.assertEqual(partition_call["pass_fds"], (41,))
        self.assertIn("--no-reread", destructive[0])
        self.assertIn("--no-tell-kernel", destructive[0])
        self.assertNotIn("--force", destructive[0])
        self.mock_ioctl.assert_called_once_with(41, helper.BLKRRPART)
        self.mock_open.assert_any_call("/dev/sdb", "8:16", exclusive=True)
        self.mock_open.assert_any_call("/dev/sdb1", "8:17")

    def test_kernel_reread_failure_never_formats_or_reports_success(self):
        self.mock_ioctl.side_effect = OSError("Device or resource busy")
        with self.assertRaisesRegex(helper.Failure, "kernel.*NOT formatted"):
            self.invoke()
        self.assertTrue(self.partitioned)
        self.assertFalse(self.formatted)

    def test_custom_label_reaches_mkfs_and_verification(self):
        original = self.command
        def command(argv, **kwargs):
            value = original(argv, **kwargs)
            if argv[0] == helper.BLKID and self.formatted:
                return result("TYPE=vfat\nVERSION=FAT32\nLABEL=My USB\n")
            return value
        self.mock_run.side_effect = command
        with contextlib.redirect_stdout(io.StringIO()):
            helper.initialize("/dev/sdb", TOKEN, "My USB")
        self.assertIn([helper.MKFS, "-F", "32", "-n", "My USB", "/proc/self/fd/42"],
                      self.destructive())

    def test_invalid_labels_fail_before_any_storage_commands(self):
        for label in ("", "abcdefghijkl", " name", "name ", "é", "disk/one",
                      "bad\nname", "NO NAME", "no name", "a.b", "a*b", "a\x00b"):
            with self.subTest(label=label), self.assertRaises(helper.Failure):
                helper.initialize("/dev/sdb", TOKEN, label)
            with self.subTest(label=label), self.assertRaises(helper.Failure):
                helper.erase("/dev/sdb", TOKEN, label)
        self.assertEqual(self.commands, [])

    def test_not_root_invalid_identity_or_mounted_never_write(self):
        for root, token, system in [(1000, TOKEN, state()), (0, "bad", state()),
                                    (0, TOKEN, state(mounted=["8:16"]))]:
            with patch.object(helper.os, "geteuid", return_value=root), patch.object(
                helper, "snapshot", return_value=system,
            ), self.assertRaises(helper.Failure):
                self.invoke(token)
            self.assertEqual(self.destructive(), [])

    def test_real_probe_encrypted_or_ambiguous_signatures_never_write(self):
        for output in ("TYPE=crypto_LUKS\n", "TYPE=LVM2_member\n", "USAGE=raid\n"):
            with patch.object(helper, "run", return_value=result(output)):
                with self.assertRaisesRegex(helper.Failure, "signatures"):
                    self.invoke()
            self.assertEqual(self.destructive(), [])
        with patch.object(helper, "run", side_effect=helper.Failure("ambivalent probe")):
            with self.assertRaises(helper.Failure):
                self.invoke()
        self.assertEqual(self.destructive(), [])

    def test_unplug_replug_between_each_revalidation_never_formats(self):
        for change_at in range(1, 8):
            self.partitioned, self.formatted = False, False
            self.commands.clear()
            identities = iter([TOKEN] * (change_at - 1) + ["b" * 64] * 20)
            with patch.object(helper, "device_info", side_effect=lambda item: next(identities)):
                with self.assertRaisesRegex(helper.Failure, "identity changed"):
                    self.invoke()
            self.assertFalse(self.formatted)
            if change_at <= 4:
                self.assertEqual(self.destructive(), [])

    def test_mount_race_before_partitioning_or_formatting(self):
        for mount_at in (2, 3, 4, 5, 6, 7):
            self.partitioned, self.formatted = False, False
            self.commands.clear()
            count = 0

            def snapshot():
                nonlocal count
                count += 1
                system = self.snapshot()
                if count >= mount_at:
                    system[2].add("8:16")
                return system

            with patch.object(helper, "snapshot", side_effect=snapshot):
                with self.assertRaisesRegex(helper.Failure, "mounted"):
                    self.invoke()
            self.assertFalse(self.formatted)

    def test_unsafe_storage_never_reaches_either_destructive_command(self):
        systems = [
            state(disk(type="part")), state(disk(ro=True)),
            state(disk(), part(fstype="crypto_LUKS")),
            state(disk(), part(fstype="LVM2_member")),
            state(disk(), part(fstype="linux_raid_member")),
            state(disk(), part(), swap=["8:17"]),
            state(disk(), part(), mounted=["8:17"]),
            state(disk(size=helper.MAX_SIZE + 512)),
        ]
        for system in systems:
            with patch.object(helper, "snapshot", return_value=system):
                with self.assertRaises(helper.Failure):
                    self.invoke()
            self.assertEqual(self.destructive(), [])
        with patch.object(helper, "device_info", side_effect=helper.Failure("not USB")):
            with self.assertRaises(helper.Failure):
                self.invoke()
        self.assertEqual(self.destructive(), [])

    def test_partition_failure_does_not_format(self):
        original = self.command

        def command(argv, **kwargs):
            if argv[0] == helper.SFDISK:
                raise helper.Failure("partition failed")
            return original(argv, **kwargs)

        self.mock_run.side_effect = command
        with self.assertRaisesRegex(helper.Failure, "partition failed"):
            self.invoke()
        self.assertFalse(self.formatted)

    def test_missing_or_wrong_size_partition_is_not_formatted(self):
        with patch.object(helper, "snapshot", side_effect=lambda: state()), \
                patch.object(helper.time, "monotonic", side_effect=[0, 11]):
            with self.assertRaisesRegex(helper.Failure, "did not appear"):
                self.invoke()
        self.assertFalse(self.formatted)
        self.partitioned = False
        with patch.object(helper, "snapshot", side_effect=lambda: (
            state(disk(), part(size=1024)) if self.partitioned else state()
        )):
            with self.assertRaisesRegex(helper.Failure, "partition device"):
                self.invoke()
        self.assertFalse(self.formatted)

    def test_signature_appearing_after_partitioning_prevents_format(self):
        original = self.command

        def command(argv, **kwargs):
            if self.partitioned and argv[0] == helper.BLKID:
                return result("TYPE=crypto_LUKS\n")
            return original(argv, **kwargs)

        self.mock_run.side_effect = command
        with self.assertRaisesRegex(helper.Failure, "signatures"):
            self.invoke()
        self.assertFalse(self.formatted)

    def test_unexpected_table_and_partition_node_never_format(self):
        original = self.command

        def bad_table(argv, **kwargs):
            if "--json" in argv:
                return result('{"partitiontable":{"label":"gpt","partitions":[]}}')
            return original(argv, **kwargs)

        self.mock_run.side_effect = bad_table
        with self.assertRaisesRegex(helper.Failure, "partition table"):
            self.invoke()
        self.assertFalse(self.formatted)
        self.partitioned = False
        self.mock_run.side_effect = original
        with patch.object(helper.os.path, "realpath", return_value="/dev/sdc1"):
            with self.assertRaisesRegex(helper.Failure, "partition device"):
                self.invoke()
        self.assertFalse(self.formatted)

    def test_failed_verification_is_not_reported_as_success(self):
        original = self.command

        def command(argv, **kwargs):
            value = original(argv, **kwargs)
            if self.formatted and argv[0] == helper.BLKID:
                return result("TYPE=ext4\n")
            return value

        self.mock_run.side_effect = command
        with self.assertRaisesRegex(helper.Failure, "verify"):
            self.invoke()


class EraseTests(unittest.TestCase):
    opened = InitializeTests.opened
    ioctl = InitializeTests.ioctl
    test_kernel_reread_failure_never_formats_or_reports_success = (
        InitializeTests.test_kernel_reread_failure_never_formats_or_reports_success
    )
    destructive = InitializeTests.destructive

    def setUp(self):
        self.mounts = [mount()]
        self.mount_open = False
        InitializeTests.setUp(self)
        patch.object(helper, "mount_table", side_effect=lambda: list(self.mounts)).start()
        self.pin = patch.object(helper, "check_pinned").start()
        patch.object(helper, "opened_mount", side_effect=self.opened_mount).start()

    def snapshot(self):
        targets = [entry[4] for entry in self.mounts]
        return state(disk(), part(fstype="vfat", mountpoints=targets),
                     mounted=["8:17"] if targets else [])

    @contextlib.contextmanager
    def opened_mount(self, entry):
        self.mount_open = True
        try:
            yield 43
        finally:
            self.mount_open = False

    def command(self, argv, **kwargs):
        if argv[0] == helper.UMOUNT:
            self.assertFalse(self.mount_open, "mount descriptor would make umount busy")
            self.mounts = self.mounts[1:]
        return InitializeTests.command(self, argv, **kwargs)

    def invoke(self, expected=TOKEN):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            helper.erase("/dev/sdb", expected)
        return json.loads(output.getvalue())

    def test_erase_unmounts_before_existing_initializer_and_reports_same_success(self):
        self.assertEqual(self.invoke(),
                         {"version": 1, "device": "/dev/sdb", "initialized": True})
        commands = [argv for argv, _ in self.commands]
        unmount = [helper.UMOUNT, "--no-canonicalize", "--internal-only",
                   "--", "/media/user/USB"]
        self.assertIn(unmount, commands)
        self.assertLess(commands.index(unmount),
                        next(i for i, argv in enumerate(commands) if argv[0] == helper.SFDISK))
        self.assertEqual(next(kwargs for argv, kwargs in self.commands if argv == unmount),
                         {})
        self.assertEqual(self.destructive()[1],
                         [helper.MKFS, "-F", "32", "-n", "Untitled", "/proc/self/fd/42"])
        self.mock_open.assert_any_call("/dev/sdb", "8:16", locked=True)
        self.mock_open.assert_any_call("/dev/sdb1", "8:17")
        self.pin.assert_any_call(42, "8:16", 0)
        self.pin.assert_any_call(42, "8:17", 0)

    def test_erase_unmounted_disk_does_not_unmount(self):
        self.mounts.clear()
        self.assertTrue(self.invoke()["initialized"])
        self.assertFalse(any(argv[0] == helper.UMOUNT for argv, _ in self.commands))

    def test_erase_whole_disk_filesystem(self):
        self.mounts = [mount(devno="8:16")]
        def snapshot():
            if not self.partitioned:
                return state(disk(fstype="vfat",
                                  mountpoints=[entry[4] for entry in self.mounts]),
                             mounted=["8:16"] if self.mounts else [])
            return state(disk(), part())
        self.mock_snapshot.side_effect = snapshot
        self.assertTrue(self.invoke()["initialized"])
        self.assertTrue(self.formatted)

    def test_erase_two_mounted_partitions(self):
        self.mounts.append(mount(devno="8:18", target="/run/media/user/Second",
                                 identifier="21"))
        def snapshot():
            if self.partitioned:
                return state(disk(), part())
            items = [disk()]
            for devno in ("8:17", "8:18"):
                targets = [entry[4] for entry in self.mounts if entry[2] == devno]
                items.append(part(name="/dev/sdb1" if devno == "8:17" else "/dev/sdb2",
                                  mountpoints=targets, **{"maj:min": devno}))
            return state(*items, mounted=[entry[2] for entry in self.mounts],
                         extra_edges=[("8:16", "8:18")])
        self.mock_snapshot.side_effect = snapshot
        self.assertTrue(self.invoke()["initialized"])
        self.assertEqual(sum(argv[0] == helper.UMOUNT for argv, _ in self.commands), 2)

    def test_erase_root_and_valid_token_required_before_any_commands(self):
        for uid, token in ((1000, TOKEN), (0, "bad"), (0, "b" * 64)):
            with patch.object(helper.os, "geteuid", return_value=uid):
                with self.assertRaises(helper.Failure):
                    self.invoke(token)
            self.assertEqual(self.commands, [])

    def test_busy_unmount_aborts_without_partitioning_or_formatting(self):
        original = self.command
        def command(argv, **kwargs):
            if argv[0] == helper.UMOUNT:
                raise helper.Failure("umount failed: target is busy")
            return original(argv, **kwargs)
        self.mock_run.side_effect = command
        with self.assertRaisesRegex(helper.Failure, "busy"):
            self.invoke()
        self.assertEqual(self.destructive(), [])

    def test_failed_pinned_mount_open_never_unmounts_or_formats(self):
        with patch.object(helper, "opened_mount", side_effect=helper.Failure(
            "Mount identity changed",
        )):
            with self.assertRaisesRegex(helper.Failure, "Mount identity"):
                self.invoke()
        self.assertFalse(any(argv[0] == helper.UMOUNT for argv, _ in self.commands))
        self.assertEqual(self.destructive(), [])

    def test_successful_unmount_status_without_mount_removal_aborts(self):
        self.mock_run.side_effect = lambda argv, **kwargs: InitializeTests.command(
            self, argv, **kwargs,
        )
        with self.assertRaisesRegex(helper.Failure, "topology"):
            self.invoke()
        self.assertEqual(self.destructive(), [])

    def test_replug_at_each_pre_unmount_validation_aborts_before_unmount(self):
        for change_at in range(1, 7):
            self.commands.clear()
            identities = iter([TOKEN] * (change_at - 1) + ["b" * 64] * 30)
            with patch.object(helper, "device_info", side_effect=lambda item: next(identities)):
                with self.assertRaisesRegex(helper.Failure, "identity changed"):
                    self.invoke()
            self.assertFalse(any(argv[0] == helper.UMOUNT for argv, _ in self.commands))
            self.assertEqual(self.destructive(), [])

    def test_replug_after_unmount_never_formats(self):
        original = self.command
        def command(argv, **kwargs):
            value = original(argv, **kwargs)
            if argv[0] == helper.UMOUNT:
                helper.device_info.return_value = "b" * 64
            return value
        self.mock_run.side_effect = command
        with self.assertRaisesRegex(helper.Failure, "identity changed"):
            self.invoke()
        self.assertEqual(self.destructive(), [])

    def test_changed_pinned_disk_or_partition_never_unmounts(self):
        for changed in ("8:16", "8:17"):
            self.commands.clear()
            def check(fd, devno, sequence):
                if devno == changed:
                    raise helper.Failure("Pinned disk identity changed")
            self.pin.side_effect = check
            with self.assertRaisesRegex(helper.Failure, "Pinned"):
                self.invoke()
            self.assertFalse(any(argv[0] == helper.UMOUNT for argv, _ in self.commands))
            self.assertEqual(self.destructive(), [])

    def test_mount_identity_or_target_changes_before_unmount_abort(self):
        for changed in (mount(identifier="22"), mount(target="/media/user/Other"),
                        mount(root="/subdir"), mount(target="/home")):
            self.commands.clear()
            self.mounts = [mount()]
            count = 0
            def mounts():
                nonlocal count
                count += 1
                if count >= 4:
                    self.mounts = [changed]
                return list(self.mounts)
            with patch.object(helper, "mount_table", side_effect=mounts):
                with self.assertRaises(helper.Failure):
                    self.invoke()
            self.assertFalse(any(argv[0] == helper.UMOUNT for argv, _ in self.commands))
            self.assertEqual(self.destructive(), [])

    def test_new_mount_or_child_after_unmount_aborts(self):
        for add_mount in (True, False):
            self.commands.clear()
            self.mounts = [mount()]
            original = self.command
            def command(argv, **kwargs):
                value = original(argv, **kwargs)
                if argv[0] == helper.UMOUNT:
                    if add_mount:
                        self.mounts = [mount(target="/media/user/New", identifier="22")]
                    else:
                        self.mock_snapshot.side_effect = lambda: state(
                            disk(), part(), part(name="/dev/sdb2", **{"maj:min": "8:18"}),
                            extra_edges=[("8:16", "8:18")],
                        )
                return value
            self.mock_run.side_effect = command
            with self.assertRaisesRegex(helper.Failure, "topology"):
                self.invoke()
            self.assertEqual(self.destructive(), [])

    def test_probe_forbidden_signature_before_unmount_never_unmounts(self):
        self.mock_run.side_effect = lambda argv, **kwargs: result("TYPE=crypto_LUKS\n")
        with self.assertRaisesRegex(helper.Failure, "signatures"):
            self.invoke()
        self.assertEqual(self.destructive(), [])
        self.assertEqual(self.mounts, [mount()])

    def test_probe_failure_and_new_signature_at_final_unmount_check_abort(self):
        for fail_probe in (True, False):
            self.commands.clear()
            count = 0
            def command(argv, **kwargs):
                nonlocal count
                if argv[0] == helper.BLKID:
                    count += 1
                    if count >= 3:
                        if fail_probe:
                            raise helper.Failure("blkid failed")
                        return result("TYPE=BitLocker\n")
                return self.command(argv, **kwargs)
            self.mock_run.side_effect = command
            with self.assertRaisesRegex(helper.Failure, "blkid|signatures"):
                self.invoke()
            self.assertFalse(any(argv[0] == helper.UMOUNT for argv, _ in self.commands))
            self.assertEqual(self.destructive(), [])


class RunnerTests(unittest.TestCase):
    def test_mountinfo_decodes_escapes_retains_aliases_and_rejects_malformed_input(self):
        text = ("1 0 8:17 / /media/user/USB\\040disk rw - vfat /dev/sdb1 rw\n"
                "2 0 8:17 / /home rw - vfat /dev/sdb1 rw\n")
        with patch.object(helper, "read_text", return_value=text):
            mounts = helper.mount_table()
        self.assertEqual(mounts[0][4], "/media/user/USB disk")
        self.assertEqual(mounts[1][4], "/home")
        for text in ("bad", "1 0 8:17 / /media/USB rw\n",
                     "1 0 bad / /media/USB rw - vfat /dev/sdb1 rw\n",
                     "1 0 8:17 / relative rw - vfat /dev/sdb1 rw\n",
                     mount()[5] + "\n" + mount()[5]):
            with patch.object(helper, "read_text", return_value=text):
                with self.assertRaises(helper.Failure):
                    helper.mount_table()

    def test_pinned_fd_disk_sequence_and_device_number_must_match(self):
        info = SimpleNamespace(st_mode=stat.S_IFBLK, st_rdev=os.makedev(8, 16))
        with patch.object(helper.os, "fstat", return_value=info), \
                patch.object(helper.fcntl, "ioctl", return_value=helper.struct.pack("=Q", 10)) \
                as ioctl:
            helper.check_pinned(4, "8:16", 10)
            ioctl.assert_called_with(4, helper.BLKGETDISKSEQ, bytes(8))
            for number, sequence in (("8:32", 10), ("8:16", 11)):
                with self.assertRaises(helper.Failure):
                    helper.check_pinned(4, number, sequence)
            info.st_mode = stat.S_IFREG
            with self.assertRaises(helper.Failure):
                helper.check_pinned(4, "8:16", 10)

    def test_opened_mount_pins_original_mount_id_and_backing_device(self):
        info = SimpleNamespace(st_dev=os.makedev(8, 17))
        with patch.object(helper.os, "open", return_value=4) as opened, \
                patch.object(helper.os, "fstat", return_value=info), \
                patch.object(helper.os, "close") as closed, \
                patch.object(helper, "read_text", return_value="mnt_id:\t20\n") as read:
            with helper.opened_mount(mount()) as fd:
                self.assertEqual(fd, 4)
            read.assert_called_once_with("/proc/self/fdinfo/4")
            flags = opened.call_args.args[1]
            self.assertTrue(flags & os.O_PATH)
            self.assertTrue(flags & os.O_NOFOLLOW)
            closed.assert_called_once_with(4)
            for entry in (mount(identifier="21"), mount(devno="8:18")):
                with self.assertRaisesRegex(helper.Failure, "Mount identity"):
                    with helper.opened_mount(entry):
                        self.fail("must not accept replacement mount")

    def test_manual_cli_contract_and_exact_arguments(self):
        with patch.object(helper, "listing", return_value={"version": 1, "disks": []}) \
                as listing, contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["list-erasable"]), 0)
            listing.assert_called_once_with(allow_user_mounts=True)
            self.assertEqual(json.loads(output.getvalue()), {"version": 1, "disks": []})
        with patch.object(helper, "erase") as erase:
            self.assertEqual(helper.main(["erase", "/dev/sdb", TOKEN]), 0)
            erase.assert_called_once_with("/dev/sdb", TOKEN)
        with patch.object(helper, "erase") as erase:
            self.assertEqual(helper.main(["erase", "/dev/sdb", TOKEN, "My USB"]), 0)
            erase.assert_called_once_with("/dev/sdb", TOKEN, "My USB")
        with patch.object(helper, "initialize") as initialize:
            self.assertEqual(helper.main(["initialize", "/dev/sdb", TOKEN, "My USB"]), 0)
            initialize.assert_called_once_with("/dev/sdb", TOKEN, "My USB")
        for args in (["erase"], ["erase", "/dev/sdb"],
                     ["erase", "/dev/sdb", TOKEN, "name", "extra"], ["list-erasable", "extra"]):
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(helper.main(args), 1)

    def test_policy_is_specific_to_installed_helper_and_requires_admin(self):
        directory = Path(helper_path).parent
        policy = ET.parse(directory / "org.zacos9.disks.policy.in").getroot()
        actions = policy.findall("action")
        self.assertEqual(len(actions), 1)
        action = actions[0]
        self.assertEqual(action.attrib["id"], "org.zacos9.disks.initialize")
        self.assertEqual(action.find("annotate").attrib["key"],
                         "org.freedesktop.policykit.exec.path")
        self.assertEqual(action.find("annotate").text, "@DISK_HELPER@")
        self.assertEqual(action.find("defaults/allow_active").text, "auth_admin_keep")
        for name in ("allow_any", "allow_inactive"):
            self.assertEqual(action.find(f"defaults/{name}").text, "auth_admin")
        self.assertEqual(list(directory.glob("*.rules")), [])

    def test_absolute_command_runner_has_clean_environment_no_shell_and_checks_status(self):
        with patch.object(helper.subprocess, "run", return_value=result()) as mocked:
            helper.run([helper.SFDISK, "device"], input="table", pass_fds=(4,))
            mocked.assert_called_once_with(
                [helper.SFDISK, "device"], input="table", text=True,
                capture_output=True, check=False, pass_fds=(4,), timeout=120,
                env={"PATH": "/usr/sbin:/usr/bin:/sbin:/bin", "LC_ALL": "C"},
            )
        for code in (1, 8):
            with patch.object(helper.subprocess, "run", return_value=result(code=code)):
                with self.assertRaises(helper.Failure):
                    helper.run([helper.BLKID], allowed=(0, 2))

    def test_open_checks_block_number_and_closes_on_failure(self):
        with patch.object(helper.os, "open", return_value=4) as open_mock, patch.object(
            helper.os, "fstat", return_value=SimpleNamespace(
                st_mode=stat.S_IFBLK, st_rdev=os.makedev(8, 32),
            ),
        ), patch.object(helper.os, "close") as close_mock:
            with self.assertRaises(helper.Failure):
                with helper.opened("/dev/sdb", "8:16", exclusive=True):
                    self.fail("must not open a replaced disk")
            self.assertTrue(open_mock.call_args.args[1] & os.O_EXCL)
            self.assertTrue(open_mock.call_args.args[1] & os.O_NOFOLLOW)
            close_mock.assert_called_once_with(4)

    def test_operation_lock_is_nonblocking_and_closes_on_contention(self):
        with patch.object(helper.os, "open", return_value=4), patch.object(
            helper.os, "fstat", return_value=SimpleNamespace(
                st_mode=stat.S_IFBLK, st_rdev=os.makedev(8, 16),
            ),
        ), patch.object(helper.os, "close") as close_mock, patch.object(
            helper.fcntl, "flock", side_effect=BlockingIOError("locked"),
        ) as lock_mock:
            with self.assertRaisesRegex(helper.Failure, "busy"):
                with helper.opened("/dev/sdb", "8:16", locked=True):
                    self.fail("must not open a busy disk")
            lock_mock.assert_called_once_with(
                4, helper.fcntl.LOCK_EX | helper.fcntl.LOCK_NB,
            )
            close_mock.assert_called_once_with(4)

    def test_cli_only_accepts_exact_commands_and_reports_failure(self):
        for args in ([], ["eject", "/dev/sdb", TOKEN], ["initialize", "/dev/sdb"],
                     ["list", "extra"]):
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(helper.main(args), 1)
        with patch.object(helper, "listing", return_value={"version": 1, "disks": []}), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(helper.main(["list"]), 0)
        self.assertEqual(json.loads(output.getvalue()), {"version": 1, "disks": []})


class LabelImageTests(unittest.TestCase):
    @unittest.skipUnless(Path(helper.MKFS).is_file() and Path(helper.BLKID).is_file(),
                         "mkfs.fat and blkid are required for image label probes")
    def test_actual_fat32_labels_with_pinned_regular_file(self):
        for label in ("Untitled", "My USB", "12345678901"):
            with self.subTest(label=label), tempfile.TemporaryFile(
                prefix="zacos9-label-test-",
            ) as image:
                image.truncate(helper.MIN_SIZE)
                fd = image.fileno()
                device = f"/proc/self/fd/{fd}"
                helper.run([helper.MKFS, "-F", "32", "-n", label, device],
                           pass_fds=(fd,))
                result = helper.run(
                    [helper.BLKID, "--probe", "--no-encoding", "--output",
                     "export", device], pass_fds=(fd,),
                )
                fields = dict(line.split("=", 1) for line in result.stdout.splitlines()
                              if "=" in line)
                self.assertEqual(fields.get("TYPE"), "vfat")
                self.assertEqual(fields.get("VERSION"), "FAT32")
                self.assertEqual(fields.get("LABEL"), label)


if __name__ == "__main__":
    unittest.main()
