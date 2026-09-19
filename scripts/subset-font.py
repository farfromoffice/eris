#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice

"""Cuts a TrueType face down to the characters the desktop draws.

A system font carries thousands of glyphs and the shell uses ninety five of
them. The subset is a real TrueType file, so the loader in the desktop parses
the same format it would parse on a full face, and the tree carries kilobytes
instead of megabytes.

Run it when the font changes, and commit what it writes.
"""

import argparse
import struct
import sys

FIRST = 32
LAST = 126


def read_table_directory(data):
    count = struct.unpack(">H", data[4:6])[0]
    tables = {}

    for index in range(count):
        offset = 12 + index * 16
        tag = data[offset:offset + 4].decode("latin1")
        start, length = struct.unpack(">II", data[offset + 8:offset + 16])
        tables[tag] = (start, length)

    return tables


def table(data, tables, tag):
    start, length = tables[tag]
    return data[start:start + length]


def glyph_for(data, tables, code):
    cmap = table(data, tables, "cmap")
    count = struct.unpack(">H", cmap[2:4])[0]

    best = None
    for index in range(count):
        platform, encoding, offset = struct.unpack(">HHI", cmap[4 + index * 8:12 + index * 8])
        format_id = struct.unpack(">H", cmap[offset:offset + 2])[0]

        if format_id == 4 and (platform, encoding) in ((3, 1), (0, 3), (0, 4)):
            best = offset

    if best is None:
        raise SystemExit("the font has no format 4 character map")

    segment_count = struct.unpack(">H", cmap[best + 6:best + 8])[0] // 2
    ends = struct.unpack(f">{segment_count}H", cmap[best + 14:best + 14 + segment_count * 2])
    starts_at = best + 16 + segment_count * 2
    starts = struct.unpack(f">{segment_count}H", cmap[starts_at:starts_at + segment_count * 2])
    deltas_at = starts_at + segment_count * 2
    deltas = struct.unpack(f">{segment_count}h", cmap[deltas_at:deltas_at + segment_count * 2])
    ranges_at = deltas_at + segment_count * 2
    ranges = struct.unpack(f">{segment_count}H", cmap[ranges_at:ranges_at + segment_count * 2])

    for segment in range(segment_count):
        if code > ends[segment] or code < starts[segment]:
            continue

        if ranges[segment] == 0:
            return (code + deltas[segment]) & 0xFFFF

        position = ranges_at + segment * 2 + ranges[segment] + (code - starts[segment]) * 2
        glyph = struct.unpack(">H", cmap[position:position + 2])[0]

        return (glyph + deltas[segment]) & 0xFFFF if glyph != 0 else 0

    return 0


