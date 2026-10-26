#!/usr/bin/python3
"""A stand-in UDisks2 on a private bus: one made-up 128 GiB GPT drive, an
EFI System partition and Windows (NTFS) filling the rest. Changes asked
for are written to the log file given; nothing else happens. Prints
"ready" once it has the name."""
import sys

from gi.repository import Gio, GLib

LOG = open(sys.argv[1], "a", buffering=1)
MiB = 1024 * 1024
GiB = 1024 * MiB
DISK = 128 * GiB
BASE = "/org/freedesktop/UDisks2"
U = "org.freedesktop.UDisks2."

drive = BASE + "/drives/Fake_Disk"
disk = BASE + "/block_devices/sdz"
parts = [
    ("sdz1", 1, MiB, 100 * MiB, "c12a7328-f81f-11d2-ba4b-00a0c93ec93b", "vfat", ""),
    ("sdz2", 2, 101 * MiB, DISK - 102 * MiB, "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", "ntfs", "Windows"),
]

XML = """<node>
 <interface name="org.freedesktop.DBus.ObjectManager">
  <method name="GetManagedObjects"><arg type="a{oa{sa{sv}}}" direction="out"/></method>
 </interface>
 <interface name="org.freedesktop.UDisks2.Filesystem">
  <method name="Resize"><arg type="t" direction="in"/><arg type="a{sv}" direction="in"/></method>
 </interface>
 <interface name="org.freedesktop.UDisks2.Partition">
  <method name="Resize"><arg type="t" direction="in"/><arg type="a{sv}" direction="in"/></method>
 </interface>
 <interface name="org.freedesktop.UDisks2.PartitionTable">
  <method name="CreatePartitionAndFormat">
   <arg type="t" direction="in"/><arg type="t" direction="in"/><arg type="s" direction="in"/>
   <arg type="s" direction="in"/><arg type="a{sv}" direction="in"/><arg type="s" direction="in"/>
   <arg type="a{sv}" direction="in"/><arg type="o" direction="out"/>
  </method>
 </interface>
</node>"""
INFO = Gio.DBusNodeInfo.new_for_xml(XML)


def ay(s):
    return GLib.Variant("ay", s.encode() + b"\0")


def objects():
    v = GLib.Variant
    out = {
        drive: {U + "Drive": {"Vendor": v("s", "Fake"), "Model": v("s", "Disk 128"), "Removable": v("b", False)}},
        disk: {
            U + "Block": {"Device": ay("/dev/sdz"), "Size": v("t", DISK), "Drive": v("o", drive),
                          "HintIgnore": v("b", False), "IdType": v("s", "")},
            U + "PartitionTable": {"Type": v("s", "gpt")},
        },
    }
    for dev, num, off, size, kind, fs, label in parts:
        out[BASE + "/block_devices/" + dev] = {
            U + "Block": {"Device": ay("/dev/" + dev), "Size": v("t", size), "Drive": v("o", drive),
                          "HintIgnore": v("b", False), "IdType": v("s", fs), "IdLabel": v("s", label)},
            U + "Partition": {"Number": v("i", num), "Offset": v("t", off), "Size": v("t", size),
                              "Type": v("s", kind), "Table": v("o", disk), "IsContainer": v("b", False),
                              "IsContained": v("b", False)},
            U + "Filesystem": {"MountPoints": v("aay", [])},
        }
    return out


def call(conn, sender, path, iface, method, params, invocation):
    dev = path.rsplit("/", 1)[1]
    if method == "GetManagedObjects":
        invocation.return_value(GLib.Variant("(a{oa{sa{sv}}})", (objects(),)))
    elif iface == U + "Filesystem":
        LOG.write(f"{dev} filesystem-resize {params[0]}\n")
        invocation.return_value(None)
    elif iface == U + "Partition":
        LOG.write(f"{dev} partition-resize {params[0]}\n")
        invocation.return_value(None)
    else:
        offset, size, kind, name, _, fs, fsopts = params.unpack()
        LOG.write(f"create {offset} {size} type={kind} {fs} {fsopts.get('label', '')}\n")
        invocation.return_value(GLib.Variant("(o)", (BASE + "/block_devices/sdz9",)))


def registered(conn, name):
    conn.register_object(BASE, INFO.interfaces[0], call, None, None)
    for path in [disk] + [BASE + "/block_devices/" + p[0] for p in parts]:
        for i in INFO.interfaces[1:]:
            conn.register_object(path, i, call, None, None)


def acquired(conn, name):
    print("ready", flush=True)


Gio.bus_own_name(Gio.BusType.SESSION, "org.freedesktop.UDisks2", 0, registered, acquired, None)
GLib.MainLoop().run()
