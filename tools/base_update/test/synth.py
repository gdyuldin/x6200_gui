#!/usr/bin/env python3
"""A small update zip made as Xiegu makes theirs, for base_fetch.py's test (tools/base_update/test/run.sh): a zip
written as a stream (each entry's sizes in a data descriptor after it), with a folder, a release note and sdcard.img;
the image an MBR and three partitions as Xiegu's has them: a FAT16 boot partition (no rootfs.tar), a Linux one, and a
FAT16 partition whose rootfs.tar holds ./usr/firmware/NAME.xgf among other members, before and after it.

    synth.py ZIP XGF_NAME XGF_FILE

SPDX-License-Identifier: LGPL-2.1-or-later
"""
import io
import struct
import sys
import tarfile
import zipfile

SECTOR = 512


def fat16(files, sectors):
    """A FAT16 file system of sectors sectors with files (name -> bytes) in its root, each contiguous"""
    spc, res, nf, roots = 4, 1, 2, 512
    clusters = sectors // spc
    fat_sectors = (clusters * 2 + SECTOR - 1) // SECTOR
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xeb\x3c\x90"
    bs[3:11] = b"MSWIN4.1"
    struct.pack_into("<HBHBHHBHHHII", bs, 11, SECTOR, spc, res, nf, roots, 0 if sectors > 65535 else sectors, 0xf8,
                     fat_sectors, 32, 64, 0, sectors if sectors > 65535 else 0)
    bs[54:62] = b"FAT16   "
    bs[510:512] = b"\x55\xaa"
    fat = bytearray(fat_sectors * SECTOR)
    struct.pack_into("<HH", fat, 0, 0xfff8, 0xffff)
    root = bytearray(roots * 32)
    data = bytearray()
    cl = 2
    for i, (name, body) in enumerate(files.items()):
        base, _, ext = name.partition(".")
        n = max(1, (len(body) + spc * SECTOR - 1) // (spc * SECTOR))
        for k in range(n):
            struct.pack_into("<H", fat, 2 * (cl + k), 0xffff if k == n - 1 else cl + k + 1)
        e = root[32 * i:32 * i + 32]
        e[0:11] = (base.upper().ljust(8) + ext.upper().ljust(3)).encode()
        e[11] = 0x20
        struct.pack_into("<HI", e, 26, cl, len(body))
        root[32 * i:32 * i + 32] = e
        data += body + b"\0" * (n * spc * SECTOR - len(body))
        cl += n
    img = bytes(bs) + bytes(fat) * nf + bytes(root) + bytes(data)
    assert len(img) <= sectors * SECTOR
    return img + b"\0" * (sectors * SECTOR - len(img))


def tar_with(name, xgf):
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode="w", format=tarfile.GNU_FORMAT) as t:
        def add(path, body):
            ti = tarfile.TarInfo(path)
            ti.size = len(body)
            t.addfile(ti, io.BytesIO(body))
        add("./etc/os-release", b"NAME=Xiegu\n")
        add("./lib/firmware/" + "x" * 120 + ".bin", b"\x01" * 3000)    # a long name: GNU's own record
        add("./usr/firmware/" + name, xgf)
        add("./usr/lib/libsomething.so", b"\x02" * 70000)
    return out.getvalue()


def main():
    zpath, name, xpath = sys.argv[1:4]
    xgf = open(xpath, "rb").read()
    boot = fat16({"zimage": b"\x03" * 9000, "boot.scr": b"setenv\n"}, 4096)
    rootfs = fat16({"rootfs.tar": tar_with(name, xgf)}, 8192)
    lead = 2048 * SECTOR
    linux = b"\x04" * (1024 * SECTOR)
    parts = [(0x0e, lead, len(boot)), (0x83, lead + len(boot), len(linux)),
             (0x0e, lead + len(boot) + len(linux), len(rootfs))]
    mbr = bytearray(SECTOR)
    for i, (kind, at, size) in enumerate(parts):
        struct.pack_into("<B3sB3sII", mbr, 446 + 16 * i, 0, b"\0\0\0", kind, b"\0\0\0", at // SECTOR, size // SECTOR)
    mbr[510:512] = b"\x55\xaa"
    image = bytes(mbr) + b"\0" * (lead - SECTOR) + boot + linux + rootfs

    class Stream(io.RawIOBase):                     # not seekable: zipfile writes data descriptors, as Xiegu's has
        def __init__(self, f):
            self.f = f

        def writable(self):
            return True

        def write(self, b):
            return self.f.write(b)

    with open(zpath, "wb") as f:
        with zipfile.ZipFile(Stream(f), "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr(zipfile.ZipInfo("synthetic/"), b"")
            z.writestr("synthetic/ReleaseNote.txt", "Synthetic release\nBASE: 9.9.9 Jan 01 2030\n")
            z.writestr("synthetic/sdcard.img", image)


if __name__ == "__main__":
    main()
