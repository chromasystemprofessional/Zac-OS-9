"""Execute 50-zacos9-power.rules with mocked subjects; never change host policy.

usage: test_power_rule.py RULE
"""
import ctypes
import ctypes.util
import json
from pathlib import Path
import sys
import unittest

rule_path = Path(sys.argv.pop(1))


class PowerRuleTests(unittest.TestCase):
    def setUp(self):
        library = ctypes.util.find_library("duktape")
        if not library:
            self.skipTest("The polkit JavaScript runtime (libduktape) is unavailable")
        rt = ctypes.CDLL(library)
        rt.duk_create_heap.argtypes = [ctypes.c_void_p] * 5
        rt.duk_create_heap.restype = ctypes.c_void_p
        rt.duk_destroy_heap.argtypes = [ctypes.c_void_p]
        rt.duk_get_global_string.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        rt.duk_push_lstring.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
        rt.duk_pcall.argtypes = [ctypes.c_void_p, ctypes.c_int]
        rt.duk_safe_to_lstring.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_size_t)]
        rt.duk_safe_to_lstring.restype = ctypes.c_char_p
        rt.duk_pop.argtypes = [ctypes.c_void_p]
        self.ctx = rt.duk_create_heap(None, None, None, None, None)
        self.addCleanup(rt.duk_destroy_heap, self.ctx)
        self.rt = rt
        self.run_js("var rule; var polkit = {Result: {YES: 'yes'}, addRule: function(f) {rule = f;}};\n"
                    + rule_path.read_text())

    def run_js(self, source):
        self.rt.duk_get_global_string(self.ctx, b"eval")
        data = source.encode()
        self.rt.duk_push_lstring(self.ctx, data, len(data))
        status = self.rt.duk_pcall(self.ctx, 1)
        result = self.rt.duk_safe_to_lstring(self.ctx, -1, None).decode()
        self.rt.duk_pop(self.ctx)
        self.assertEqual(status, 0, result)
        return result

    def decide(self, action, user):
        return self.run_js("rule({id: %s}, {user: %s, local: false, active: false, "
                           "isInGroup: function() {return false;}});" % (json.dumps(action), json.dumps(user)))

    def test_login_window_may_shut_down_and_restart(self):
        for verb in ("power-off", "power-off-multiple-sessions", "reboot", "reboot-multiple-sessions"):
            with self.subTest(verb=verb):
                self.assertEqual(self.decide("org.freedesktop.login1." + verb, "_greetd"), "yes")

    def test_not_past_programs_holding_it_off(self):
        for verb in ("power-off-ignore-inhibit", "reboot-ignore-inhibit", "suspend", "set-wall-message"):
            with self.subTest(verb=verb):
                self.assertEqual(self.decide("org.freedesktop.login1." + verb, "_greetd"), "undefined")

    def test_nobody_else(self):
        for user in ("adam", "root", "greeter", ""):
            with self.subTest(user=user):
                self.assertEqual(self.decide("org.freedesktop.login1.power-off", user), "undefined")


if __name__ == "__main__":
    unittest.main()
