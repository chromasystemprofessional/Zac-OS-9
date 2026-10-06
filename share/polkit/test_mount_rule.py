"""Execute the mount rule with mocked subjects; never change host policy."""
import ctypes
import ctypes.util
import json
from pathlib import Path
import sys
import unittest


rule_path = Path(sys.argv.pop(1))


class MountAuthorizationTests(unittest.TestCase):
    def test_authorization_scope(self):
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

        def evaluate(source):
            runtime.duk_get_global_string(context, b"eval")
            encoded = source.encode()
            runtime.duk_push_lstring(context, encoded, len(encoded))
            status = runtime.duk_pcall(context, 1)
            result = runtime.duk_safe_to_lstring(context, -1, None).decode()
            runtime.duk_pop(context)
            self.assertEqual(status, 0, result)
            return result

        evaluate("""
var rule;
var polkit = {Result: {YES: "yes"}, addRule: function(callback) {rule = callback;}};
""" + rule_path.read_text())
        granted = {
            "org.freedesktop.udisks2.filesystem-mount-system",
            "org.freedesktop.udisks2.filesystem-mount",
            "org.zacos9.disks.mac-mount",
        }
        for action in sorted(granted | {
            "org.zacos9.disks", "org.freedesktop.udisks2.encrypted-unlock",
            "org.freedesktop.udisks2.filesystem-mount-other-user",
            "org.freedesktop.policykit.exec", "unrelated.action",
        }):
            for local in (False, True):
                for active in (False, True):
                    for admin in (False, True):
                        with self.subTest(action=action, local=local, active=active, admin=admin):
                            result = evaluate("""
rule({id: %s}, {local: %s, active: %s,
    isInGroup: function(group) {return group === "sudo" && %s;}});
""" % tuple(json.dumps(value) for value in (action, local, active, admin)))
                            expected = "yes" if action in granted and local and active and admin else "undefined"
                            self.assertEqual(result, expected)


if __name__ == "__main__":
    unittest.main()
