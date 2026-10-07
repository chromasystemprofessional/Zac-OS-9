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
# The real String methods (SubString, CharAt, Length); the rest are mocked.
lib.script_lib_string_setup.argtypes = [ctypes.POINTER(State)]
lib.script_lib_string_setup.restype = ctypes.c_void_p
string_lib = lib.script_lib_string_setup(state)
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
Image.Crop = fun(x, y, w, h) { part = [] | Image; part.width = w; part.height = h; part.crop_x = x; return part; };
Image.Tile = fun(w, h) { tiled = [] | Image; tiled.width = w; tiled.height = h; return tiled; };
Image |= fun(file) {
  image = [] | Image;
  image.width = 64; image.height = 64;
  if (file == "progress-box.png") { image.width = BOX_WIDTH; image.height = BOX_HEIGHT; }
  if (file == "progress-fill.png") { image.width = 1; image.height = BOX_HEIGHT - 4; }
  if (file == "welcome.png") { image.width = 322; image.height = 272; }
  if (file == "bar-fill.png") { image.width = 218; image.height = 10; }
  if (file == "bar-end.png") { image.width = 5; image.height = 10; }
  if (file == "parade.png") { image.width = 32 * 44; image.height = 32; }
  return image;
};
Image.Text = fun(text, r, g, b) { image = [] | Image; image.width = 180; image.height = 12; return image; };
Sprite.SetPosition = fun(x, y, z) { this.x = x; this.y = y; this.z = z; };
Sprite.SetImage = fun(image) { this.image = image; };
Sprite.SetOpacity = fun(opacity) { this.opacity = opacity; };
Sprite |= fun(image) { sprite = [] | Sprite; sprite.image = image; return sprite; };
Plymouth.SetRefreshFunction = fun(callback) { Plymouth.refresh = callback; };
Plymouth.SetBootProgressFunction = fun(callback) { Plymouth.progress = callback; };
Plymouth.SetDisplayPasswordFunction = fun(callback) { Plymouth.password = callback; };
Plymouth.SetDisplayNormalFunction = fun(callback) { Plymouth.normal = callback; };
Plymouth.SetUpdateStatusFunction = fun(callback) { Plymouth.status = callback; };
Plymouth.GetMode = fun() { return MODE; };
failures = 0;
""".replace("BOX_WIDTH", str(box_width)).replace("BOX_HEIGHT", str(box_height))


def check(text, label):
    execute(text, label)
    assert lib.script_obj_hash_get_number(state.contents.global_, b"failures") == 0, label
    print("ok:", label)


try:
    mode = sys.argv[2] if len(sys.argv) > 2 else "shutdown"
    execute(mock.replace("MODE", f'"{mode}"'), "mock-displays")
    execute(theme.read_text(), str(theme))
    if mode == "boot":
        check("""
          failures += screens[0].logo.opacity != 1 || screens[0].welcome.opacity != 0;
          Plymouth.progress(0.2, 0.1); Plymouth.refresh();
          failures += screens[0].logo.opacity != 1 || screens[0].box.opacity != 0;
        """, "boot starts on the logo alone, without the shutdown bar")
        check("""
          Plymouth.status("systemd: Started Something");
          failures += parade_count != 0 || welcome != 0;
          Plymouth.status("zacos9-ext:3");
          failures += welcome != 1 || parade_count != 1;
          failures += screens[0].logo.opacity != 0 || screens[1].welcome.opacity != 1;
          failures += screens[0].pattern.image.width != 1280 || screens[1].pattern.image.height != 1080;
          failures += screens[0].welcome.x != 480 || screens[0].welcome.y != 265;
          failures += screens[1].welcome.x != 1280 + 800 || screens[1].welcome.y != 405;
          failures += screens[0].icons[0].image.crop_x != 96;
          failures += screens[0].icons[0].x != 16 || screens[0].icons[0].y != 800 - 44;
          failures += screens[1].icons[0].x != 1280 + 16 || screens[1].icons[0].y != 1080 - 44;
        """, "a parade status brings up the Welcome screen with the icon on every display")
        check("""
          Plymouth.status("zacos9-ext:x7"); Plymouth.status("zacos9-ext:44");
          failures += parade_count != 1;
          for (n = 0; n < 31; n++) Plymouth.status("zacos9-ext:10");
          failures += parade_count != 32;
          # 1280 wide: 31 icons a row, the 32nd starts a row above.
          failures += screens[0].icons[30].x != 16 + 30 * 40 || screens[0].icons[30].y != 756;
          failures += screens[0].icons[31].x != 16 || screens[0].icons[31].y != 716;
          failures += screens[1].icons[31].x != 1280 + 16 + 31 * 40;
        """, "bad statuses are ignored and the parade wraps into rows")
        check("""
          welcome_fill = 0; Plymouth.progress(5, 0.015); Plymouth.refresh();
          failures += welcome_fill != 0 || screens[0].bar.opacity != 0;
          Plymouth.progress(5, 0.5); Plymouth.refresh();
          fill = Math.Int(0.5 * 0.75 * 218 + 0.5);
          failures += screens[0].bar.image.width != fill || screens[0].bar.opacity != 1;
          failures += screens[0].bar.x != 480 + 51 || screens[0].bar.y != 265 + 233;
          failures += screens[0].bar_end.x != 480 + 51 + fill - 2 || screens[0].bar_end.opacity != 1;
          failures += screens[0].bar_end.image.width != 5;
          Plymouth.progress(5, 1.4); Plymouth.refresh();
          failures += screens[0].bar.image.width != 218 || screens[0].bar_end.opacity != 0;
          welcome_fill = 0; Plymouth.progress(5, 0.997); Plymouth.refresh();
          failures += welcome_fill != 163 || screens[0].bar_end.image.width != 5;
          welcome_fill = 0; Plymouth.progress(5, 216 / 163.5); Plymouth.refresh();
          failures += screens[0].bar_end.image.width != 4;
          Plymouth.password("Unlock", "***");
          failures += screens[0].password.y != 265 + 270 + 16 || screens[0].password.opacity == 0;
          Plymouth.normal();
        """, "the Welcome bar fills to three quarters, the prompt goes under the box")
        sys.exit(0)
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
    lib.script_lib_string_destroy.argtypes = [ctypes.c_void_p]
    lib.script_lib_string_destroy(string_lib)
    lib.script_state_destroy(state)
    for operation in operations:
        lib.script_parse_op_free(operation)
