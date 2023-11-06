def read_bpb(filename):
    with open(filename, 'rb') as f:
        boot_sector = f.read(512)

    bpb = {
        'BytesPerSector': int.from_bytes(boot_sector[11:13], 'little'),
        'SectorsPerCluster': boot_sector[13],
        'ReservedSectors': int.from_bytes(boot_sector[14:16], 'little'),
        'NumberOfFATs': boot_sector[16],
        'RootEntries': int.from_bytes(boot_sector[17:19], 'little'),
        'TotalSectors16': int.from_bytes(boot_sector[19:21], 'little'),
        'MediaDescriptor': boot_sector[21],
        'SectorsPerFAT16': int.from_bytes(boot_sector[22:24], 'little'),
        'SectorsPerTrack': int.from_bytes(boot_sector[24:26], 'little'),
        'Heads': int.from_bytes(boot_sector[26:28], 'little'),
        'HiddenSectors': int.from_bytes(boot_sector[28:32], 'little'),
        'TotalSectors32': int.from_bytes(boot_sector[32:36], 'little'),
    }

    # Print BPB
    for key, value in bpb.items():
        print(f"{key}: {value}")

if __name__ == '__main__':
    read_bpb('bootsector.bin')

