"""Exercise a standard GTK chooser with an isolated, registered Desktop."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

compositor = Path(sys.argv[1]).resolve()
helper = Path(sys.argv[2]).resolve()
if len(sys.argv) > 3:
    os.environ["GTK_MODULES"] = str(Path(sys.argv[3]).resolve())
    os.environ["ZACOS9_EXPECT_DESKTOP_DEFAULT"] = "1"

try:
    import gi
    gi.require_version("Gtk", "3.0")
except (ImportError, ValueError):
    print("SKIP: GTK 3 Python bindings unavailable")
    sys.exit(77)

with tempfile.TemporaryDirectory(prefix="zacos9-desktop-gtk-") as temporary:
    home = Path(temporary)
    os.environ.update(HOME=str(home), XDG_CONFIG_HOME=str(home / "config"),
                      XDG_DATA_HOME=str(home / "data"), XDG_CACHE_HOME=str(home / "cache"))
    # Open the dialogs on a private headless compositor, never the live desktop.
    os.environ.pop("DISPLAY", None)
    os.environ.update(XDG_RUNTIME_DIR=str(home), WAYLAND_DISPLAY="wayland-0",
                      GDK_BACKEND="wayland", WLR_BACKENDS="headless", WLR_RENDERER="pixman",
                      WLR_HEADLESS_OUTPUTS="1", ZACOS9_FINDER="", ZACOS9_MENUBAR="",
                      ZACOS9_STARTUP="0", ZACOS9_COLLAR="")
    log = (home / "compositor.log").open("w")
    server = subprocess.Popen([str(compositor)], stdout=log, stderr=log)
    deadline = time.monotonic() + 8
    while not (home / "wayland-0").is_socket():
        assert server.poll() is None and time.monotonic() < deadline, "Compositor failed"
        time.sleep(0.05)
    try:
        result = subprocess.run([sys.executable, str(helper)], text=True, capture_output=True)
        assert result.returncode == 0, result.stderr
        from gi.repository import Gtk, GLib
        assert Gtk.init_check()[0], "GTK cannot open the headless compositor"

        desktop = Path(result.stdout.strip())
        target = home / "Real Save Folder"
        target.mkdir()
        alias = desktop / "Save Folder alias"
        alias.symlink_to(target, target_is_directory=True)
        chooser = Gtk.FileChooserDialog(title="Desktop save test", action=Gtk.FileChooserAction.SAVE)
        chooser.show()

        def settle():
            until = time.monotonic() + 0.5
            while time.monotonic() < until:
                while GLib.MainContext.default().pending():
                    GLib.MainContext.default().iteration(False)
                time.sleep(0.01)

        def descendants(widget):
            yield widget
            if isinstance(widget, Gtk.Container):
                for child in widget.get_children():
                    yield from descendants(child)

        try:
            settle()
            sidebars = [widget for widget in descendants(chooser) if isinstance(widget, Gtk.PlacesSidebar)]
            assert sidebars, "GTK chooser has no places sidebar"
            assert any(isinstance(widget, Gtk.Label) and widget.get_text() == "Desktop"
                       for sidebar in sidebars for widget in descendants(sidebar)), \
                "Desktop is missing from the real GTK chooser sidebar"
            assert any(isinstance(widget, Gtk.Label) and widget.get_text() == "Zacintosh HD"
                       for sidebar in sidebars for widget in descendants(sidebar)), \
                "Zacintosh HD is missing from the real GTK chooser sidebar"
            if os.environ.get("ZACOS9_EXPECT_DESKTOP_DEFAULT") == "1":
                assert Path(chooser.get_current_folder()).resolve() == desktop.resolve(), \
                    "GTK save dialog did not default to Desktop"
            private_system_folder = home / "data/zacos9/Zacintosh HD/System Folder"
            assert private_system_folder.is_dir()
            assert chooser.set_current_folder(str(private_system_folder)), \
                "Cannot navigate to the private Zacintosh HD System Folder"
            settle()
            chooser.set_current_name("Private document")
            selected = chooser.get_filename()
            assert selected, "GTK chooser did not return a private Zacintosh HD save path"
            Path(selected).write_text("private", encoding="utf-8")
            assert (private_system_folder / "Private document").read_text() == "private"
            assert chooser.set_current_folder(str(desktop)), "Cannot navigate to Desktop"
            settle()
            assert Path(chooser.get_current_folder()).resolve() == desktop.resolve()
            assert chooser.set_current_folder(str(alias)), "Cannot open Desktop folder alias"
            settle()
            chooser.set_current_name("Saved by GTK")
            selected = chooser.get_filename()
            assert selected, "GTK chooser did not return a save path"
            Path(selected).write_text("Saved through a Desktop alias", encoding="utf-8")
            assert (target / "Saved by GTK").read_text() == "Saved through a Desktop alias"
            explicit = Gtk.FileChooserDialog(title="Explicit GTK save location",
                                             action=Gtk.FileChooserAction.SAVE)
            assert explicit.set_current_folder(str(target))
            explicit.show()
            settle()
            assert Path(explicit.get_current_folder()).resolve() == target.resolve(), \
                "GTK integration overrode an explicit application save location"
            explicit.destroy()
            print("OK: GTK Desktop default, Zacintosh HD place, private saves and explicit folders")
        finally:
            chooser.destroy()
            settle()
    finally:
        server.terminate()
        server.wait()
        log.close()
