"""Read the boot executable from the Mode-2 CDI layout produced by cdi4dc."""
import struct

SESSION_LBA = 11702
SECTOR_SIZE = 2336
DATA_SIZE = 2048
MAX_EXECUTABLE = 8 * 1024 * 1024


def read_boot_file(data, expected_name):
    start = data.find(b'SEGA SEGAKATANA SEGA ENTERPRISES')
    if start < 0:
        raise ValueError('CDI bootstrap not found')

    def sector(index):
        offset = start + index * SECTOR_SIZE
        if index < 0 or offset + DATA_SIZE > len(data):
            raise ValueError('CDI file extent is outside the image')
        return data[offset:offset + DATA_SIZE]

    def extent(lba, size):
        return b''.join(sector(lba - SESSION_LBA + i)
                        for i in range((size + DATA_SIZE - 1) // DATA_SIZE))[:size]

    if sector(0)[96:112].rstrip(b' ') != expected_name.encode('ascii'):
        raise ValueError('CDI boot filename does not match its executable')
    pvd = sector(16)
    if pvd[:7] != b'\x01CD001\x01' or struct.unpack_from('<H', pvd, 128)[0] != DATA_SIZE:
        raise ValueError('CDI ISO9660 primary volume descriptor is invalid')
    root_lba = struct.unpack_from('<I', pvd, 158)[0]
    root_size = struct.unpack_from('<I', pvd, 166)[0]
    if not 0 < root_size <= 1024 * 1024:
        raise ValueError('CDI root directory size is invalid')
    directory = extent(root_lba, root_size)
    offset, matches = 0, []
    while offset < len(directory):
        length = directory[offset]
        if length == 0:
            offset = (offset // DATA_SIZE + 1) * DATA_SIZE
            continue
        record = directory[offset:offset + length]
        if length < 34 or len(record) != length or 33 + record[32] > length:
            raise ValueError('CDI directory record is truncated')
        name = record[33:33 + record[32]].split(b';')[0]
        if name == expected_name.encode('ascii'):
            if record[25] & (2 | 128):
                raise ValueError('CDI boot executable is a directory or split extent')
            lba, size = struct.unpack_from('<I', record, 2)[0], struct.unpack_from('<I', record, 10)[0]
            if not 4 <= size <= MAX_EXECUTABLE or size % 4:
                raise ValueError('CDI boot executable size is invalid')
            matches.append(extent(lba, size))
        offset += length
    if len(matches) != 1:
        raise ValueError('CDI must contain exactly one matching boot executable')
    return matches[0]


def descramble(data):
    seed, offset = len(data) & 0xffff, 0
    result = bytearray(len(data))
    chunk = 2 * 1024 * 1024
    while chunk >= 32:
        while len(data) - offset >= chunk:
            indices = list(range(chunk // 32))
            for i in range(len(indices) - 1, -1, -1):
                seed = (seed * 2109 + 9273) & 0x7fff
                other = (((seed + 0xc000) & 0xffff) * i) >> 16
                indices[i], indices[other] = indices[other], indices[i]
                source = offset + (len(indices) - 1 - i) * 32
                dest = offset + indices[i] * 32
                result[dest:dest + 32] = data[source:source + 32]
            offset += chunk
        chunk //= 2
    result[offset:] = data[offset:]
    return bytes(result)


def verify_boot_payload(data, expected_name, expected_raw):
    if descramble(read_boot_file(data, expected_name)) != expected_raw:
        raise ValueError(f'CDI {expected_name} differs from the compiled executable')
