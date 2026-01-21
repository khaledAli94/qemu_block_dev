#  What lives at sector 0?
Sector **0** contains:

- MBR boot code (first 446 bytes)
- Partition table (bytes 446–509)
- Signature `55 AA` (bytes 510–511)

So **MBR + partition table = sector 0**.

---

#  What lives at sector 2048?
Sector **2048** is the **first sector of your FAT32 partition**.

This sector contains the **FAT32 Boot Sector**, also called the **Volume Boot Record (VBR)**.

It includes:

- Jump instruction
- OEM name
- BPB (BIOS Parameter Block)
- FAT parameters
- Volume ID
- Volume label
- FAT32 extended BPB
- Boot code
- Signature `55 AA`

## calculation
```
absolute_sector = partition_start
                + reserved_sectors
                + (fat_size * number_of_fats)
                + (cluster_number - 2) * sectors_per_cluster
```

###  loop dev creation
```bash
sudo losetup --find --partscan --show ../sdcard.img
sudo mkdir -p /mnt/myimg_name
lsblk /dev/loop0
sudo mount /dev/loop0p1 /mnt/myimg_name
```

### file creation 
```bash
sudo nano /mnt/sd/hello.txt
sudo cp myfile.bin /mnt/sd/
```

### maintainance 
```bash
sudo filefrag -v /mnt/mysdcard/hello_world.txt
sudo fdisk -l ../sdcard.img
sudo fsck.fat -v /dev/loop0p1
```

### cleanup 
```bash
sudo umount /mnt/sd
sudo losetup -d /dev/loop0
```