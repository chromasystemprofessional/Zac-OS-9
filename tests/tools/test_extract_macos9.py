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
    """Build a valid resource fork from [(type, id, body)].

    Header (data_off, map_off, data_len, map_len), then the data section
    (each resource prefixed by a 4-byte length), then the resource map whose
    reference entries carry each resource's real data offset."""
    data_offset = 256
    data = b""
    offsets = []
    for rtype, res_id, body in resources:
        offsets.append((rtype, res_id, len(data)))
        data += struct.pack(">I", len(body)) + body

    from collections import OrderedDict
    type_counts = OrderedDict()
    for rtype, res_id, body in resources:
        type_counts.setdefault(rtype, 0)
        type_counts[rtype] += 1

    # Type list: num types, then per type: type(4), num-1(2), ref-list offset(2)
    # (offset relative to the start of the type list).
    header_size = 2 + 8 * len(type_counts)
    cursor = header_size
    type_list = struct.pack(">H", len(type_counts) - 1)
    for rtype, count in type_counts.items():
        type_list += struct.pack(">4sHH", rtype, count - 1, cursor)
        cursor += 12 * count

    # Reference lists (no leading count: each is `count` 12-byte entries).
    for rtype, count in type_counts.items():
        for rt, rid, off in offsets:
            if rt != rtype:
                continue
            type_list += struct.pack(">h", rid)
            type_list += struct.pack(">H", 0xFFFF)  # no name
            type_list += bytes([0])                 # attributes
            type_list += struct.pack(">I", off)[1:] # 3-byte data offset
            type_list += struct.pack(">I", 0)       # reserved

    map_size = 28 + len(type_list)
    name_list_offset = 28 + len(type_list)
    map_header = struct.pack(">IIII", data_offset, data_offset + len(data), len(data), map_size)
    map_info = struct.pack(">IHH", 0, 0, 0)       # next map, file ref, attrs
    map_ = map_header + map_info + struct.pack(">HH", 28, name_list_offset) + type_list

    return struct.pack(">IIII", data_offset, data_offset + len(data), len(data), map_size) \
        + b"\x00" * (data_offset - 16) + data + map_


def test_parse():
    fork = build_resource_fork([(b"ICN#", 128, b"\x01\x02\x03")])
    items = tool.parse_resource_fork(fork)
    assert len(items) == 1, f"expected 1 resource, got {len(items)}"
    assert items[0][0] == b"ICN#", "type mismatch"
    assert items[0][2] == b"\x01\x02\x03", "body mismatch"
    print("ok    synthetic resource fork parsed")


def test_end_to_end():
    src = tempfile.mkdtemp()
    with open(os.path.join(src, "a.png"), "wb") as f:
        f.write(b"png")
    # Build a resource fork and save as a file
    fork = build_resource_fork([(b"icns", 128, b"iconbytes"), (b"thme", 1, b"thmedata")])
    with open(os.path.join(src, "Finder"), "wb") as f:
        f.write(fork)
    out = tempfile.mkdtemp() + "/theme"
    r = subprocess.run(
        [sys.executable, sys.argv[1], "--source", src, "--output", out, "--name", "T"],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert os.path.isfile(os.path.join(out, "theme.conf"))
    assert os.path.isfile(os.path.join(out, "icons", "a.png"))
    assert any(fn.startswith("icns_") for fn in os.listdir(os.path.join(out, "icons"))), \
        "resource-fork icon extracted"
    thme_files = [fn for fn in os.listdir(os.path.join(out, "theme")) if fn.startswith("thme_")]
    assert thme_files, "resource-fork theme extracted"
    with open(os.path.join(out, "theme", thme_files[0]), "rb") as f:
        assert f.read() == b"thmedata", "theme body is the full resource"
    print("ok    end-to-end bundle build")


test_parse()
test_end_to_end()
print("OK")