"""Read numeric resources from a PE32+ executable without executing Windows code."""
import struct


def read_resource(data, resource_type, resource_id):
    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError("Truncated PE structure")
        return struct.unpack_from(fmt, data, offset)

    pe, = unpack("<I", 0x3C)
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    sections, = unpack("<H", pe + 6)
    optional_size, = unpack("<H", pe + 20)
    optional = pe + 24
    if unpack("<H", optional)[0] != 0x20B or optional_size < 136:
        raise ValueError("Expected PE32+ resource directory")
    resource_rva, resource_size = unpack("<II", optional + 128)
    section_table = optional + optional_size

    def offset_for(rva, size):
        for i in range(sections):
            section = section_table + i * 40
            _, address, raw_size, raw_offset = unpack("<IIII", section + 8)
            if address <= rva and rva - address + size <= raw_size:
                offset = raw_offset + rva - address
                if offset + size <= len(data):
                    return offset
        raise ValueError("PE resource points outside file-backed sections")

    base = offset_for(resource_rva, resource_size)

    def resource_offset(relative, size):
        if relative < 0 or relative + size > resource_size:
            raise ValueError("Resource directory is out of bounds")
        return base + relative

    directory = 0
    for wanted in (resource_type, resource_id, None):
        header = resource_offset(directory, 16)
        named, numbered = unpack("<HH", header + 12)
        selected = None
        for i in range(named + numbered):
            name, target = unpack("<II", resource_offset(directory + 16 + i * 8, 8))
            if wanted is None or name == wanted:
                selected = target
                break
        if selected is None:
            raise ValueError(f"Missing PE resource: {resource_type}/{resource_id}")
        is_directory = bool(selected & 0x80000000)
        directory = selected & 0x7FFFFFFF
        if is_directory != (wanted is not None):
            raise ValueError("Unexpected resource directory depth")
    rva, size = unpack("<II", resource_offset(directory, 16))
    offset = offset_for(rva, size)
    return data[offset:offset + size]
