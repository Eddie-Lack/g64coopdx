#!/usr/bin/env python3
"""Writes raft.gltf: a small wooden raft drawn from scratch, for the sample pack.

The raft is a flat box: 48 units wide (x), 6 units high (y) and 36 units deep
(z). Its top is two large triangles, which a glove can stand on. Its one
texture is a 16 by 16 picture of four planks, drawn here from a fixed
formula. The buffer and the PNG are data URIs inside the file. Nothing comes
from the game. Run it with no arguments; it writes raft.gltf next to itself.
"""

import base64
import json
import os
import struct
import zlib

# Half the size along each axis.
HALF = (24.0, 3.0, 18.0)
SIDE = 16


def png(width, height, rows):
    """A PNG of 8-bit RGBA rows, with no filter."""
    raw = b"".join(b"\x00" + bytes(row) for row in rows)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def grain(x, y):
    """A small whole number from 0 to 3 that changes along a plank, for its grain."""
    v = (x * 7 + y * 13 + (x * y) % 5) % 11
    return v % 4


def planks():
    """Four planks of four rows each. The last row of each plank is a dark seam."""
    seam = (70, 40, 20, 255)
    tones = [(170, 112, 60), (150, 96, 50), (182, 124, 70), (160, 104, 56)]
    rows = []
    for y in range(SIDE):
        row = []
        plank = y // 4
        for x in range(SIDE):
            if y % 4 == 3:
                row.extend(seam)
                continue
            r, g, b = tones[plank]
            d = 8 * grain(x, y)
            # A nail head near each end of a plank.
            if y % 4 == 1 and x in (1, SIDE - 2):
                row.extend((60, 60, 66, 255))
            else:
                row.extend((r - d, g - d, b - d // 2, 255))
        rows.append(row)
    return png(SIDE, SIDE, rows)


def box():
    # Each face: its normal, and two axes that span it. The corners go counter-clockwise seen from outside.
    faces = [
        ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
        ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
        ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
        ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
        ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
        ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
    ]
    pos, nrm, uv, idx = [], [], [], []
    for n, u, v in faces:
        base = len(pos)
        for su, sv, tu, tv in ((-1, -1, 0, 1), (1, -1, 1, 1), (1, 1, 1, 0), (-1, 1, 0, 0)):
            pos.append(tuple(HALF[a] * (n[a] + su * u[a] + sv * v[a]) for a in range(3)))
            nrm.append(n)
            uv.append((tu, tv))
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return pos, nrm, uv, idx


def main():
    pos, nrm, uv, idx = box()
    blob = b"".join(struct.pack("<3f", *p) for p in pos)
    blob += b"".join(struct.pack("<3f", *n) for n in nrm)
    blob += b"".join(struct.pack("<2f", *t) for t in uv)
    blob += b"".join(struct.pack("<H", i) for i in idx)
    n = len(pos)
    views = [
        {"buffer": 0, "byteOffset": 0, "byteLength": 12 * n, "target": 34962},
        {"buffer": 0, "byteOffset": 12 * n, "byteLength": 12 * n, "target": 34962},
        {"buffer": 0, "byteOffset": 24 * n, "byteLength": 8 * n, "target": 34962},
        {"buffer": 0, "byteOffset": 32 * n, "byteLength": 2 * len(idx), "target": 34963},
    ]
    doc = {
        "asset": {"version": "2.0", "generator": "make_raft.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "raft", "mesh": 0}],
        "meshes": [{"name": "raft", "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
            "indices": 3,
            "material": 0,
        }]}],
        "materials": [{"name": "planks", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 10497, "wrapT": 10497}],
        "images": [{"uri": "data:image/png;base64," + base64.b64encode(planks()).decode()}],
        "buffers": [{"byteLength": len(blob),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode()}],
        "bufferViews": views,
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": n, "type": "VEC3",
             "min": [-h for h in HALF], "max": list(HALF)},
            {"bufferView": 1, "componentType": 5126, "count": n, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": n, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": len(idx), "type": "SCALAR"},
        ],
    }
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "raft.gltf")
    with open(out, "w", newline="\n") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")


if __name__ == "__main__":
    main()
