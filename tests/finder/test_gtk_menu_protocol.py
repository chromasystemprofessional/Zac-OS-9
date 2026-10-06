import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gio, GLib

compositor, observer, fixture, module, qt_fixture = sys.argv[1:]


def iterate(seconds=0.1):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        while GLib.MainContext.default().pending():
            GLib.MainContext.default().iteration(False)
        time.sleep(0.01)


with tempfile.TemporaryDirectory(prefix="zacos9-gtk-menus-") as temporary:
    root = Path(temporary)
    env = os.environ | {
        "HOME": temporary, "XDG_CONFIG_HOME": str(root / "config"),
        "XDG_DATA_HOME": str(root / "data"), "XDG_RUNTIME_DIR": temporary,
        "WAYLAND_DISPLAY": "wayland-0", "GDK_BACKEND": "wayland",
        "WLR_BACKENDS": "headless", "WLR_RENDERER": "pixman", "WLR_HEADLESS_OUTPUTS": "1",
        "ZACOS9_FINDER": "", "ZACOS9_MENUBAR": "", "ZACOS9_STARTUP": "0",
        "GTK_MODULES": module, "UBUNTU_MENUPROXY": "1",
        "GSETTINGS_BACKEND": "memory",
        "GIO_USE_VFS": "local",
    }
    processes = []
    with (root / "session.log").open("w+") as log:
        try:
            server = subprocess.Popen([compositor], env=env, stdout=log, stderr=log)
            processes.append(server)
            deadline = time.monotonic() + 8
            while not (root / "wayland-0").is_socket():
                assert server.poll() is None and time.monotonic() < deadline, "Compositor failed"
                time.sleep(0.05)
            monitor = subprocess.Popen([observer], env=env, stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=log, text=True)
            processes.append(monitor)
            messages = queue.Queue()
            rendered = queue.Queue()
            titles = queue.Queue()
            def receive():
                for line in monitor.stdout:
                    if line.startswith("("):
                        messages.put(GLib.Variant.parse(GLib.VariantType("(usssss)"),
                                                       line.strip(), None, None).unpack())
                    elif line.startswith("ITEM "):
                        rendered.put(line[5:].strip().split("\t", 1))
                    elif line.startswith("MENU "):
                        process_id, title = line[5:].strip().split("\t", 1)
                        titles.put((int(process_id), title))
            reader = threading.Thread(target=receive, daemon=True)
            reader.start()

            def metadata(process, old_window=None):
                deadline = time.monotonic() + 8
                while time.monotonic() < deadline:
                    try:
                        item = messages.get(timeout=0.1)
                    except queue.Empty:
                        continue
                    if item[0] == process.pid and item[1] and item[3] and item[4] != old_window:
                        return item
                raise AssertionError("No focused GTK menu properties received")

            app = subprocess.Popen([sys.executable, fixture], env=env, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=log, text=True)
            processes.append(app)
            activations = queue.Queue()
            def responses(process):
                for line in process.stdout:
                    activations.put(line.strip())
            threading.Thread(target=responses, args=(app,), daemon=True).start()
            first = metadata(app)
            assert first[4] == first[3], "Legacy menu actions must use their actual export path"
            bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

            models = {}
            def labels(model):
                result = []
                for index in range(model.get_n_items()):
                    label = model.get_item_attribute_value(index, "label", GLib.VariantType("s"))
                    if label:
                        result.append(label.unpack())
                    for kind in ("section", "submenu"):
                        child = model.get_item_link(index, kind)
                        if child:
                            retained = models.setdefault((hash(model), index, kind), child)
                            result.extend(labels(retained))
                return result

            def loaded(properties, expected):
                model = Gio.DBusMenuModel.get(bus, properties[1], properties[3])
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    values = labels(model)
                    if any(expected in label for label in values):
                        return model, values
                    iterate()
                raise AssertionError(f"Exported menus missing {expected}: {labels(model)}")

            def activate_rendered(wanted):
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    try:
                        label, argument = rendered.get(timeout=0.1)
                    except queue.Empty:
                        continue
                    if wanted in label:
                        monitor.stdin.write("click " + argument + "\n")
                        monitor.stdin.flush()
                        assert activations.get(timeout=5) == "ACTIVATED " + wanted
                        return
                raise AssertionError(f"Global menu backend did not render enabled {wanted} action")

            model, values = loaded(first, "First")
            assert any("File" in label for label in values), values
            def status(expected):
                app.stdin.write("status\n")
                app.stdin.flush()
                assert activations.get(timeout=5) == "MENUBAR " + expected
            iterate(0.3)
            status("HIDDEN")
            activate_rendered("First")
            app.stdin.write("second\n")
            app.stdin.flush()
            second = metadata(app, first[4])
            assert second[1] == first[1] and second[4] != first[4], "Same-process windows not distinguished"
            model2, values2 = loaded(second, "Second")
            app.stdin.write("close\n")
            app.stdin.flush()
            back = metadata(app, second[4])
            assert back[4] == first[4], "Closing second window did not restore first window menu"

            native = subprocess.Popen([sys.executable, fixture, "--native"], env=env,
                                      stdout=subprocess.PIPE, stderr=log, text=True)
            processes.append(native)
            threading.Thread(target=responses, args=(native,), daemon=True).start()
            native_properties = metadata(native)
            native_model, native_labels = loaded(native_properties, "Native")
            activate_rendered("Native")
            print("OK: legacy and native GTK menus export working actions; focus distinguishes same-process windows")
            if shutil.which("galculator"):
                calculator = subprocess.Popen(["galculator"], env=env, stdout=log, stderr=log)
                processes.append(calculator)
                calc = metadata(calculator)
                calc_model, calc_labels = loaded(calc, "File")
                assert any("Edit" in label for label in calc_labels), calc_labels
                assert any("Help" in label for label in calc_labels), calc_labels
                assert calculator.poll() is None
                global_titles = set()
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and not {"File", "Edit", "Help"} <= global_titles:
                    try:
                        process_id, title = titles.get(timeout=0.1)
                    except queue.Empty:
                        continue
                    if process_id == calculator.pid:
                        global_titles.add(title)
                assert {"File", "Edit", "Help"} <= global_titles, global_titles
                print("OK: actual Galculator File/Edit/Help render in the global-menu backend")
            else:
                print("Galculator-specific check skipped: optional application is not installed")
            qt = subprocess.Popen([qt_fixture], env=env | {"QT_QPA_PLATFORM": "wayland"},
                                  stdout=subprocess.PIPE, stderr=log, text=True)
            processes.append(qt)
            threading.Thread(target=responses, args=(qt,), daemon=True).start()
            activate_rendered("Qt action")
            print("OK: Qt global-menu registration and action forwarding still work after GTK integration")
            monitor.terminate()
            monitor.wait(timeout=5)
            iterate(0.3)
            status("VISIBLE")
        except Exception:
            log.flush()
            log.seek(0)
            print(log.read(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
