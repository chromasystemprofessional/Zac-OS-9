#!/usr/bin/env python3
"""Check the shipped capture integration without changing the live session."""

import configparser
from pathlib import Path
import sys
import unittest


ROOT = Path(sys.argv.pop(1))


class ScreenSharingConfig(unittest.TestCase):
    def test_portal_routing_preserves_finder(self):
        config = configparser.ConfigParser()
        config.optionxform = str
        config.read(ROOT / "share/xdg/xdg-desktop-portal/zacos9-portals.conf")
        preferred = config["preferred"]
        self.assertEqual(preferred["org.freedesktop.impl.portal.FileChooser"], "zacos9")
        self.assertEqual(preferred["org.freedesktop.impl.portal.ScreenCast"], "wlr")
        self.assertNotIn("org.freedesktop.impl.portal.RemoteDesktop", preferred)

    def test_capture_stack_is_required(self):
        control = (ROOT / "debian/control").read_text()
        package = control.split("Package: zacos9\n", 1)[1]
        depends = package.split("Depends:", 1)[1].split("\nRecommends:", 1)[0]
        dependencies = {entry.strip() for entry in depends.split(",")}
        for name in ("pipewire", "wireplumber", "xdg-desktop-portal",
                     "xdg-desktop-portal-wlr"):
            with self.subTest(package=name):
                self.assertIn(name, dependencies)
        self.assertNotIn("rustdesk", dependencies)

    def test_compositor_has_capture_protocol(self):
        main = (ROOT / "compositor/src/main.c").read_text()
        self.assertIn("wlr_screencopy_manager_v1_create(server.display)", main)


if __name__ == "__main__":
    unittest.main()
