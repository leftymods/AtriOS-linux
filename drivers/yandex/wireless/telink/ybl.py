#!/usr/bin/env python3

import os
import sys
import zlib
import struct


def main():
    binPath = sys.argv[1]
    yblPath = sys.argv[2]

    data = open(binPath, "rb").read()

    crc32 = zlib.crc32(data)

    print("CRC32:", hex(crc32))

    with open(yblPath, "wb") as f:
        f.write(data)
        f.write(struct.pack("<I", crc32))


if __name__ == "__main__":
    main()
