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
assert settings4.get("alert-sound", "") == "", \
    "the preset names no alert without a decodable sound (and clears an older one)"
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
    if themed[0].get(key):
        assert catalog.match(themed[0][key]), (key, themed[0][key])
print("ok    preset names pattern and wallpaper by catalog id")

# The bundle records the extractor that built it, so a ZacOS update that can
# decode more rebuilds it instead of keeping the old, partial bundle.
with open(os.path.join(bundle, "theme.conf")) as f:
    assert "extractor-sha256=" in f.read(), "bundle records the extractor"
print("ok    the bundle records the extractor that built it")

# Installing a theme with no desktop picture drops the previous one instead
# of leaving it underneath the new chrome.
with open(os.path.join(config3, "zacos9", "desktop.conf"), "a") as f:
    f.write("wallpaper=wallpaper:old:image\npattern=old-pattern\nbackground=wallpaper\n")
r = subprocess.run([binary, "--file", aquapath], env=env3,
                   capture_output=True, text=True, timeout=120)
assert r.returncode == 0, r.stderr
with open(os.path.join(config3, "zacos9", "desktop.conf")) as f:
    conf = f.read()
assert "wallpaper=" not in conf, conf
assert "old-pattern" not in conf, conf
assert "background=pattern" in conf, conf
print("ok    a theme with no desktop picture drops the previous one")

# A desktop picture in any format the Desktop Pictures folder holds installs
# as PNG: the desktop loads wallpapers as PNG only, so JPEG, GIF, TIFF and
# BMP are converted on the way in.
pics = os.path.join(root, "System Folder", "Appearance", "Desktop Pictures")
os.makedirs(pics, exist_ok=True)
def bmp_2x2():
    header = struct.pack("<2sIHHI", b"BM", 54 + 16, 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, 2, 2, 1, 24, 0, 16, 2835, 2835, 0, 0)
    pixels = b"\x00\x00\xff" * 2 + b"\x00\x00" + b"\xff\x00\x00" * 2 + b"\x00\x00"
    return header + info + pixels
with open(os.path.join(pics, "Test Picture"), "wb") as f:
    f.write(bmp_2x2())
scen6 = build_scen([(b"name", b"\x0cTest Picture"),
                    (b"dpnm", b"\x0cTest Picture")])
fork6 = build_fork([
    (b"icl8", 1024, b"\x00" * 1024),
    (b"ICN#", 1024, b"\xff" * 128 + b"\xff" * 128),
    (b"clut", 200, struct.pack(">I", 0) + struct.pack(">HH", 0, 0) + struct.pack(">HHHH", 0, 0, 0, 0)),
    (b"scen", 2000, scen6),
])
picture = os.path.join(src, "Picture Theme")
with open(picture, "wb") as f:
    f.write(fork6)
r = subprocess.run([binary, "--file", picture], env=env3,
                   capture_output=True, text=True, timeout=120)
assert r.returncode == 0, r.stderr
wallpapers = _glob.glob(os.path.join(data3, "zacos9", "appearance", "Wallpaper", "*.png"))
assert wallpapers, "the BMP desktop picture installed as PNG"
with open(os.path.join(config3, "zacos9", "desktop.conf")) as f:
    conf = f.read()
assert "background=wallpaper" in conf, conf
print("ok    a desktop picture in any format installs as PNG")

# Old Dog's System Folder can be copied out of its disk image: the installer
# mounts it read-only through zacos9-mac-disks and keeps the resource forks
# as the .rsrc companions the extractor reads.
mount = tempfile.mkdtemp()
macsf = os.path.join(mount, "System Folder")
os.makedirs(os.path.join(macsf, ".rsrc"))
with open(os.path.join(macsf, "System"), "wb") as f:
    f.write(b"data fork")
with open(os.path.join(macsf, ".rsrc", "System"), "wb") as f:
    f.write(b"resource fork")
pics = os.path.join(macsf, "Appearance", "Desktop Pictures")
os.makedirs(pics)
with open(os.path.join(pics, "Picture"), "wb") as f:
    f.write(b"picture")
home6 = tempfile.mkdtemp()
os.makedirs(os.path.join(home6, "Classic"))
with open(os.path.join(home6, "Classic", "Macintosh HD.dsk"), "wb") as f:
    f.write(b"\x00" * 1024 + b"BD" + b"\x00" * 2048)
fakebin = tempfile.mkdtemp()
tool = os.path.join(fakebin, "zacos9-mac-disks")
with open(tool, "w") as f:
    f.write('#!/bin/sh\nif [ "$1" = mount ]; then\n'
            '  printf \'{"paths":["%s"],"devices":["fake0"]}\\n\' "$FAKE_MOUNT"\nfi\nexit 0\n')
os.chmod(tool, 0o755)
env6 = dict(os.environ)
env6["HOME"] = home6
env6["PATH"] = fakebin + ":" + env6["PATH"]
env6["FAKE_MOUNT"] = mount
env6["QT_QPA_PLATFORM"] = "offscreen"
r = subprocess.run([binary, "--copy-system-folder"], env=env6,
                   capture_output=True, text=True, timeout=120)
