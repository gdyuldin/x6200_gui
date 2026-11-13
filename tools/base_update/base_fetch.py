#!/usr/bin/env python3
"""base_fetch.py: Xiegu's X6200 base firmware, found and fetched from xiegu.eu, on a computer (tools/base_update/fetch.sh;
the radio's image has no Python).

  base_fetch.py list
        the releases known (base_releases.txt), and which of their files are in DIR
  base_fetch.py get VERSION|latest|all [--out DIR]
        Xiegu's update zip for that release streamed from xiegu.eu, and its base file taken out as it goes by: the zip's
        card image (sdcard.img), its FAT partition, rootfs.tar in it, ./usr/firmware/*.xgf in that. Nothing of the
        zip (370 MB) or the card image (860 MB) is kept, so a radio has room for it; the download stops once the file
        is through. The file is checked against the list's size and SHA-256 and kept in DIR with Xiegu's release note.
  base_fetch.py get --url URL [--out DIR]
        the same for a zip the list does not have (a newer release): the file is kept, marked unverified (FILE.json),
        and its line for the list printed
  base_fetch.py get --zip FILE [--out DIR]
        the same from a zip already here
  base_fetch.py check [--base VERSION] [--out DIR]
        xiegu.eu's downloads page ("X6200 Firmware Upgrade vX.Y.Z") against the list and the base's version
        (--base, as flash.sh --check prints it): DIR/check.json, and a line. Never downloads a base file and never
        writes the base.

DIR: --out (fetch.sh: build/base_update), else here. Exit 0 done, 1 failed, 2 nothing to do.
Only Xiegu's own site is fetched from; nothing of Xiegu's is kept in this repository.

SPDX-License-Identifier: LGPL-2.1-or-later
"""
import ctypes
import ctypes.util
import hashlib
import json
import os
import re
import struct
import sys
import time
import urllib.parse
import urllib.request

UA = "Mozilla/5.0 (compatible; x6200_gui base_update)"     # xiegu.eu turns a bare "Mozilla/5.0" away
DOWNLOADS = "https://xiegu.eu/downloads/"
SDM = "https://xiegu.eu/?sdm_process_download=1&download_id=%s"
HERE = os.path.dirname(os.path.abspath(__file__))
LISTS = [os.path.join(HERE, "base_releases.txt")]
BASE_VERSION_FILE = "/tmp/x6x00-base-version"


def say(*a):
    print(*a, flush=True)


# ---- the list ---------------------------------------------------------------------------------------------------

def releases():
    for path in LISTS:
        if os.path.exists(path):
            out = []
            for line in open(path):
                f = line.split()
                if not f or f[0].startswith("#") or len(f) < 8:
                    continue
                out.append({"version": f[0], "built": f[1], "xgf": f[2], "xgf_size": int(f[3]), "xgf_sha256": f[4],
                            "zip_size": None if f[5] == "-" else int(f[5]), "zip_sha256": None if f[6] == "-" else f[6],
                            "url": None if f[7] == "-" else f[7]})
            return out
    raise SystemExit("no base_releases.txt (%s)" % ", ".join(LISTS))


def vkey(v):
    return tuple(int(x) for x in re.findall(r"\d+", v))


def latest(rels):
    on_site = [r for r in rels if r["url"]]
    return max(on_site, key=lambda r: (vkey(r["version"]), r["built"])) if on_site else None


def default_dir():
    return "."


# ---- raw inflate: the zlib module, or libz through ctypes (the radio's Python has no zlib module) ---------------

class _ZStream(ctypes.Structure):
    _fields_ = [("next_in", ctypes.c_void_p), ("avail_in", ctypes.c_uint), ("total_in", ctypes.c_ulong),
                ("next_out", ctypes.c_void_p), ("avail_out", ctypes.c_uint), ("total_out", ctypes.c_ulong),
                ("msg", ctypes.c_char_p), ("state", ctypes.c_void_p), ("zalloc", ctypes.c_void_p),
                ("zfree", ctypes.c_void_p), ("opaque", ctypes.c_void_p), ("data_type", ctypes.c_int),
                ("adler", ctypes.c_ulong), ("reserved", ctypes.c_ulong)]


