import importlib.util
import re
import os
import struct
import subprocess
import sys
import tempfile

binary = sys.argv[1]

# A valid HFS resource fork: 16-byte header, the resource data section
# (each resource prefixed by a 4-byte length), then the resource map whose
# reference entries carry each resource's real data offset.
def build_fork(resources):
    data_offset = 256
    data = b""
    offsets = []
    for rtype, res_id, body in resources:
        offsets.append((rtype, res_id, len(data)))
        data += struct.pack(">I", len(body)) + body

    from collections import OrderedDict
    counts = OrderedDict()
    for rtype, res_id, body in resources:
        counts.setdefault(rtype, 0)
        counts[rtype] += 1

    header_size = 2 + 8 * len(counts)
    cursor = header_size
    type_list = struct.pack(">H", len(counts) - 1)
    for rtype, n in counts.items():
        type_list += struct.pack(">4sHH", rtype, n - 1, cursor)
        cursor += 12 * n
    for rtype, n in counts.items():
        for rt, rid, off in offsets:
            if rt != rtype:
                continue
            type_list += struct.pack(">h", rid)
            type_list += struct.pack(">H", 0xFFFF)
            type_list += bytes([0])
            type_list += struct.pack(">I", off)[1:]
            type_list += struct.pack(">I", 0)

    map_offset = data_offset + len(data)
    map_size = 28 + len(type_list)
    header = struct.pack(">IIII", data_offset, map_offset, len(data), map_size)
    name_list = 28 + len(type_list)
    return header + b"\x00" * (data_offset - 16) + data + \
        struct.pack(">IIII", data_offset, map_offset, len(data), map_size) + \
        struct.pack(">IHH", 0, 0, 0) + struct.pack(">HH", 28, name_list) + type_list


def make_theme_file(directory):
    icl8 = b"\x00" * 1024
    icn = b"\xff" * 128 + b"\xff" * 128
    clut = struct.pack(">I", 0) + struct.pack(">HH", 0, 0)
    for i in range(1):
        clut += struct.pack(">HHHH", i, i * 255, i * 255, i * 255)
    fork = build_fork([(b"icl8", 1024, icl8), (b"ICN#", 1024, icn), (b"clut", 200, clut)])
    path = os.path.join(directory, "Apple platinum copy")
    with open(path, "wb") as f:
        f.write(fork)
    return path


def run(binary, file, name, env):
    return subprocess.run([binary, "--file", file, "--name", name],
                          env=env, capture_output=True, text=True, timeout=60)


# 1. A file with no Mac OS 9 artwork: the installer says so.
empty = tempfile.mkdtemp()
with open(os.path.join(empty, "a.png"), "wb") as f:
    f.write(b"png")
env = dict(os.environ)
env["XDG_CONFIG_HOME"] = tempfile.mkdtemp()
env["XDG_DATA_HOME"] = tempfile.mkdtemp()
env["HOME"] = tempfile.mkdtemp()
env["QT_QPA_PLATFORM"] = "offscreen"
r = run(binary, os.path.join(empty, "a.png"), "Test", env)
assert r.returncode != 0, "an artless file is refused"
assert "no Mac OS 9 artwork" in r.stderr, "the message names the cause"
print("ok    artless file is refused with instructions")

# 2. A theme file's resource fork: the installer succeeds and names it.
src = tempfile.mkdtemp()
theme = make_theme_file(src)
env2 = dict(os.environ)
config = tempfile.mkdtemp()
data = tempfile.mkdtemp()
env2["XDG_CONFIG_HOME"] = config
env2["XDG_DATA_HOME"] = data
env2["HOME"] = tempfile.mkdtemp()
env2["QT_QPA_PLATFORM"] = "offscreen"
# No --name: the name is inherited from the file ("Apple platinum").
r = subprocess.run([binary, "--file", theme], env=env2,
                   capture_output=True, text=True, timeout=60)
assert r.returncode == 0, r.stderr
out = os.path.join(config, "zacos9", "themes", "Apple platinum")
assert os.path.isfile(os.path.join(out, "menu-icon.png")), "menu icon written"
assert os.path.isfile(os.path.join(out, "theme.conf")), "theme.conf written"

