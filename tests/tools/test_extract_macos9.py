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
    """Build a minimal valid resource fork from [(type, id, body)].

    Format: 16-byte header (data_off=256, map_off, data_len, map_len),
    then 256 bytes of padding (header area), then resource data (each body
    prefixed by 4-byte length), then the resource map.
    """
    # Build resource data (each prefixed by 4-byte length)
    data = b""
    for rtype, res_id, body in resources:
        data += struct.pack(">I", len(body)) + body

    # Type list
    from collections import OrderedDict
    type_counts = OrderedDict()
    for rtype, res_id, body in resources:
        type_counts.setdefault(rtype, 0)
        type_counts[rtype] += 1

    type_list = struct.pack(">H", len(type_counts) - 1)
    header_size = 2 + 8 * len(type_counts)
    cursor = header_size
    ref_blobs = {}
    for rtype, count in type_counts.items():
        ref_off = cursor
        type_list += struct.pack(">4sH H", rtype, count - 1, ref_off)
        cursor += 2 + 12 * count  # count field + entries
    for rtype, count in type_counts.items():
        ref_list = struct.pack(">H", count - 1)
        # Need resource IDs; use sequential indices
        idx = 0
        for r, rid, body in resources:
            if r == rtype:
                ref_list += struct.pack(">H", rid)  # res_id
                ref_list += struct.pack(">H", 0xFFFF)  # no name
                ref_list += bytes([0])  # attrs
                # data offset (3 bytes, relative to data section start)
                # We need to compute the offset. For simplicity, use 0.
                ref_list += struct.pack(">I", 0)[1:]  # 3 bytes
                ref_list += struct.pack(">I", 0)  # reserved
                idx += 1
        type_list += ref_list

    # Resource map
    data_section_start = 256
    data_section = b"\x00" * (256 - len(data) % 256) + data if len(data) < 256 else data
    # Actually, let's just place data at offset 256
    data_section = b"\x00" * 256 + data
    map_offset = len(data_section)
    map_size = 28 + len(type_list)
    map_header = struct.pack(">IIII", data_section_start, map_offset, len(data), map_size)
    map_info = struct.pack(">I H H", 0, 0, 0)  # next map, file ref, attrs
    name_list_offset = 28 + len(type_list)
    map_ = (map_header + map_info
            + struct.pack(">HH", 28, name_list_offset)
            + type_list)

    # The data offsets in reference entries need to be correct.
    # Let me recompute them properly.
    # Actually, the sequential parsing in parse_resource_fork doesn't use
    # the reference list offsets — it walks the data section sequentially.
    # So the reference list offsets don't matter for our parser.
    # But the data section must start at data_offset (256).

    # full file: 16-byte header + 240 bytes padding + data + map
    return map_header + b"\x00" * 240 + data + map_


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
    assert any(fn.startswith("thme_") for fn in os.listdir(os.path.join(out, "theme"))), \
        "resource-fork theme extracted"
    print("ok    end-to-end bundle build")


test_parse()
test_end_to_end()
print("OK")