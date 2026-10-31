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


def build_scen(items):
    """Build a flattened Appearance collection from [(tag, value_bytes)]."""
    import struct as _s
    header = _s.pack(">III", 0x00010000, 0x40000000, len(items))
    headers = b""
    pool = b""
    offs = []
    for tag, value in items:
        offs.append(len(pool))
        headers += _s.pack(">4sIII", tag, 0, 0x40000000, len(pool))
        pool += value
    return header + headers + _s.pack(">I", len(pool)) + pool


def test_clut_positional():
    import importlib.util as _iu
    spec = _iu.spec_from_file_location("macicons", os.path.join(os.path.dirname(sys.argv[1]), "macicons.py"))
    mi = _iu.module_from_spec(spec); spec.loader.exec_module(mi)
    # System-file colour tables leave the ctEntry index 0 and use order.
    lookup = mi.clut_lookup([(0, (255, 0, 0)), (0, (0, 255, 0)), (0, (0, 0, 255))])
    assert lookup == [(255, 0, 0), (0, 255, 0), (0, 0, 255)], lookup
    print("ok    positional clut lookup")


def test_wctb():
    w = struct.pack(">6sH", b"\x00" * 6, 1) + struct.pack(">HHHH", 1, 0xFFFF, 0xCCCC, 0x9999) \
        + struct.pack(">HHHH", 7, 0xEEEE, 0xEEEE, 0xEEEE)
    out = tool.decode_wctb(w)
    assert out["window-frame"] == "ffcc99", out
    assert out["window-titlebar-light"] == "eeeeee", out
    print("ok    window colour table decoded")


def test_scen():
    data = build_scen([
        (b"name", b"\x0bConvergence"),
        (b"coll", b"\x00"),
        (b"smoo", b"\x01"),
        (b"vfsz", struct.pack(">h", 10)),
        (b"smos", struct.pack(">H", 12)),
        (b"hcol", struct.pack(">HHH", 0x9999, 0xCCCC, 0xFFFF)),
        (b"sbar", struct.pack(">H", 1)),
        (b"sbth", struct.pack(">H", 0)),
    ])
    tags = tool.decode_scen_collection(data)
    assert tags["name"] == "Convergence", tags
    assert tags["coll"] is False, tags
    assert tags["smoo"] is True, tags
    assert tags["vfsz"] == 10, tags
    assert tags["smos"] == 12, tags
    assert tags["hcol"] == (0x9999, 0xCCCC, 0xFFFF), tags
    assert tags["sbar"] == 1, tags
    assert tags["sbth"] == 0, tags
    print("ok    scen collection decoded")


test_parse()
test_end_to_end()
test_clut_positional()
test_wctb()
test_scen()
print("OK")