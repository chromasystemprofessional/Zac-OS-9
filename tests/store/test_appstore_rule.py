"""Execute the Software helper's polkit rule with mocked subjects and pkexec
command lines; never change host policy.

usage: test_appstore_rule.py RULE
"""
import ctypes
import ctypes.util
import json
from pathlib import Path
import sys
import unittest


rule_path = Path(sys.argv.pop(1))
HELPER = "/usr/libexec/zacos9/zacos9-appstore-helper"


class AppstoreAuthorizationTests(unittest.TestCase):
    def setUp(self):
        library = ctypes.util.find_library("duktape")
        if not library:
            self.skipTest("The polkit JavaScript runtime (libduktape) is unavailable")
        runtime = ctypes.CDLL(library)
        runtime.duk_create_heap.argtypes = [ctypes.c_void_p] * 5
        runtime.duk_create_heap.restype = ctypes.c_void_p
        runtime.duk_destroy_heap.argtypes = [ctypes.c_void_p]
        runtime.duk_get_global_string.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        runtime.duk_push_lstring.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
        runtime.duk_pcall.argtypes = [ctypes.c_void_p, ctypes.c_int]
        runtime.duk_safe_to_lstring.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_size_t)]
        runtime.duk_safe_to_lstring.restype = ctypes.c_char_p
        runtime.duk_pop.argtypes = [ctypes.c_void_p]
        context = runtime.duk_create_heap(None, None, None, None, None)
        self.assertTrue(context)
        self.addCleanup(runtime.duk_destroy_heap, context)
        self.runtime, self.context = runtime, context
        self.evaluate("""
var rule;
var polkit = {Result: {YES: "yes"}, addRule: function(callback) {rule = callback;}};
""" + rule_path.read_text())

    def evaluate(self, source):
        runtime, context = self.runtime, self.context
        runtime.duk_get_global_string(context, b"eval")
        encoded = source.encode()
        runtime.duk_push_lstring(context, encoded, len(encoded))
        status = runtime.duk_pcall(context, 1)
        result = runtime.duk_safe_to_lstring(context, -1, None).decode()
        runtime.duk_pop(context)
        self.assertEqual(status, 0, result)
        return result

    def decide(self, action, command, local=True, active=True, admin=True):
        return self.evaluate("""
rule({id: %s, lookup: function(key) {return key === "command_line" ? %s : undefined;}},
    {local: %s, active: %s, isInGroup: function(group) {return group === "sudo" && %s;}});
""" % tuple(json.dumps(v) for v in (action, command, local, active, admin)))

    def test_archive_software_needs_no_password(self):
        for args in ("install gimp", "remove gimp", "refresh", "upgrade"):
            with self.subTest(args=args):
                self.assertEqual(self.decide("org.zacos9.appstore", f"{HELPER} {args}"), "yes")

    def test_downloaded_package_asks(self):
        for command in (f"{HELPER} install-deb /home/u/Downloads/a.deb",
                        f"{HELPER} install-deb",
                        # a path with spaces can't hide the verb
                        "/tmp/x install y install-deb /tmp/evil.deb"):
            with self.subTest(command=command):
                self.assertEqual(self.decide("org.zacos9.appstore", command), "undefined")

    def test_no_command_line_asks(self):
        self.assertEqual(self.decide("org.zacos9.appstore", None), "undefined")

    def test_only_active_local_administrators(self):
        for local in (False, True):
            for active in (False, True):
                for admin in (False, True):
                    if local and active and admin:
                        continue
                    with self.subTest(local=local, active=active, admin=admin):
                        self.assertEqual(self.decide("org.zacos9.appstore", f"{HELPER} install gimp",
                                                     local, active, admin), "undefined")

    def test_other_actions_untouched(self):
        for action in ("org.zacos9.sharing", "org.freedesktop.policykit.exec", "unrelated.action"):
            with self.subTest(action=action):
                self.assertEqual(self.decide(action, f"{HELPER} install gimp"), "undefined")


if __name__ == "__main__":
    unittest.main()