class Inflate:
    """A raw deflate stream: feed(data) gives the output in pieces of at most OUT bytes; .eof once the stream ended,
    .unused what came after it"""
    OUT = 1 << 18

    def __init__(self):
        self.eof, self.unused = False, b""
        try:
            if os.environ.get("X6X00_LIBZ"):        # the radio's way, tried on a computer (tools/base_update/test)
                raise ImportError
            import zlib
            self.z = zlib.decompressobj(-15)
            self.lib = None
        except ImportError:
            name = ctypes.util.find_library("z") or "libz.so.1"
            self.lib = ctypes.CDLL(name)
            self.s = _ZStream()
            ver = self.lib.zlibVersion
            ver.restype = ctypes.c_char_p
            if self.lib.inflateInit2_(ctypes.byref(self.s), -15, ver(), ctypes.sizeof(self.s)) != 0:
                raise RuntimeError("inflateInit2 failed")
            self.outbuf = ctypes.create_string_buffer(self.OUT)

    def feed(self, data):
        if self.lib is None:
            while data and not self.eof:
                out = self.z.decompress(data, self.OUT)
                data = self.z.unconsumed_tail
                if self.z.eof:
                    self.eof, self.unused = True, self.z.unused_data
                if out:
                    yield out
                elif not data:
                    break
            return
        inbuf = ctypes.create_string_buffer(data, len(data))
        self.s.next_in = ctypes.addressof(inbuf)
        self.s.avail_in = len(data)
        while not self.eof:
            self.s.next_out = ctypes.addressof(self.outbuf)
            self.s.avail_out = self.OUT
            ret = self.lib.inflate(ctypes.byref(self.s), 0)
            n = self.OUT - self.s.avail_out
            if n:
                yield self.outbuf.raw[:n]
            if ret == 1:                            # Z_STREAM_END
                self.eof = True
                left = self.s.avail_in
                self.unused = data[len(data) - left:] if left else b""
                self.lib.inflateEnd(ctypes.byref(self.s))
            elif ret < 0 and ret != -5:             # an error other than Z_BUF_ERROR (no progress, wants input)
                raise RuntimeError("inflate: %d" % ret)
            if self.s.avail_in == 0 and n < self.OUT:
                break


# ---- the source: bytes as they come, with what was read too far given back -----------------------------------------

class Source:
    def __init__(self, f, total=None, label=""):
        self.f, self.back, self.done, self.total, self.label = f, b"", 0, total, label
        self.said = 0.0

    def read(self, n):
        if self.back:
            d, self.back = self.back[:n], self.back[n:]
            return d
        d = self.f.read(n)
        self.done += len(d)
        if self.total and self.done - self.said >= self.total * 0.05:
            self.said = self.done
            say("downloaded %.0f of %.0f MB (%d%%)%s" % (self.done / 1e6, self.total / 1e6, 100 * self.done / self.total,
                                                         self.label))
        return d

    def exact(self, n):
        d = b""
        while len(d) < n:
            c = self.read(n - len(d))
            if not c:
                raise EOFError("the zip ended early")
            d += c
        return d

    def unread(self, d):
        self.back = d + self.back


# ---- the card image, as it goes by: MBR, the FAT partition, rootfs.tar, ./usr/firmware/*.xgf -----------------------

class Found(Exception):
    pass


