#!/usr/bin/python3
"""Run the theme in Plymouth's real interpreter with isolated display/sprite mocks."""
import ctypes
import glob
import struct
import sys
from pathlib import Path


class State(ctypes.Structure):
    _fields_ = [(name, ctypes.c_void_p) for name in ("user_data", "global_", "local", "this")]


class Result(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("object", ctypes.c_void_p)]


plugins = glob.glob("/usr/lib/*/plymouth/script.so") + glob.glob("/usr/lib/plymouth/script.so")
if not plugins:
    print("SKIP: Plymouth's script interpreter is not installed")
    sys.exit(77)

lib = ctypes.CDLL(plugins[0])
lib.script_state_new.argtypes = [ctypes.c_void_p]
lib.script_state_new.restype = ctypes.POINTER(State)
lib.script_parse_string.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
lib.script_parse_string.restype = ctypes.c_void_p
lib.script_execute.argtypes = [ctypes.POINTER(State), ctypes.c_void_p]
lib.script_execute.restype = Result
lib.script_obj_unref.argtypes = [ctypes.c_void_p]
lib.script_parse_op_free.argtypes = [ctypes.c_void_p]
lib.script_state_destroy.argtypes = [ctypes.POINTER(State)]
lib.script_obj_hash_get_number.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.script_obj_hash_get_number.restype = ctypes.c_double

state = lib.script_state_new(None)
operations = []


def execute(text, name):
    op = lib.script_parse_string(text.encode(), name.encode())
    assert op, f"{name}: script failed to parse"
    operations.append(op)
    result = lib.script_execute(state, op)
    assert result.type == 0, f"{name}: interpreter execution failed"
    lib.script_obj_unref(result.object)


theme = Path(sys.argv[1])
box_width, box_height = struct.unpack(">II", (theme.parent / "progress-box.png").read_bytes()[16:24])
mock = """
widths = [1280, 1920];
heights = [800, 1080];
viewport_x = [];
viewport_y = [];
Window.GetWidth = fun(i) { return widths[i]; };
Window.GetHeight = fun(i) { return heights[i]; };
Window.SetX = fun(i, value) { viewport_x[i] = value; };
Window.SetY = fun(i, value) { viewport_y[i] = value; };
Window.SetBackgroundTopColor = fun(r, g, b) {};
Window.SetBackgroundBottomColor = fun(r, g, b) {};
Math.Int = fun(value) { return value - (value % 1); };
Image.GetWidth = fun() { return this.width; };
Image.GetHeight = fun() { return this.height; };
Image.Scale = fun(w, h) { scaled = [] | Image; scaled.width = w; scaled.height = h; return scaled; };
Image |= fun(file) {
  image = [] | Image;
  image.width = 64; image.height = 64;
  if (file == "progress-box.png") { image.width = BOX_WIDTH; image.height = BOX_HEIGHT; }
  if (file == "progress-fill.png") { image.width = 1; image.height = BOX_HEIGHT - 4; }
  return image;
};
Image.Text = fun(text, r, g, b) { image = [] | Image; image.width = 180; image.height = 12; return image; };
Sprite.SetPosition = fun(x, y, z) { this.x = x; this.y = y; this.z = z; };
Sprite.SetImage = fun(image) { this.image = image; };
Sprite |= fun(image) { sprite = [] | Sprite; sprite.image = image; return sprite; };
Plymouth.SetRefreshFunction = fun(callback) { Plymouth.refresh = callback; };
Plymouth.SetBootProgressFunction = fun(callback) { Plymouth.progress = callback; };
Plymouth.SetDisplayPasswordFunction = fun(callback) { Plymouth.password = callback; };
Plymouth.SetDisplayNormalFunction = fun(callback) { Plymouth.normal = callback; };
failures = 0;
""".replace("BOX_WIDTH", str(box_width)).replace("BOX_HEIGHT", str(box_height))


def check(text, label):
    execute(text, label)
    assert lib.script_obj_hash_get_number(state.contents.global_, b"failures") == 0, label
    print("ok:", label)


try:
    execute(mock, "mock-displays")
    execute(theme.read_text(), str(theme))
    check("""
      failures += screen_count != 2;
      failures += viewport_x[0] != 0 || viewport_x[1] != 1280;
      failures += screens[0].logo.x != 608 || screens[0].logo.y != 368;
      failures += screens[1].logo.x != 2208 || screens[1].logo.y != 508;
    """, "independently center splash on unequal displays without sprite overlap")
    check("""
      Plymouth.progress(1, 0.5); Plymouth.refresh();
      failures += screens[0].fill.image.GetWidth() != Math.Int(fill_inner * 0.5);
      failures += screens[1].fill.image.GetWidth() != screens[0].fill.image.GetWidth();
      Plymouth.password("Unlock", "***");
      failures += screens[0].password.x != 550 || screens[1].password.x != 2150;
      failures += screens[0].password.image == NULL || screens[1].password.image == NULL;
      Plymouth.normal();
      failures += screens[0].password.image != NULL || screens[1].password.image != NULL;
    """, "progress and password prompts are replicated on both monitors")
    check("""
      widths = [1024]; heights = [768]; Plymouth.refresh();
      failures += screen_count != 1 || screens[1] != NULL;
      failures += screens[0].logo.x != 480 || screens[0].logo.y != 352;
      widths = [1024, 1600]; heights = [768, 900]; Plymouth.refresh();
      failures += screen_count != 2;
      failures += screens[1].logo.x != 1792 || screens[1].logo.y != 418;
      failures += screens[1].fill.image.GetWidth() != Math.Int(fill_inner * 0.5);
    """, "resize, hot-unplug and hot-plug reposition complete splash artwork")
finally:
    lib.script_state_destroy(state)
    for operation in operations:
        lib.script_parse_op_free(operation)