# 2b. Version dots survive naming; a leading ._ is stripped; " copy" is
# dropped. NeXTheme 1.0.1 and 1.0.2 must land in distinct bundles.
def theme_bytes():
    icl8 = b"\x00" * 1024
    icn = b"\xff" * 128 + b"\xff" * 128
    clut = struct.pack(">I", 0) + struct.pack(">HH", 0, 0)
    clut += struct.pack(">HHHH", 0, 0, 0, 0)
    return build_fork([(b"icl8", 1024, icl8), (b"ICN#", 1024, icn), (b"clut", 200, clut)])

envN = dict(os.environ)
configN = tempfile.mkdtemp()
dataN = tempfile.mkdtemp()
envN["XDG_CONFIG_HOME"] = configN
envN["XDG_DATA_HOME"] = dataN
envN["QT_QPA_PLATFORM"] = "offscreen"
srcdir = tempfile.mkdtemp()
for filename, expected in [("NeXTheme 1.0.1", "NeXTheme 1.0.1"),
                           ("NeXTheme 1.0.2", "NeXTheme 1.0.2"),
                           ("Aqua v2.0.1", "Aqua v2.0.1"),
                           ("._Foo", "Foo"),
                           ("Bar copy", "Bar")]:
    with open(os.path.join(srcdir, filename), "wb") as f:
        f.write(theme_bytes())
    r = subprocess.run([binary, "--file", os.path.join(srcdir, filename)], env=envN,
                       capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, (filename, r.stderr)
    out = os.path.join(configN, "zacos9", "themes", expected)
    assert os.path.isfile(os.path.join(out, "theme.conf")), "bundle kept full name: " + expected
assert os.path.isdir(os.path.join(configN, "zacos9", "themes", "NeXTheme 1.0.1")), \
    "1.0.1 keeps its own bundle"
assert os.path.isdir(os.path.join(configN, "zacos9", "themes", "NeXTheme 1.0.2")), \
    "1.0.2 keeps its own bundle"
assert not os.path.exists(os.path.join(configN, "zacos9", "themes", "NeXTheme 1.0")), \
    "no version-stripped bundle collides"
print("ok    theme names keep version dots and drop ._ prefixes")
# 3. A Mac OS 9 tree: fonts install, the program font applies, and the
# theme works without the real ~/Classic (HOME points at the temp tree).
def build_scen(items):
    header = struct.pack(">III", 0x00010000, 0x40000000, len(items))
    headers, pool = b"", b""
    for tag, value in items:
        headers += struct.pack(">4sIII", tag, 0, 0x40000000, len(pool))
        pool += value
    return header + headers + struct.pack(">I", len(pool)) + pool


def build_sfnt(family, subfamily):
    recs, strings, off = b"", b"", 0
    for name_id, text in ((1, family), (2, subfamily)):
        raw = text.encode("utf-16-be")
        recs += struct.pack(">HHHHHH", 3, 1, 0x409, name_id, len(raw), off)
        strings += raw
        off += len(raw)
    table = struct.pack(">HHH", 0, 2, 6 + 24) + recs + strings
    head = struct.pack(">II", 0x00010000, 0) + struct.pack(">I", 0) + \
        struct.pack(">IHH", 0x5F0F3CF5, 0, 2048) + b"\x00" * 16 + \
        struct.pack(">hhhhhhhhh", 0, 0, 0, 0, 0, 8, 2, 0, 0)
    tables = [(b"head", head), (b"name", table)]
    out = struct.pack(">IHHHH", 0x00010000, len(tables), 0, 0, 0)
    pos = 12 + 16 * len(tables)
    for tag, body in tables:
        out += tag + struct.pack(">III", 0, pos, len(body))
        pos += len(body)
    for _tag, body in tables:
        out += body
    return out


root = tempfile.mkdtemp()
sysdir = os.path.join(root, "System Folder", ".rsrc")
os.makedirs(sysdir)
apple = build_fork([
    (b"ics8", -16386, b"\x07" * 256),
    (b"ics#", -16386, b"\x00" * 64),
])
with open(os.path.join(sysdir, "System"), "wb") as f:
    f.write(apple)
fontsdir = os.path.join(root, "System Folder", "Fonts")
os.makedirs(fontsdir)
with open(os.path.join(fontsdir, "Charcoal"), "wb") as f:
    f.write(build_fork([(b"sfnt", 1, build_sfnt("Charcoal", "Regular"))]))
themedir = os.path.join(root, "System Folder", "Appearance", "Theme Files")
os.makedirs(themedir)
scen = build_scen([
    (b"name", b"\x0bTest Theme"),
    (b"lgsf", b"\x08Charcoal"),
])
theme2 = build_fork([
    (b"icl8", 1024, b"\x00" * 1024),
    (b"ICN#", 1024, b"\xff" * 128 + b"\xff" * 128),
    (b"clut", 200, struct.pack(">I", 0) + struct.pack(">HH", 0, 0) + struct.pack(">HHHH", 0, 0, 0, 0)),
    (b"scen", 2000, scen),
])
aquapath = os.path.join(themedir, "Aqua")
with open(aquapath, "wb") as f:
    f.write(theme2)
env3 = dict(os.environ)
config3 = tempfile.mkdtemp()
data3 = tempfile.mkdtemp()
env3["HOME"] = root
env3["XDG_CONFIG_HOME"] = config3
env3["XDG_DATA_HOME"] = data3
env3["QT_QPA_PLATFORM"] = "offscreen"
r = subprocess.run([binary, "--file", aquapath], env=env3,
                   capture_output=True, text=True, timeout=120)
assert r.returncode == 0, r.stderr
bundle = os.path.join(config3, "zacos9", "themes", "Aqua")
assert os.path.isfile(os.path.join(bundle, "fonts", "Charcoal.ttf")), "decoded face kept"
installed = os.path.join(data3, "fonts", "zacos9", "Charcoal.ttf")
assert os.path.isfile(installed), "program fonts install where fontconfig looks"
with open(os.path.join(bundle, "theme.conf")) as f:
    conf = f.read()
assert "font-families=Charcoal" in conf, "bundle records its families"
import glob as _glob
import json as _json
presets = _glob.glob(os.path.join(data3, "zacos9", "appearance", "Theme Files", "*.json"))
assert len(presets) == 1, presets
with open(presets[0]) as f:
    settings = _json.load(f)["settings"]
assert settings.get("font-family") == "Charcoal", "theme font picked from system font"
with open(os.path.join(config3, "zacos9", "desktop.conf")) as f:
    assert "font-family=Charcoal" in f.read(), "program font applies at install"
print("ok    imported fonts install and the program font applies")

# 3b. A theme file inside Theme Files keeps its preset next to itself.
import shutil as _shutil
inner = os.path.join(data3, "zacos9", "appearance", "Theme Files", "Aqua")
os.makedirs(inner)
_shutil.copy(aquapath, os.path.join(inner, "Aqua"))
env3b = dict(env3)
r = subprocess.run([binary, "--file", os.path.join(inner, "Aqua")], env=env3b,
                   capture_output=True, text=True, timeout=120)
assert r.returncode == 0, r.stderr
inner_presets = _glob.glob(os.path.join(inner, "*.json"))
assert len(inner_presets) == 1, inner_presets
assert len(_glob.glob(os.path.join(data3, "zacos9", "appearance", "Theme Files", "*.json"))) == 1, \
    "the earlier out-of-tree preset stays the only one in the root"
with open(inner_presets[0]) as f:
    assert _json.load(f)["name"] == "Aqua"
bundle_icon = os.path.join(config3, "zacos9", "themes", "Aqua", "file-icon.png")
assert os.path.isfile(bundle_icon), "the theme's own icon rides along for the Sniffer"
print("ok    the preset lives with its theme, with its icon")

# 4. Raw resource dumps (*.bin) never install as desktop assets: the
# preset references only decodable PNGs and WAVs.
src4 = tempfile.mkdtemp()
junkppat = b"\x00" * 100
fork4 = build_fork([
    (b"icl8", 1024, b"\x00" * 1024),
    (b"ICN#", 1024, b"\xff" * 128 + b"\xff" * 128),
    (b"clut", 200, struct.pack(">I", 0) + struct.pack(">HH", 0, 0) + struct.pack(">HHHH", 0, 0, 0, 0)),
    (b"PAT ", 100, b"\xaa" * 8),
    (b"ppat", 1000, junkppat),
    (b"snd ", 1, b"junk"),
])
theme4 = os.path.join(src4, "Filter")
with open(theme4, "wb") as f:
    f.write(fork4)
env4 = dict(os.environ)
config4 = tempfile.mkdtemp()
data4 = tempfile.mkdtemp()
env4["XDG_CONFIG_HOME"] = config4
env4["XDG_DATA_HOME"] = data4
env4["QT_QPA_PLATFORM"] = "offscreen"
r = subprocess.run([binary, "--file", theme4], env=env4,
                   capture_output=True, text=True, timeout=120)
assert r.returncode == 0, r.stderr
bundle4 = os.path.join(config4, "zacos9", "themes", "Filter")
assert os.path.isfile(os.path.join(bundle4, "patterns", "ppat_1000.bin")), \
    "raw dumps stay in the bundle for reference"
installed_patterns = os.listdir(os.path.join(data4, "zacos9", "appearance", "Desktop Patterns"))
assert installed_patterns, "the decodable PAT installs"
assert all(p.endswith(".png") for p in installed_patterns), \
    f"no raw dumps install as patterns: {installed_patterns}"
installed_sounds = os.listdir(os.path.join(data4, "zacos9", "sounds"))
assert all(p.endswith(".wav") for p in installed_sounds), \
    f"no raw dumps install as sounds: {installed_sounds}"
with open(_glob.glob(os.path.join(data4, "zacos9", "appearance", "Theme Files", "*.json"))[0]) as f:
    settings4 = _json.load(f)["settings"]
assert re.fullmatch(r"user:[0-9a-f]{64}:image", settings4.get("pattern", "") or ""), \
    f"preset references the decoded tile by catalog id: {settings4}"
assert "alert-sound" not in settings4, "no alert without a decodable sound"
with open(os.path.join(config4, "zacos9", "desktop.conf")) as f:
    desktop = {}
    for line in f:
        if "=" in line and not line.lstrip().startswith(("[", ";", "#")):
            key, _, value = line.partition("=")
            desktop[key.strip()] = value.strip()
assert re.fullmatch(r"user:[0-9a-f]{64}:image", desktop.get("pattern", "") or ""), \
    f"installing applies the theme's desktop pattern by catalog id: {desktop}"
print("ok    raw dumps stay out of the installed assets")

# The installer applies the desktop settings itself: theme.conf and
# desktop.conf agree, and the background falls back to pattern.
def conf_dict(path):
    out = {}
    with open(path) as f:
        for line in f.read().splitlines():
            if "=" in line:
                k, v = line.split("=", 1)
                out[k.strip()] = v.strip()
    return out
conf = conf_dict(os.path.join(out, "theme.conf"))
desktop = conf_dict(os.path.join(config, "zacos9", "desktop.conf"))
assert desktop.get("theme-bundle") == "Apple platinum", desktop
assert desktop.get("background") == "pattern", desktop
assert "background" not in conf, conf  # no desktop art in this theme
assert "sound-theme" not in desktop, desktop  # its sound set is not installed
print("ok    installed theme is applied to theme.conf and desktop.conf")

# The preset names pattern and wallpaper by the Appearance panel's catalog ids:
# the prefix, the SHA-256 of the file name, then ":image".
import glob, json, re
presets = [json.load(open(f)) for f in glob.glob(os.path.join(data, "zacos9", "appearance", "Theme Files", "**", "*.json"), recursive=True)]
themed = [p["settings"] for p in presets if p["settings"].get("theme-bundle") == "Apple platinum"]
assert themed, "a preset for the theme"
catalog = re.compile(r"\A(user|wallpaper):[0-9a-f]{64}:image\Z")
for key in ("pattern", "wallpaper"):
    if key in themed[0]:
        assert catalog.match(themed[0][key]), (key, themed[0][key])
print("ok    preset names pattern and wallpaper by catalog id")

print("OK")