class Tar:
    """rootfs.tar fed in order: the .xgf files under usr/firmware kept; Found once the directory is behind"""

    def __init__(self):
        self.buf, self.want, self.name, self.data, self.files, self.longname, self.in_fw = b"", 0, None, None, {}, None, False

    def feed(self, d):
        self.buf += d
        while True:
            if self.want:                           # in a member's data (rounded up to 512)
                take = min(self.want, len(self.buf))
                if self.data is not None:
                    self.data += self.buf[:take]
                self.buf, self.want = self.buf[take:], self.want - take
                if self.want:
                    return
                if self.data is not None:
                    if self.name == "//longname":
                        self.longname = self.data[:self.size].split(b"\0")[0].decode(errors="replace")
                    else:
                        self.files[os.path.basename(self.name)] = bytes(self.data[:self.size])
                        say("found %s in rootfs.tar (%d bytes)" % (self.name, self.size))
                    self.data = None
                continue
            if len(self.buf) < 512:
                return
            h, self.buf = self.buf[:512], self.buf[512:]
            if h == b"\0" * 512:
                if self.files:
                    raise Found()
                continue
            name = h[0:100].split(b"\0")[0].decode(errors="replace")
            prefix = h[345:500].split(b"\0")[0].decode(errors="replace") if h[257:262] == b"ustar" else ""
            if prefix:
                name = prefix + "/" + name
            if self.longname:
                name, self.longname = self.longname, None
            size = int(h[124:136].split(b"\0")[0].strip() or b"0", 8)
            kind = h[156:157]
            self.size, self.want = size, (size + 511) // 512 * 512
            n = name.lstrip("./")
            if kind == b"L":                        # GNU long name: the next member's
                self.name, self.data = "//longname", b""
            elif n.startswith("usr/firmware/") and n.endswith(".xgf") and kind in (b"0", b"\0"):
                self.name, self.data, self.in_fw = name, b"", True
            else:
                self.name, self.data = name, None
                if self.files and not n.startswith("usr/firmware"):
                    raise Found()                   # usr/firmware is behind: no need for the rest


