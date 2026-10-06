import gi
import sys

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, GLib, Gio

windows = []


def window(name):
    win = Gtk.Window(title=name)
    win.set_default_size(320, 180)
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
    bar = Gtk.MenuBar()
    title = Gtk.MenuItem.new_with_mnemonic("_File")
    menu = Gtk.Menu()
    action = Gtk.MenuItem.new_with_mnemonic("_" + name)
    action.connect("activate", lambda item: print("ACTIVATED " + name, flush=True))
    menu.append(action)
    title.set_submenu(menu)
    bar.append(title)
    box.pack_start(bar, False, False, 0)
    box.pack_start(Gtk.Label(label=name), True, True, 0)
    win.add(box)
    win.testbar = bar
    win.show_all()
    windows.append(win)


def command(stream, condition):
    line = stream.readline().strip()
    if line == "second":
        window("Second")
    elif line == "close":
        windows.pop().destroy()
    elif line == "quit" or not line:
        Gtk.main_quit()
        return False
    elif line == "status":
        print("MENUBAR " + ("VISIBLE" if windows[-1].testbar.get_child_visible() else "HIDDEN"), flush=True)
    return True


if "--native" in sys.argv:
    app = Gtk.Application(application_id="org.zacos9.NativeMenuTest")
    def activate(application):
        action = Gio.SimpleAction.new("test", None)
        action.connect("activate", lambda *args: print("ACTIVATED Native", flush=True))
        application.add_action(action)
        submenu = Gio.Menu()
        submenu.append("_Native", "app.test")
        menu = Gio.Menu()
        menu.append_submenu("_File", submenu)
        application.set_menubar(menu)
        win = Gtk.ApplicationWindow(application=application, title="Native")
        win.set_default_size(320, 180)
        win.show_all()
    app.connect("activate", activate)
    app.run([sys.argv[0]])
else:
    window("First")
    GLib.io_add_watch(sys.stdin, GLib.IO_IN | GLib.IO_HUP, command)
    Gtk.main()
