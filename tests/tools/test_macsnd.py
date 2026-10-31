import importlib.util
import struct
import sys

spec = importlib.util.spec_from_file_location("macsnd", sys.argv[1])
macsnd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(macsnd)


def sampled_snd(samples, version=1, encode=0, channels=1, bits=8, rate=22050):
    """A snd resource with one buffer command pointing at a sound header."""
    if version == 1:
        head = struct.pack(">HHHI", 1, 1, 5, 0)  # format 1, one synth
    else:
        head = struct.pack(">HH", 2, 0)  # format 2, reserved
    head += struct.pack(">H", 1)  # one command
    header_at = len(head) + 8
    head += struct.pack(">HHI", 0x8051, 0, header_at)
    if encode == 0:
        header = struct.pack(">IIIII", 0, len(samples), rate << 16, 0, 0)
        header += struct.pack(">BB", 0, 60)
    else:
        header = struct.pack(">IIIII", 0, channels, rate << 16, 0, 0)
        header += struct.pack(">BB", encode, 60)
        header += struct.pack(">I", len(samples) // channels // (bits // 8))
        header += bytes(22)  # AIFF rate, marker, instrument and recording fields
        header += struct.pack(">H", bits)  # sample size at 48
        header += bytes(64 - len(header))  # reserved, up to the sample area
    return head + header + samples


def test_standard_8bit():
    rate_info = macsnd.decode_snd(sampled_snd(bytes((128, 255, 0))))
    channels, rate, bits, frames = rate_info
    assert (channels, rate, bits) == (1, 22050, 8), rate_info
    assert frames == bytes((128, 255, 0))
    print("ok    standard 8-bit sampled sound")


def test_format_2():
    channels, rate, bits, frames = macsnd.decode_snd(sampled_snd(bytes((200, 56)), version=2))
    assert (channels, bits, frames) == (1, 8, bytes((200, 56)))
    print("ok    format 2 sampled sound")


def test_extended_16bit_stereo():
    big = struct.pack(">hhhh", 1000, -1000, 2000, -2000)
    channels, rate, bits, frames = macsnd.decode_snd(
        sampled_snd(big, encode=255, channels=2, bits=16))
    assert (channels, bits) == (2, 16)
    assert frames == struct.pack("<hhhh", 1000, -1000, 2000, -2000)
    print("ok    extended 16-bit stereo, byte-swapped")


def test_rejects_unsupported():
    for data, why in (
        (struct.pack(">HHHI", 1, 1, 5, 0) + struct.pack(">H", 0), "no sample command"),
        (sampled_snd(b"\x00\x00", encode=254, channels=1, bits=16), "compressed"),
    ):
        try:
            macsnd.decode_snd(data)
        except ValueError:
            continue
        raise AssertionError("accepted " + why)
    print("ok    synthesized and compressed sounds are rejected")


test_standard_8bit()
test_format_2()
test_extended_16bit_stereo()
test_rejects_unsupported()
print("OK")