class Image:
    """sdcard.img fed in order: its MBR, then each FAT partition's start (boot sector, FAT, root directory) read,
    rootfs.tar's clusters found and fed to Tar as they go by"""

    def __init__(self):
        self.off, self.head, self.parts, self.part, self.meta, self.ranges, self.tar = 0, b"", None, None, b"", [], Tar()

    def feed(self, d):
        start = self.off
        self.off += len(d)
        if self.parts is None:                      # the MBR
            self.head += d[:max(0, 512 - start)]
            if len(self.head) < 512:
                return
            if self.head[510:512] != b"\x55\xaa":
                raise RuntimeError("sdcard.img has no MBR")
            self.parts = []
            for i in range(4):
                e = self.head[446 + 16 * i:462 + 16 * i]
                kind, lba, cnt = e[4], struct.unpack("<I", e[8:12])[0], struct.unpack("<I", e[12:16])[0]
                if kind in (0x01, 0x04, 0x06, 0x0b, 0x0c, 0x0e) and cnt:
                    self.parts.append((lba * 512, cnt * 512))
            if not self.parts:
                raise RuntimeError("sdcard.img has no FAT partition")
        # a FAT partition's head: gathered until its data region, then read
        if self.part is None and self.parts and self.off > self.parts[0][0]:
            self.part, self.meta = self.parts.pop(0), b""
        if self.part is not None and not self.ranges:
            p0 = self.part[0]
            lo = max(start, p0)
            if lo < self.off:
                self.meta += d[lo - start:]
            if len(self.meta) >= 512 and not hasattr(self, "geo"):
                b = self.meta
                bps, spc, res, nf, roots = struct.unpack("<HBHBH", b[11:19])
                fat16 = struct.unpack("<H", b[22:24])[0]
                if not fat16 or bps != 512:
                    raise RuntimeError("the partition is not FAT16 with 512-byte sectors")
                self.geo = (bps, spc, res, nf, roots, fat16)
            if hasattr(self, "geo"):
                bps, spc, res, nf, roots, fat16 = self.geo
                data_at = (res + nf * fat16) * bps + roots * 32
                if len(self.meta) >= data_at:
                    self.find_tar(data_at)
                    if not self.ranges:             # not in this one: the next FAT partition
                        del self.geo
                        self.part = None
                        return
                    # what of this piece is past the head already: the clusters
                    lo = max(start, p0 + data_at)
                    if lo < self.off:
                        self.feed_ranges(lo, d[lo - start:])
                    return
            return
        if self.ranges:
            self.feed_ranges(start, d)

    def find_tar(self, data_at):
        bps, spc, res, nf, roots, fat16 = self.geo
        fat = self.meta[res * bps:(res + fat16) * bps]
        rd = self.meta[(res + nf * fat16) * bps:data_at]
        for i in range(0, len(rd), 32):
            e = rd[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xe5 or e[11] == 0x0f or e[11] & 0x18:
                continue
            name = (e[0:8].decode(errors="replace").strip() + "." + e[8:11].decode(errors="replace").strip()).lower()
            if name != "rootfs.tar":
                continue
            cl, size = struct.unpack("<H", e[26:28])[0], struct.unpack("<I", e[28:32])[0]
            csize = spc * bps
            chain = []
            while 2 <= cl < 0xfff8 and len(chain) * csize < size + csize:
                chain.append(cl)
                cl = struct.unpack("<H", fat[2 * cl:2 * cl + 2])[0]
            if any(b <= a for a, b in zip(chain, chain[1:])):
                raise RuntimeError("rootfs.tar's clusters are out of order: it can't be read as it streams")
            p0 = self.part[0]
            left = size
            for c in chain:                         # runs of the file: (from, to) in the image, in order
                a = p0 + data_at + (c - 2) * csize
                n = min(csize, left)
                if self.ranges and self.ranges[-1][1] == a:
                    self.ranges[-1][1] = a + n
                else:
                    self.ranges.append([a, a + n])
                left -= n
            say("rootfs.tar in the card image's FAT partition: %d bytes, %d runs" % (size, len(self.ranges)))
            return

    def feed_ranges(self, start, d):
        end = start + len(d)
        while self.ranges and self.ranges[0][0] < end:
            a, b = self.ranges[0]
            lo, hi = max(a, start), min(b, end)
            if lo < hi:
                self.tar.feed(d[lo - start:hi - start])
            if b <= end:
                self.ranges.pop(0)
            else:
                break
        if not self.ranges:                         # rootfs.tar all through
            if self.tar.files:
                raise Found()
            raise RuntimeError("no ./usr/firmware/*.xgf in the card image's rootfs.tar")


def extract(src):
    """The zip's entries in order (it is written as a stream: sizes after the data): the release note kept, the card
    image's base files returned"""
    note, image = None, None
    while True:
        h = src.exact(4)
        if h != b"PK\x03\x04":
            break
        h = src.exact(26)
        ver, flags, method, _, _, crc, csize, usize, nlen, xlen = struct.unpack("<HHHHHIIIHH", h)
        name = src.exact(nlen).decode(errors="replace")
        extra = src.exact(xlen)
        zip64, i = False, 0
        while i + 4 <= len(extra):                  # its records: id, size, data; 0x0001 is zip64's
            rid, rsz = struct.unpack("<HH", extra[i:i + 4])
            zip64 |= rid == 1
            i += 4 + rsz
        is_img = name.lower().endswith(".img")
        is_note = name.lower().endswith(".txt")
        if method == 0 and not flags & 8:           # stored, sizes known (the folder)
            data = src.exact(csize) if csize else b""
            if is_note:
                note = (os.path.basename(name), data)
            continue
        if method == 0 and name.endswith("/"):      # a folder written as a stream: no data, then its descriptor
            d = src.exact(4)
            if d != b"PK\x07\x08":
                src.unread(d)
            src.exact(20 if zip64 else 12)
            continue
        if method != 8:
            raise RuntimeError("%s: method %d, not deflate" % (name, method))
        inf = Inflate()
        keep = b"" if is_note else None
        if is_img:
            image = Image()
            say("reading %s" % name)
        try:
            while not inf.eof:
                d = src.read(1 << 16)
                if not d:
                    raise EOFError("the zip ended inside %s" % name)
                for out in inf.feed(d):
                    if keep is not None:
                        keep += out
                    if image is not None:
                        image.feed(out)
        except Found:
            return note, image.tar.files
        src.unread(inf.unused)
        if keep is not None:
            note = (os.path.basename(name), keep)
        if flags & 8:                               # the data descriptor
            d = src.exact(4)
            if d != b"PK\x07\x08":
                src.unread(d)
            src.exact(20 if zip64 else 12)
        if image is not None:
            break
    if image is None:
        raise RuntimeError("no card image (.img) in the zip")
    if image.tar.files:
        return note, image.tar.files
    raise RuntimeError("no ./usr/firmware/*.xgf in the card image's rootfs.tar")


# ---- get ------------------------------------------------------------------------------------------------------------

def open_url(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=60)


def here_by_sha(out):
    """The .xgf files in out by their SHA-256, whatever their names"""
    have = {}
    if os.path.isdir(out):
        for n in sorted(os.listdir(out)):
            if n.endswith(".xgf"):
                with open(os.path.join(out, n), "rb") as f:
                    have.setdefault(hashlib.sha256(f.read()).hexdigest(), n)
    return have


def keep(out, files, note, rels, url, zip_size):
    """Each file taken out, told apart by its SHA-256 alone: one already here under any name is not kept twice, a
    different file of the same name is not overwritten (the new one gets its sum in its name)"""
    os.makedirs(out, exist_ok=True)
    have = here_by_sha(out)
    ok = True
    for name, data in files.items():
        sha = hashlib.sha256(data).hexdigest()
        known = [r for r in rels if r["xgf_sha256"] == sha]
        if sha in have:
            say("%s: here already, as %s (the same SHA-256 %s): not kept twice" %
                (name, os.path.join(out, have[sha]), sha))
            continue
        path = os.path.join(out, name)
        if os.path.exists(path):                   # another file under this name: kept, the new one beside it
            path = os.path.join(out, "%s-%s.xgf" % (name[:-4] if name.endswith(".xgf") else name, sha[:8]))
        tmp = path + ".part"
        with open(tmp, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
        have[sha] = os.path.basename(path)
        if note:
            with open(path + ".txt", "wb") as f:
                f.write(note[1])
        if known and known[0]["xgf_size"] == len(data):
            say("%s: Xiegu's base %s (%s), %d bytes, sha256 %s: as the list has it" %
                (path, known[0]["version"], known[0]["built"], len(data), sha))
            if os.path.exists(path + ".json"):
                os.remove(path + ".json")
            continue
        named = [r for r in rels if r["xgf"] == name]
        ver = "?"
        m = re.search(r"BASE\W*(\d+\.\d+\.\d+)", (note[1].decode(errors="replace") if note else ""), re.I)
        if m:
            ver = m.group(1)
        stamp = re.search(r"_(\d{8})", name)
        built = "%s-%s-%s" % (stamp.group(1)[:4], stamp.group(1)[4:6], stamp.group(1)[6:8]) if stamp else "?"
        line = "%s %s %s %d %s %s - %s" % (ver, built, name, len(data), sha, zip_size or "-", url or "-")
        with open(path + ".json", "w") as f:
            json.dump({"unverified": True, "version": ver, "built": built, "size": len(data), "sha256": sha,
                       "url": url, "list_line": line}, f, indent=1)
        if named:
            say("%s: %d bytes, sha256 %s: the list has another file under this name (%s): kept as unverified" %
                (path, len(data), sha, named[0]["xgf_sha256"]))
            ok = False
        else:
            say("%s: NOT in the list: %d bytes, sha256 %s, kept as unverified (%s.json)" % (path, len(data), sha, path))
            say("its line for base_releases.txt (the zip's sum still to fill in): %s" % line)
    return ok


def get(args, rels):
    out = args.get("out") or default_dir()
    if args.get("zip"):
        f = open(args["zip"], "rb")
        note, files = extract(Source(f, os.path.getsize(args["zip"]), " of the zip"))
        return 0 if keep(out, files, note, rels, None, os.path.getsize(args["zip"])) else 1
    if args.get("url"):
        targets = [{"url": args["url"], "zip_size": None, "version": "?"}]
    else:
        v = args["version"]
        if v == "latest":
            targets = [latest(rels)]
        elif v == "all":
            targets = [r for r in rels if r["url"]]
        else:
            targets = [r for r in rels if r["version"] == v and r["url"]]
        if not targets or targets[0] is None:
            say("no release %s on xiegu.eu in the list (base_fetch.py list)" % v)
            return 2
    rc = 0
    have = here_by_sha(out)                         # the files here by their SHA-256, whatever their names
    for r in targets:
        if r.get("xgf_sha256") in have:
            say("base %s: here already, as %s (its SHA-256)" % (r["version"], os.path.join(out, have[r["xgf_sha256"]])))
            continue
        say("fetching base %s from %s" % (r["version"], r["url"]))
        t = time.time()
        resp = open_url(r["url"])
        total = int(resp.headers.get("Content-Length") or 0) or r.get("zip_size")
        if r.get("zip_size") and total and total != r["zip_size"]:
            say("xiegu.eu's file is %d bytes, the list has %d: taken out all the same, checked by its own sum" %
                (total, r["zip_size"]))
        src = Source(resp, total)
        note, files = extract(src)
        resp.close()
        say("the base file is out after %.0f of %.0f MB, in %.0f s" % (src.done / 1e6, (total or 0) / 1e6,
                                                                      time.time() - t))
        if not keep(out, files, note, rels, r["url"], total):
            rc = 1
    return rc


# ---- check ----------------------------------------------------------------------------------------------------------

def online():
    page = open_url(DOWNLOADS).read().decode(errors="replace")
    found = {}
    # each release's own download link: <a href="...download_id=N" ... title="X6200 Firmware Upgrade vX.Y.Z">, the
    # two in the one tag (the titles in the text around them belong to no link)
    for tag in re.findall(r"<a\b[^>]*>", page):
        i = re.search(r"download_id=(\d+)", tag)
        v = re.search(r'title="X6200 Firmware Upgrade v(\d+(?:\.\d+)+)"', tag)
        if i and v:
            found.setdefault(v.group(1), i.group(1))
    return found


def check(args, rels):
    out = args.get("out") or default_dir()
    base = args.get("base")
    if not base and os.path.exists(BASE_VERSION_FILE):
        base = open(BASE_VERSION_FILE).read().strip()
    found = online()
    if not found:
        say("xiegu.eu's downloads page has no X6200 firmware: not checked")
        return 1
    newest = max(found, key=vkey)
    known = {r["version"] for r in rels}
    res = {"checked": time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime()), "base": base or "",
           "online": sorted(found, key=vkey, reverse=True), "latest": newest,
           "newer": bool(base) and vkey(newest) > vkey(base), "not_in_list": sorted(set(found) - known, key=vkey),
           "links": {v: SDM % i for v, i in found.items()}}
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "check.json")
    old = {}
    if os.path.exists(path):
        try:
            old = json.load(open(path))
        except ValueError:
            pass
    res["announced"] = old.get("announced", "")
    with open(path + ".part", "w") as f:
        json.dump(res, f, indent=1)
    os.replace(path + ".part", path)
    say("base %s; xiegu.eu: %s; latest %s%s%s" % (base or "?", ", ".join(res["online"]), newest,
                                                  ": NEWER than this base" if res["newer"] else "",
                                                  "; not in the list yet: " + ", ".join(res["not_in_list"])
                                                  if res["not_in_list"] else ""))
    return 0


