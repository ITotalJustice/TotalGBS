#!/usr/bin/python

import os
import zipfile
import argparse
import struct

parser = argparse.ArgumentParser(prog="sodium64 ROM converter (modified)")
parser.add_argument('--rom', required=True, help='path to the rom folder')
parser.add_argument('--z64', dest='z64', required=True, help='path to the rom to patch')
parser.add_argument("--ext", action="extend", required=True, nargs="+", default=[], help='list of supported extensions')
parser.add_argument("--offset", required=True, type=lambda x: int(x,0), help='offset in the output to append the rom')
parser.add_argument("--out_ext", required=True, help='output ext appended to rom')
parser.add_argument("--out", required=False, default="out", help='output folder')
parser.add_argument("--big_endian", required=False, default=False, help='set to true fpr big endian')
args = parser.parse_args()

ROM_DIR = args.rom
Z64_PATH = args.z64
EXTENSIONS = args.ext
OFFSET = args.offset
OUT_DIR = args.out
OUT_EXT = args.out_ext
BIG_ENDIAN = args.big_endian

print("sodium64 ROM converter (modified)")
print("Place " + Z64_PATH + " and ROMs with extension " + str(EXTENSIONS) + " in the same folder as this script.")
print("The out folder will contain the ROMs converted to " + OUT_EXT + " format, ready to play.\n")

if not os.path.isfile(Z64_PATH):
    print("Error: " + Z64_PATH + " not found!")
    exit()

if not os.path.isdir(ROM_DIR):
    print("Error: " + ROM_DIR + " not found!")
    exit()

if not os.path.isdir(OUT_DIR):
    os.mkdir(OUT_DIR)

def read_file(filepath):
    with open(filepath, 'rb') as f:
        return f.read()

def parse_gbs(filepath):
    return (bytearray(), read_file(filepath))

def parse_zip(filepath):
    with zipfile.ZipFile(filepath, 'r') as zip:
        m3u_data = bytearray()
        for info in zip.infolist():
            if (info.filename.endswith(".m3u")):
                m3u_data += zip.read(info) + bytearray(1) # add null byte
            elif (info.filename.endswith(".gbs")):
                gbs_data = zip.read(info)
        return (m3u_data, gbs_data)

BASE_ROM = read_file(Z64_PATH)

for filename in os.listdir(ROM_DIR):
    ext = os.path.splitext(filename)[1].lower()
    filepath = os.path.join(ROM_DIR, filename)

    # check if we want this file
    if (ext in EXTENSIONS) and os.path.isfile(filepath):
        with open(os.path.join(OUT_DIR, filename[:-len(ext)] + OUT_EXT), 'wb') as outFile:
            if (zipfile.is_zipfile(filepath)):
                (m3u_data, gbs_data) = parse_zip(filepath)
            else:
                (m3u_data, gbs_data) = parse_gbs(filepath)

            gbs_size = len(gbs_data)
            m3u_size = len(m3u_data)

            # skip if gbs wasn't found
            if (gbs_size == 0):
                continue

            # align to 2 bytes (for dmaCopy)
            gbs_offset = (OFFSET + 16 + m3u_size + 0x1) & ~0x1

            # align bankx to 2 bytes (for dmaCopy)
            # this isn't required, as it will call gbaFastMemcpy otherwise
            # however, it will offer a decent speed up in some games
            # that bank switch a lot (more than 13 during a single song).

            # so far, no songs do this, so this is a pointless speedup
            # however i still deem it worth it ;)
            load_addr = (gbs_data[0x6 + 0] << 0) | (gbs_data[0x6 + 1] << 8)
            # this will unalign bank0 but align bankx
            gbs_offset += load_addr & 0x1

            print("Converting " + filename + "..." + " gbs_size: " + str(gbs_size) + " gbs_offset: " + str(gbs_offset) + " m3u_size: " + str(m3u_size))

            # write out base rome
            outFile.write(BASE_ROM)
            outFile.seek(OFFSET, 0)

            # write out struct and m3u data
            if (BIG_ENDIAN):
                outFile.write(struct.pack('>IIII', 0x454D5530, gbs_size, gbs_offset, m3u_size))
            else:
                outFile.write(struct.pack('<IIII', 0x454D5530, gbs_size, gbs_offset, m3u_size))

            outFile.write(m3u_data)

            # write out gbs data
            outFile.seek(gbs_offset, 0)
            outFile.write(gbs_data)
