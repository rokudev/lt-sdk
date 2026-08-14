#!/usr/bin/env python3
#
# mkltflasher.py                        LTFlasher.json stub converter
#
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, you can obtain one at
# https://mozilla.org/MPL/2.0/.
#
# Copyright 2026, Roku, Inc.  All rights reserved.
#
"""Convert an esptool flasher stub into the flat image format that rit expects.

esptool ships each stub as a JSON object with separately based text and data
sections::

    {"entry": ..., "text": <base64>, "text_start": ..., "data": <base64>, "data_start": ...}

rit's Esp32LoadAndRunFlasher() instead walks a single flat blob::

    [0..3]   image entry address       (u32, little endian)
    [4..7]   section 0 start address   (u32, little endian)
    [8..11]  section 0 size in bytes   (u32, little endian)
    [12..]   section 0 data
             ... section 1 start / size / data, and so on

which is then base64 encoded into an "image_b" member of LTFlasher.json.

Usage:
    ./mkltflasher.py NAME path/to/stub_flasher_xx.json

NAME is the chip name rit looks the stub up by, that is the ChipInfo.pName in
lt/build/tools/rit/source/platforms/esp32.c ("esp32", "esp32s3", ...). The
matching JSON5 fragment is written to stdout for pasting into the "flasher"
object of LTFlasher.json; a summary of the decoded sections goes to stderr.

The esptool stubs live in esptool/targets/stub_flasher/ from esptool v4.0
onwards, as stub_flasher_32.json, stub_flasher_32s3.json, and so on. Earlier
releases do not publish them as standalone JSON.

Pass --verify instead of a stub path to decode the entry already present in
LTFlasher.json and print its sections, e.g.

    ./mkltflasher.py esp32   --verify < LTFlasher.json
    ./mkltflasher.py esp32s3 --verify < LTFlasher.json
"""

import base64
import json
import re
import struct
import sys


def flatten(stub):
    """Pack an esptool stub dict into rit's flat section format."""
    blob = struct.pack("<I", stub["entry"])
    for name in ("text", "data"):
        payload = base64.b64decode(stub[name])
        if not payload:
            continue
        blob += struct.pack("<II", stub[name + "_start"], len(payload))
        blob += payload
    return blob


def describe(blob, where=sys.stderr):
    """Walk a flat blob the same way rit does, reporting each section."""
    entry, = struct.unpack_from("<I", blob, 0)
    print("  entry 0x%08x, %d bytes total" % (entry, len(blob)), file=where)
    offset = 4
    while offset < len(blob):
        address, size = struct.unpack_from("<II", blob, offset)
        offset += 8
        print("  section 0x%08x  %d bytes" % (address, size), file=where)
        offset += size
    if offset != len(blob):
        sys.exit("blob is truncated: consumed %d of %d bytes" % (offset, len(blob)))


def extract(name, text):
    """Pull the flat blob for `name` back out of an LTFlasher.json body."""
    match = re.search(
        r"\b%s\s*:\s*\{[^}]*?image_b\s*:\s*\"([^\"]+)\"" % re.escape(name), text, re.S
    )
    if not match:
        sys.exit("no image_b found for '%s'" % name)
    return base64.b64decode(match.group(1))


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)
    name, path = argv[1], argv[2]

    if path == "--verify":
        print("%s (from LTFlasher.json):" % name, file=sys.stderr)
        describe(extract(name, sys.stdin.read()))
        return

    with open(path) as handle:
        blob = flatten(json.load(handle))
    print("%s (from %s):" % (name, path), file=sys.stderr)
    describe(blob)

    print("    %s: {" % name)
    print('        image_b: "%s"' % base64.b64encode(blob).decode("ascii"))
    print("    }")


if __name__ == "__main__":
    main(sys.argv)