def main(argv):
    args, pos = {}, []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--out", "--url", "--zip", "--base") and i + 1 < len(argv):
            args[a[2:]] = argv[i + 1]
            i += 2
        else:
            pos.append(a)
            i += 1
    rels = releases()
    if not pos or pos[0] == "list":
        out = args.get("out") or default_dir()
        sums = set()                                # files are told apart by their SHA-256, not their names
        if os.path.isdir(out):
            for n in os.listdir(out):
                if n.endswith(".xgf"):
                    sums.add(hashlib.sha256(open(os.path.join(out, n), "rb").read()).hexdigest())
        for r in rels:
            here = r["xgf_sha256"] in sums
            say("%-6s %s  %-30s %s" % (r["version"], r["built"], r["xgf"],
                                        "here" if here else ("on xiegu.eu" if r["url"] else "on the radio's internal memory")))
        return 0
    if pos[0] == "get":
        if len(pos) > 1:
            args["version"] = pos[1]
        elif not args.get("url") and not args.get("zip"):
            args["version"] = "latest"
        return get(args, rels)
    if pos[0] == "check":
        return check(args, rels)
    say(__doc__)
    return 1


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        sys.exit(1)
    except Exception as e:                          # one line for the screen and the log, not a trace
        say("failed: %s" % e)
        sys.exit(1)