def build(source, destination):
    data = open(source, "rb").read()
    tables = read_table_directory(data)

    head = bytearray(table(data, tables, "head"))
    hhea = bytearray(table(data, tables, "hhea"))
    maxp = bytearray(table(data, tables, "maxp"))
    hmtx = table(data, tables, "hmtx")
    glyf = table(data, tables, "glyf")

    long_loca = struct.unpack(">h", head[50:52])[0] == 1
    loca_raw = table(data, tables, "loca")

    if long_loca:
        loca = list(struct.unpack(f">{len(loca_raw) // 4}I", loca_raw))
    else:
        loca = [value * 2 for value in struct.unpack(f">{len(loca_raw) // 2}H", loca_raw)]

    metric_count = struct.unpack(">H", hhea[34:36])[0]

    def advance(glyph):
        if glyph < metric_count:
            return struct.unpack(">Hh", hmtx[glyph * 4:glyph * 4 + 4])
        last = struct.unpack(">Hh", hmtx[(metric_count - 1) * 4:(metric_count - 1) * 4 + 4])
        bearing_at = metric_count * 4 + (glyph - metric_count) * 2
        bearing = struct.unpack(">h", hmtx[bearing_at:bearing_at + 2])[0]
        return (last[0], bearing)

    def components_of(glyph):
        """The glyphs a composite is built from, such as the dot over an i."""
        outline = glyf[loca[glyph]:loca[glyph + 1]]
        if not outline or struct.unpack(">h", outline[0:2])[0] >= 0:
            return []

        found = []
        cursor = 10

        while True:
            flags, index = struct.unpack(">HH", outline[cursor:cursor + 4])
            found.append(index)
            cursor += 4

            cursor += 4 if flags & 0x0001 else 2

            if flags & 0x0008:
                cursor += 2
            elif flags & 0x0040:
                cursor += 4
            elif flags & 0x0080:
                cursor += 8

            if not flags & 0x0020:
                break

        return found

    # Glyph zero is the missing character box and every font keeps it first.
    wanted = [0]
    mapping = {}

    for code in range(FIRST, LAST + 1):
        glyph = glyph_for(data, tables, code)
        if glyph not in wanted:
            wanted.append(glyph)
        mapping[code] = wanted.index(glyph)

    # A composite points at other glyphs, and those have to come along or the
    # letter arrives without its accent, or without its dot.
    pending = list(wanted)
    while pending:
        glyph = pending.pop()
        for component in components_of(glyph):
            if component not in wanted:
                wanted.append(component)
                pending.append(component)

    new_glyf = bytearray()
    new_loca = [0]
    new_hmtx = bytearray()

    for glyph in wanted:
        outline = bytearray(glyf[loca[glyph]:loca[glyph + 1]])

        if outline and struct.unpack(">h", outline[0:2])[0] < 0:
            # The components moved, so the indices inside the composite have to
            # move with them.
            cursor = 10

            while True:
                flags, index = struct.unpack(">HH", outline[cursor:cursor + 4])
                outline[cursor + 2:cursor + 4] = struct.pack(">H", wanted.index(index))
                cursor += 4

                cursor += 4 if flags & 0x0001 else 2

                if flags & 0x0008:
                    cursor += 2
                elif flags & 0x0040:
                    cursor += 4
                elif flags & 0x0080:
                    cursor += 8

                if not flags & 0x0020:
                    break

        outline = bytes(outline)

        new_glyf.extend(outline)
        if len(new_glyf) % 4 != 0:
            new_glyf.extend(b"\0" * (4 - len(new_glyf) % 4))

        new_loca.append(len(new_glyf))

        width, bearing = advance(glyph)
        new_hmtx.extend(struct.pack(">Hh", width, bearing))

    head[50:52] = struct.pack(">h", 1)
    maxp[4:6] = struct.pack(">H", len(wanted))
    hhea[34:36] = struct.pack(">H", len(wanted))

    # One contiguous run of characters, which is all the subset holds.
    segments = [(FIRST, LAST), (0xFFFF, 0xFFFF)]
    segment_count = len(segments)

    cmap_subtable = bytearray()
    cmap_subtable.extend(struct.pack(">HHH", 4, 16 + segment_count * 8, 0))
    cmap_subtable.extend(struct.pack(">HHHH", segment_count * 2, 4, 1, 0))
    cmap_subtable.extend(struct.pack(f">{segment_count}H", *[end for _, end in segments]))
    cmap_subtable.extend(struct.pack(">H", 0))
    cmap_subtable.extend(struct.pack(f">{segment_count}H", *[start for start, _ in segments]))

    first_glyph = mapping[FIRST]
    cmap_subtable.extend(struct.pack(f">{segment_count}h", first_glyph - FIRST, 1))
    cmap_subtable.extend(struct.pack(f">{segment_count}H", 0, 0))

    cmap = bytearray(struct.pack(">HH", 0, 1))
    cmap.extend(struct.pack(">HHI", 3, 1, 12))
    cmap.extend(cmap_subtable)

    output = {
        "cmap": bytes(cmap),
        "glyf": bytes(new_glyf),
        "head": bytes(head),
        "hhea": bytes(hhea),
        "hmtx": bytes(new_hmtx),
        "loca": struct.pack(f">{len(new_loca)}I", *new_loca),
        "maxp": bytes(maxp),
    }

    tags = sorted(output)
    count = len(tags)

    search = 1
    entry = 0
    while search * 2 <= count:
        search *= 2
        entry += 1

    header = bytearray(struct.pack(">IHHHH", 0x00010000, count, search * 16, entry,
                                   (count - search) * 16))

    offset = 12 + count * 16
    directory = bytearray()
    body = bytearray()

    for tag in tags:
        payload = output[tag]
        directory.extend(tag.encode("latin1"))
        directory.extend(struct.pack(">III", 0, offset + len(body), len(payload)))

        body.extend(payload)
        if len(body) % 4 != 0:
            body.extend(b"\0" * (4 - len(body) % 4))

    with open(destination, "wb") as out:
        out.write(header)
        out.write(directory)
        out.write(body)

    print(
        f"{destination}: {len(wanted)} glyphs, {(len(header) + len(directory) + len(body)) // 1024} KiB",
        file=sys.stderr,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True)
    parser.add_argument("--out", required=True)
    arguments = parser.parse_args()

    build(arguments.font, arguments.out)


if __name__ == "__main__":
    main()