assert r.returncode == 0, r.stderr
copied = os.path.join(home6, "Classic", "System Folder")
with open(os.path.join(copied, "System"), "rb") as f:
    assert f.read() == b"data fork", "the data fork is copied"
with open(os.path.join(copied, ".rsrc", "System"), "rb") as f:
    assert f.read() == b"resource fork", "the resource fork lands in .rsrc"
assert os.path.isfile(os.path.join(copied, "Appearance", "Desktop Pictures", "Picture"))
print("ok    Old Dog's System Folder copies out of its disk with its forks")

# 7. A batch import (the desktop's drop path) with no System Folder copies
# one from Old Dog's disk first, so a theme such as Apple Platinum still
# gets its Apple menu icon instead of the theme's own document icon.
mount7 = tempfile.mkdtemp()
macsf7 = os.path.join(mount7, "System Folder")
os.makedirs(os.path.join(macsf7, ".rsrc"))
with open(os.path.join(macsf7, "System"), "wb") as f:
    f.write(b"data fork")
clut7 = struct.pack(">I", 0) + struct.pack(">HH", 0, 0)
for i in range(256):
    clut7 += struct.pack(">HHHH", i, i, i, i)
apple7 = build_fork([
    (b"ics8", -16386, b"\x07" * 256),
    (b"ics#", -16386, b"\x00" * 64),
    (b"clut", 9, clut7),
])
with open(os.path.join(macsf7, ".rsrc", "System"), "wb") as f:
    f.write(apple7)
home7 = tempfile.mkdtemp()
os.makedirs(os.path.join(home7, "Classic"))
with open(os.path.join(home7, "Classic", "Macintosh HD.dsk"), "wb") as f:
    f.write(b"\x00" * 1024 + b"BD" + b"\x00" * 2048)
fakebin7 = tempfile.mkdtemp()
tool7 = os.path.join(fakebin7, "zacos9-mac-disks")
with open(tool7, "w") as f:
    f.write('#!/bin/sh\nif [ "$1" = mount ]; then\n'
            '  printf \'{"paths":["%s"],"devices":["fake0"]}\\n\' "$FAKE_MOUNT"\nfi\nexit 0\n')
os.chmod(tool7, 0o755)
env7 = dict(os.environ)
config7 = tempfile.mkdtemp()
data7 = tempfile.mkdtemp()
env7["HOME"] = home7
env7["XDG_CONFIG_HOME"] = config7
env7["XDG_DATA_HOME"] = data7
env7["PATH"] = fakebin7 + ":" + env7["PATH"]
env7["FAKE_MOUNT"] = mount7
env7["QT_QPA_PLATFORM"] = "offscreen"
theme7 = make_theme_file(tempfile.mkdtemp())
r = subprocess.run([binary, "--file", theme7], env=env7,
                   capture_output=True, text=True, timeout=120)
assert r.returncode == 0, r.stderr
copied7 = os.path.join(home7, "Classic", "System Folder")
assert os.path.isfile(os.path.join(copied7, ".rsrc", "System")), \
    "the batch import copied the System Folder"
bundle7 = os.path.join(config7, "zacos9", "themes", "Apple platinum")
assert os.path.isfile(os.path.join(bundle7, "apple-menu.png")), \
    "the Apple menu icon came from the copied System file"
assert os.path.isfile(os.path.join(bundle7, "menu-icon.png")), "menu icon written"
print("ok    a batch drop import copies the System Folder and keeps the Apple")

# 7b. A partial System Folder of the user's own is never replaced by the
# automatic batch copy; it is left alone and the theme still installs.
home7b = tempfile.mkdtemp()
partial = os.path.join(home7b, "Classic", "System Folder")
os.makedirs(partial)
with open(os.path.join(partial, "Keep Me"), "wb") as f:
    f.write(b"user data")
with open(os.path.join(home7b, "Classic", "Macintosh HD.dsk"), "wb") as f:
    f.write(b"\x00" * 1024 + b"BD" + b"\x00" * 2048)
env7b = dict(env7)
env7b["HOME"] = home7b
env7b["XDG_CONFIG_HOME"] = tempfile.mkdtemp()
env7b["XDG_DATA_HOME"] = tempfile.mkdtemp()
r = subprocess.run([binary, "--file", theme7], env=env7b,
                   capture_output=True, text=True, timeout=120)
assert r.returncode == 0, r.stderr
assert os.path.isfile(os.path.join(partial, "Keep Me")), \
    "the batch copy did not replace the user's partial System Folder"
assert not os.path.isfile(os.path.join(partial, ".rsrc", "System"))
bundle7b = os.path.join(env7b["XDG_CONFIG_HOME"], "zacos9", "themes", "Apple platinum")
assert not os.path.isfile(os.path.join(bundle7b, "apple-menu.png")), \
    "without a usable System file the theme keeps its own icon"
print("ok    a batch import leaves a partial System Folder alone")

print("OK")
