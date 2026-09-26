#!/usr/bin/env python3
"""Export captured CPU vertex inputs without images, palettes or guest addresses.

The resulting local binary is input to capture_vertex_replay.cpp, not a source
asset to commit. Snapshot revisions preserve observed state changes; the capture
does not contain original command-list boundaries or redundant matrix writes.
"""
import collections
import json
from pathlib import Path
import struct
import sys
import zipfile


def export(capture, output):
    with zipfile.ZipFile(capture) as archive:
        names = sorted((n for n in archive.namelist()
                        if n.startswith("draws/") and n.endswith(".json")),
                       key=lambda n: int(n.split("/")[1].split(".")[0]))
        if not names or len(names) > 100000:
            raise ValueError("Invalid captured draw count")
        lighting_ops = [0x17, *range(0x18, 0x1c), *range(0x53, 0x9b)]
        vertex_ops = [0x12, 0x1f, *range(0x42, 0x4e), 0x51,
                      *range(0xb8, 0xc0), 0xc0, 0xc1, 0xcd, 0xce]
        last_light = last_vertex = None
        light_revision = vertex_revision = 0
        formats = collections.Counter()
        vertices = 0
        with open(output, "wb") as stream:
            stream.write(struct.pack("<8sI", b"LCSVTX1\0", len(names)))
            for name in names:
                prefix = name[:-5]
                metadata = json.loads(archive.read(name))
                command_bytes = archive.read(prefix + ".commands")
                matrices = archive.read(prefix + ".matrices")
                records = archive.read(prefix + ".vertices")
                if len(command_bytes) != 1024 or len(matrices) != 624 or len(records) > 4*1024*1024:
                    raise ValueError("Malformed captured vertex inputs")
                commands = struct.unpack("<256I", command_bytes)
                index_size = (0, 1, 2, 4)[(commands[0x12] >> 11) & 3]
                indices = archive.read(prefix + ".indices") if index_size else b""
                count = metadata["primitive"] & 65535
                if index_size and len(indices) != count * index_size:
                    raise ValueError("Incomplete captured index stream")
                light = (tuple(commands[i] for i in lighting_ops),
                         ((commands[0x12] >> 2) & 7) >= 4)
                vertex = (tuple(commands[i] for i in vertex_ops), light, matrices)
                light_revision += light != last_light
                vertex_revision += vertex != last_vertex
                last_light, last_vertex = light, vertex
                stream.write(struct.pack("<5I", metadata["primitive"], len(records),
                                         len(indices), vertex_revision, light_revision))
                stream.write(command_bytes)
                stream.write(matrices)
                stream.write(records)
                stream.write(indices)
                vertices += count
                formats[hex(commands[0x12])] += count
    print(json.dumps({"draws": len(names), "vertices": vertices,
                      "vertices_by_format": formats,
                      "observed_vertex_state_changes": vertex_revision,
                      "observed_lighting_state_changes": light_revision,
                      "fixture_bytes": Path(output).stat().st_size}, indent=2))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: capture_vertex_fixture.py capture.zip output.bin")
    export(sys.argv[1], sys.argv[2])
