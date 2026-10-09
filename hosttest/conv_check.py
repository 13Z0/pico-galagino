#!/usr/bin/env python3
"""
conv_check.py - compare the core's ROM conversion with upstream Galagino's.

Upstream converts the ROMs on a PC with the Python scripts in romconv/; the
core does the same on the board, in C. This runs both on your ROM sets and
compares the results byte for byte.

    hosttest/build.sh
    hosttest/conv_check.py <romroot> <galagino-src> <workdir>

<romroot> holds the game folders as on the SD card (PACMAN, GALAGA, ...);
<galagino-src> is a checkout of https://github.com/harbaum/galagino; <workdir>
receives the extracted ROMs, upstream's headers and the core's dumps.

Steps:
  1. every file of the sets (galagino/gal_games.c) is taken out of the zips by
     CRC32 and written under upstream's name, as romconv expects them;
  2. the commands of upstream's romconv/conv.sh are run on them (logos, the
     star set, the tile map and the Z80 patch are left out: their output is
     committed upstream and used as is);
  3. hosttest/conv_check dumps the core's tables;
  4. every array upstream wrote is compared with the core's, allowing for the
     documented differences: colours are not byte-swapped, sprites are kept in
     two variants (normal and flipped in x; a flip in y reads the rows
     backwards), and 1942's playfield tiles in one (flipped while drawing).

The ROM sets are not part of this project; nothing here is stored outside
<workdir>.
"""

import os
import re
import shlex
import subprocess
import sys
import zipfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def rom_table():
    """name -> crc, from gal_games.c"""
    src = open(os.path.join(ROOT, "galagino", "gal_games.c")).read()
    files = {}
    for name, crc in re.findall(r'\{"([^"]+)",\s*0x([0-9a-f]{8}),', src):
        files[name] = int(crc, 16)
    return files


def extract(romroot, outdir):
    wanted = {}
    for name, crc in rom_table().items():
        wanted.setdefault(crc, []).append(name)
    os.makedirs(outdir, exist_ok=True)
    found = set()
    for folder in sorted(os.listdir(romroot)):
        path = os.path.join(romroot, folder)
        if not os.path.isdir(path):
            continue
        for fn in sorted(os.listdir(path)):
            full = os.path.join(path, fn)
            if fn.lower().endswith(".zip"):
                with zipfile.ZipFile(full) as z:
                    for info in z.infolist():
                        if info.CRC in wanted:
                            data = z.read(info)
                            for name in wanted[info.CRC]:
                                open(os.path.join(outdir, name), "wb").write(data)
                                found.add(name)
            elif os.path.isfile(full):
                data = open(full, "rb").read()
                crc = zlib.crc32(data)
                for name in wanted.get(crc, []):
                    open(os.path.join(outdir, name), "wb").write(data)
                    found.add(name)
    return found


def run_upstream(src, romdir, outdir):
    os.makedirs(outdir, exist_ok=True)
    conv = open(os.path.join(src, "romconv", "conv.sh")).read()
    for line in conv.splitlines():
        line = line.strip()
        if not line.startswith("./") or "z80patch" in line or "logoconv" in line:
            continue
        args = shlex.split(line)
        script = os.path.join(src, "romconv", args[0][2:])
        args = [a.replace("../roms/", romdir + "/").replace("../galagino/", outdir + "/") for a in args[1:]]
        # skip what needs files that are not there (a set that was not found)
        needed = [p for a in args for p in a.split(",") if p.startswith(romdir + "/")]
        if not all(os.path.exists(p) for p in needed):
            continue
        r = subprocess.run([sys.executable, "-I", script] + args, capture_output=True, text=True)
        if r.returncode != 0:
            print("upstream failed:", line, r.stderr.strip()[-300:])


ARRAY = re.compile(r"(signed\s+char|unsigned\s+char|unsigned\s+short|unsigned\s+long)\s+(\w+)\s*((?:\[\s*\w*\s*\])+)\s*=\s*\{(.*?)\};", re.S)


