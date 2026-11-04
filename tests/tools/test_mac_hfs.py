import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hfs_fixture import make_hfs_image

tool = sys.argv[1]


def main():
    root = tempfile.mkdtemp()
    image = os.path.join(root, "Test.dsk")
    with open(image, "wb") as stream:
        stream.write(make_hfs_image())
    out = os.path.join(root, "System Folder")
    result = subprocess.run([sys.executable, tool, "--image", image,
                             "--source", "System Folder", "--destination", out],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    with open(os.path.join(out, "System"), "rb") as stream:
        assert stream.read() == b"DATA", "the data fork is copied"
    with open(os.path.join(out, ".rsrc", "System"), "rb") as stream:
        assert stream.read() == b"RSRC", "the resource fork is copied beside it"

    # An older copy has data forks only; --merge repairs the forks without
    # replacing the user's data file.
    merged = os.path.join(root, "Merged")
    os.makedirs(merged)
    with open(os.path.join(merged, "System"), "wb") as stream:
        stream.write(b"OLD!")
    result = subprocess.run([sys.executable, tool, "--image", image,
                             "--source", "System Folder", "--destination", merged,
                             "--merge"], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    with open(os.path.join(merged, "System"), "rb") as stream:
        assert stream.read() == b"OLD!", "merge keeps the existing data fork"
    with open(os.path.join(merged, ".rsrc", "System"), "rb") as stream:
        assert stream.read() == b"RSRC", "merge adds the missing resource fork"
    print("ok    HFS data and resource forks copy out of an image")


main()
