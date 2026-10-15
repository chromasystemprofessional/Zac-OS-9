"""A stand-in for accountsservice (org.freedesktop.Accounts) on the session
bus, for the Users panel's test: the methods the panel uses, over the
accounts it starts with (you, with this process's uid, an administrator;
and "bob"). Every call is written to LOG, one per line. A method
named in the file LOG.deny (comma-separated, read on each call) answers
PermissionDenied, as accountsservice does when the administrator's password
isn't given.

usage: fake_accounts.py LOG
"""
import os
import sys

import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

LOG = sys.argv[1]


def denied(method):
    try:
        with open(LOG + ".deny") as f:
            return method in f.read().strip().split(",")
    except FileNotFoundError:
        return False

ROOT = "/org/freedesktop/Accounts"

XML = """
<node>
  <interface name="org.freedesktop.Accounts">
    <method name="ListCachedUsers"><arg direction="out" type="ao"/></method>
    <method name="CreateUser">
      <arg direction="in" type="s"/><arg direction="in" type="s"/><arg direction="in" type="i"/>
      <arg direction="out" type="o"/>
    </method>
    <method name="DeleteUser"><arg direction="in" type="x"/><arg direction="in" type="b"/></method>
  </interface>
  <interface name="org.freedesktop.Accounts.User">
    <method name="SetRealName"><arg direction="in" type="s"/></method>
    <method name="SetIconFile"><arg direction="in" type="s"/></method>
    <method name="SetLocked"><arg direction="in" type="b"/></method>
    <method name="SetAccountType"><arg direction="in" type="i"/></method>
    <method name="SetPassword"><arg direction="in" type="s"/><arg direction="in" type="s"/></method>
    <property name="Uid" type="t" access="read"/>
    <property name="UserName" type="s" access="read"/>
    <property name="RealName" type="s" access="read"/>
    <property name="AccountType" type="i" access="read"/>
    <property name="HomeDirectory" type="s" access="read"/>
    <property name="IconFile" type="s" access="read"/>
    <property name="Locked" type="b" access="read"/>
    <property name="SystemAccount" type="b" access="read"/>
  </interface>
</node>
"""
node = Gio.DBusNodeInfo.new_for_xml(XML)
root_iface, user_iface = node.interfaces

users = {}   # uid -> dict
objects = {}  # uid -> registration id


def log(line):
    with open(LOG, "a") as f:
        f.write(line + "\n")


def path_of(uid):
    return f"{ROOT}/User{uid}"


def user_props(u):
    return {
        "Uid": GLib.Variant("t", u["uid"]), "UserName": GLib.Variant("s", u["name"]),
        "RealName": GLib.Variant("s", u["real"]), "AccountType": GLib.Variant("i", u["type"]),
        "HomeDirectory": GLib.Variant("s", "/home/" + u["name"]),
        "IconFile": GLib.Variant("s", u["icon"]), "Locked": GLib.Variant("b", u["locked"]),
        "SystemAccount": GLib.Variant("b", u["system"]),
    }


def on_user_call(conn, sender, path, iface, method, params, invocation):
    uid = int(path.rsplit("User", 1)[1])
    args = params.unpack()
    log(f"{method} {users[uid]['name']} {' '.join(map(str, args))}")
    if denied(method):
        invocation.return_dbus_error("org.freedesktop.Accounts.Error.PermissionDenied", "Not authorized")
        return
    u = users[uid]
    if method == "SetRealName":
        u["real"] = args[0]
    elif method == "SetIconFile":
        u["icon"] = args[0]
    elif method == "SetLocked":
        u["locked"] = args[0]
    elif method == "SetAccountType":
        u["type"] = args[0]
    elif method == "SetPassword":
        if not args[0].startswith("$"):
            invocation.return_dbus_error("org.freedesktop.Accounts.Error.Failed", "not a crypt(3) hash")
            return
    invocation.return_value(None)


def on_user_get(conn, sender, path, iface, prop):
    return user_props(users[int(path.rsplit("User", 1)[1])])[prop]


def add(uid, name, real, type_, system=False):
    users[uid] = dict(uid=uid, name=name, real=real, type=type_, icon="", locked=False, system=system)
    objects[uid] = bus.register_object(path_of(uid), user_iface, on_user_call, on_user_get, None)


def on_root_call(conn, sender, path, iface, method, params, invocation):
    args = params.unpack()
    log(f"{method} {' '.join(map(str, args))}".rstrip())
    if denied(method):
        invocation.return_dbus_error("org.freedesktop.Accounts.Error.PermissionDenied", "Not authorized")
    elif method == "ListCachedUsers":
        invocation.return_value(GLib.Variant("(ao)", ([path_of(u) for u in sorted(users)],)))
    elif method == "CreateUser":
        name, real, type_ = args
        if any(u["name"] == name for u in users.values()):
            invocation.return_dbus_error("org.freedesktop.Accounts.Error.UserExists", "exists")
            return
        uid = max(users) + 1
        add(uid, name, real, type_)
        invocation.return_value(GLib.Variant("(o)", (path_of(uid),)))
    elif method == "DeleteUser":
        uid, _ = args
        bus.unregister_object(objects.pop(uid))
        users.pop(uid)
        invocation.return_value(None)


def on_bus(conn, name):
    global bus
    bus = conn
    conn.register_object(ROOT, root_iface, on_root_call, None, None)
    add(os.getuid(), "you", "You Yourself", 1)
    add(os.getuid() + 1, "bob", "Bob Builder", 0)
    add(120, "lightdm", "Light Display Manager", 0, system=True)


def on_name(conn, name):
    log("ready")


bus = None
Gio.bus_own_name(Gio.BusType.SESSION, "org.freedesktop.Accounts", Gio.BusNameOwnerFlags.NONE,
                 on_bus, on_name, lambda *a: sys.exit("could not own the name"))
GLib.MainLoop().run()