def parse_headers(outdir):
    arrays = {}
    for fn in os.listdir(outdir):
        if not fn.endswith(".h"):
            continue
        text = re.sub(r"//[^\n]*", "", open(os.path.join(outdir, fn)).read())
        for ctype, name, _dims, body in ARRAY.findall(text):
            vals = [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", body)]
            arrays[name] = (ctype.split()[-1], vals)
    return arrays


def load_dump(dumpdir, name, size):
    path = os.path.join(dumpdir, name + ".bin")
    if not os.path.exists(path):
        return None
    data = open(path, "rb").read()
    fmt = {1: "B", 2: "H", 4: "I"}[size]
    import array
    a = array.array(fmt)
    a.frombytes(data)
    return list(a)


def swap16(v):
    return ((v >> 8) | (v << 8)) & 0xffff


def rows_reversed(words, per_item, rows):
    """reverse the order of the rows (each per_item/rows words) of every item"""
    out = []
    step = per_item // rows
    for i in range(0, len(words), per_item):
        item = words[i:i + per_item]
        for r in reversed(range(rows)):
            out.extend(item[r * step:(r + 1) * step])
    return out


def nibbles_reversed(words):
    """1942 tiles: each row is two words of 8 nibbles; flip the 16 pixels"""
    out = []
    for i in range(0, len(words), 2):
        v = words[i] | (words[i + 1] << 32)
        f = 0
        for p in range(16):
            f = (f << 4) | ((v >> (4 * p)) & 15)
        out.extend([f & 0xffffffff, f >> 32])
    return out


def compare(arrays, dumpdir):
    ok = bad = 0

    def report(name, good, detail=""):
        nonlocal ok, bad
        print("  %-26s %s%s" % (name, "ok" if good else "DIFFERENT", detail))
        if good:
            ok += 1
        else:
            bad += 1

    for name in sorted(arrays):
        ctype, vals = arrays[name]
        if name.endswith("_logo") or name in ("galaga_star_set", "tileaddr"):
            continue
        if ctype == "char":
            mine = load_dump(dumpdir, name, 1)
            if mine is None:
                print("  %-26s no dump" % name)
                continue
            if name.endswith("_wavetable"):
                mine = [v - 256 if v > 127 else v for v in mine]
            report(name, mine[:len(vals)] == vals and len(mine) >= len(vals))
        elif ctype == "short":
            mine = load_dump(dumpdir, name, 2)
            if mine is None:
                print("  %-26s no dump" % name)
                continue
            if "colormap" in name:
                vals = [swap16(v) for v in vals]
            report(name, mine == vals, "" if mine == vals else " (%d vs %d words)" % (len(mine), len(vals)))
        else:  # long: 32-bit words
            mine = load_dump(dumpdir, name, 4)
            if mine is None:
                print("  %-26s no dump" % name)
                continue
            if name == "sr_08_a1":
                n = len(mine)
                variants = [mine, rows_reversed(mine, 32, 16), nibbles_reversed(mine),
                            rows_reversed(nibbles_reversed(mine), 32, 16)]
                good = all(vals[v * n:(v + 1) * n] == variants[v] for v in range(4)) and len(vals) == 4 * n
                report(name, good, " (4 flips)")
            elif name == "_1942_sprites":
                report(name, mine == vals)
            else:
                mine_x = load_dump(dumpdir, name + "_x", 4)
                n = len(vals) // 4
                normal, xflip = mine[:n], mine_x[:n]
                variants = [normal, rows_reversed(normal, 16, 16), xflip, rows_reversed(xflip, 16, 16)]
                good = all(vals[v * n:(v + 1) * n] == variants[v] for v in range(4))
                report(name, good, " (4 variants from 2)")
    return ok, bad


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 1
    romroot, src, work = sys.argv[1:]
    romdir, outdir, dumpdir = (os.path.join(work, d) for d in ("roms", "upstream", "dump"))
    found = extract(romroot, romdir)
    print("extracted %d ROM files" % len(found))
    run_upstream(src, romdir, outdir)
    r = subprocess.run([os.path.join(ROOT, "hosttest", "conv_check"), romroot, dumpdir])
    if r.returncode != 0:
        print("hosttest/conv_check failed")
        return 1
    ok, bad = compare(parse_headers(outdir), dumpdir)
    print("%d arrays identical, %d different" % (ok, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
