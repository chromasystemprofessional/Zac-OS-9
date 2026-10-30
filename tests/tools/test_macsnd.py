import importlib.util
import struct
import sys

spec = importlib.util.spec_from_file_location("macsnd", sys.argv[1])
macsnd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(macsnd)


def test_square_wave():
    data = struct.pack(">h", 1) + b"\x00" * 8
    rate, width, frames = macsnd.decode_snd(data)
    assert rate == 22050 and width == 8
    assert len(frames) == rate // 2
    print("ok    square wave")


def test_sampled_pcm():
    header = struct.pack(">hhIhIh", 2, 1, 4, 16, 22050 << 16, 1)
    header += struct.pack(">h", 1)  # numCommands again
    cmd = struct.pack(">h", 1) + struct.pack(">h", 0) + struct.pack(">I", 0)
    samples = struct.pack(">hhhh", 0, 1000, -1000, 0)
    rate, width, frames = macsnd.decode_snd(header + cmd + samples)
    assert rate == 22050 and width == 16
    assert len(frames) == 8
    print("ok    sampled pcm")


def test_sampled_8bit():
    header = struct.pack(">hhIhIh", 2, 1, 2, 8, 22050 << 16, 1)
    header += struct.pack(">h", 1)
    cmd = struct.pack(">h", 1) + struct.pack(">h", 0) + struct.pack(">I", 0)
    samples = bytes((128, 255))
    rate, width, frames = macsnd.decode_snd(header + cmd + samples)
    assert width == 16
    assert struct.unpack("<hh", frames) == (0, 127 * 256)
    print("ok    sampled 8-bit")


test_square_wave()
test_sampled_pcm()
test_sampled_8bit()
print("OK")
