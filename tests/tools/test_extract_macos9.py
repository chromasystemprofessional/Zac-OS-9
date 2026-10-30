import importlib.util
import os
import struct
import subprocess
import sys
import tempfile

# Load the tool as a module to test parse_resource_fork directly.
spec = importlib.util.spec_from_file_location("extract", sys.argv[1])
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def build_resource_fork(resources):
    """Build a minimal valid resource fork from [(type, id, body)]."""
    # Lay resource data out first; each body is prefixed by a 4-byte length.
    data = b""
    refs = {}  # type -> list of (id, data_offset)
    for rtype, res_id, body in resources:
        off = len(data)
        data += struct.pack(">I", len(body)) + body
        refs.setdefault(rtype, []).append((res_id, off))

    # Reference lists and type list, laid out after the data.
    type_list = b""
    ref_blobs = {}
    # We need offsets relative to the type list start, computed after we know sizes.
    # Build reference lists first.
    for rtype, entries in refs.items():
        ref = struct.pack(">H", len(entries) - 1)
        for res_id, off in entries:
            ref += struct.pack(">HH", res_id, 0xFFFF)  # id, no name
            ref += struct.pack(">I", off)              # data offset (attrs 0)
            ref += struct.pack(">I", 0)                # reserved handle
        ref_blobs[rtype] = ref

    # Type list: count, then per type (type, num_res, offset to ref list).
    # Offsets within the type list are relative to the type list start.
    type_list = struct.pack(">H", len(ref_blobs) - 1)
    header_size = 2 + 8 * len(ref_blobs)
    cursor = header_size
    for rtype, entries in refs.items():
        ref_off = cursor
        type_list += struct.pack(">4sH H", rtype, len(entries) - 1, ref_off)
        cursor += len(ref_blobs[rtype])
    for rtype in refs:
        type_list += ref_blobs[rtype]

    # Resource map: 16-byte header copy, then 8 bytes (next map, file ref,
    # attrs), then type-list and name-list offsets, then the lists themselves.
    type_list_offset = 28  # right after the 28-byte map header
    name_list_offset = type_list_offset + len(type_list)
    map_size = 28 + len(type_list)
    map_offset = len(data)  # the map follows the data

    header = struct.pack(">IIII", 0, map_offset, len(data), map_size)
    map_info = struct.pack(">I H H", 0, 0, 0)  # next map, file ref, attrs
    map_ = (header + map_info
            + struct.pack(">HH", type_list_offset, name_list_offset)
            + type_list)

    return data + map_ + struct.pack(">I", map_offset)


def test_parse():
    fork = build_resource_fork([
        (b"ICN#", 128, b"\x01\x02\x03"),
        (b"snd ", 1, b"\x00\x00\x00\x04SOUND"),
        (b"PAT ", 0, b"\xaa\xbb"),
    ])
    items = tool.parse_resource_fork(fork)
    got = {(t, i): b for t, i, b in items}
    assert got[(b"ICN#", 128)] == b"\x01\x02\x03", "icon resource extracted"
    assert got[(b"snd ", 1)] == b"\x00\x00\x00\x04SOUND", "sound resource extracted"
    assert got[(b"PAT ", 0)] == b"\xaa\xbb", "pattern resource extracted"
    print("ok    synthetic resource fork parsed")


def test_end_to_end():
    src = tempfile.mkdtemp()
    with open(os.path.join(src, "a.png"), "wb") as f:
        f.write(b"png")
    fork = build_resource_fork([(b"icns", 128, b"iconbytes")])
    with open(os.path.join(src, "Finder"), "wb") as f:
        f.write(fork)
    out = tempfile.mkdtemp() + "/theme"
    r = subprocess.run(
        [sys.executable, sys.argv[1], "--source", src, "--output", out, "--name", "T"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert os.path.isfile(os.path.join(out, "theme.conf"))
    assert os.path.isfile(os.path.join(out, "icons", "a.png"))
    assert any(fn.startswith("icns_128") for fn in os.listdir(os.path.join(out, "icons"))), \
        "resource-fork icon extracted"
    print("ok    end-to-end bundle build")


test_parse()
test_end_to_end()
print("OK")
