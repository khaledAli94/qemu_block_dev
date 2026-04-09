#ifndef FAT_H
#define FAT_H

#include <stdint.h>
#include <stddef.h>


// --- On-Disk Structures ---
struct fat_lfn_entry_t {
    uint8_t seqno; // (bit 6 = last LFN entry)
    uint16_t name_utf16_1[5];
    uint8_t attr;// Always 0x0F
    uint8_t type; // Always 0x00
    uint8_t checksum;  // Checksum of the 8.3 short name
    uint16_t name_utf16_2[6];
    uint16_t zero;
    uint16_t name_utf16_3[2];
} __attribute__ ((packed));

struct partition_entry_t {
    uint8_t  status;
    uint8_t  chs_start[3];
    uint8_t  type;           /* 0x06/0x0E=FAT16, 0x0B/0x0C=FAT32 */
    uint8_t  chs_end[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed));

/* master boot record */
struct mbr_t {
    uint8_t  bootstrap[446];
    struct partition_entry_t partitions[4];
    uint16_t signature; // 0xAA55
} __attribute__((packed));

/* extended boot sector */
struct fat16_bootsec_t {
    uint8_t  drive_number;       // 0x24
    uint8_t  reserved1;          // 0x25
    uint8_t  boot_signature;     // 0x26 (0x29 = extended boot signature)
    uint32_t volume_id;          // 0x27
    uint8_t  volume_label[11];   // 0x2B
    uint8_t  fs_type[8];         // 0x36 ("FAT16   ")
    uint8_t  boot_code[448];     // 0x3E
} __attribute__((packed));

struct fat32_bootsec_t {
    uint32_t fat_size_32;        // 0x24 (sectors per FAT)
    uint16_t ext_flags;          // 0x28
    uint16_t fs_version;         // 0x2A
    uint32_t root_cluster;       // 0x2C
    uint16_t fs_info;            // 0x30
    uint16_t backup_boot_sector; // 0x32
    uint8_t  reserved[12];       // 0x34

    uint8_t  drive_number;       // 0x40
    uint8_t  reserved1;          // 0x41
    uint8_t  boot_signature;     // 0x42 (0x29 = extended boot signature)
    uint32_t volume_id;          // 0x43
    uint8_t  volume_label[11];   // 0x47
    uint8_t  fs_type[8];         // 0x52 ("FAT32   ")

    uint8_t  boot_code[420];     // 0x5A
} __attribute__((packed));

// Full 512-byte Boot Sector Definition
struct fat_bootsector_t {
    // Jump instruction
    uint8_t jmp_boot[3];           // 0x00
    uint8_t oem_name[8];           // 0x03
    
    // BPB (BIOS Parameter Block)
    uint16_t bytes_per_sector;      // 0x0B
    uint8_t  sectors_per_cluster;   // 0x0D
    uint16_t reserved_sectors;      // 0x0E
    uint8_t  num_fats;              // 0x10
    uint16_t root_entry_count;      // 0x11
    uint16_t total_sectors_16;      // 0x13
    uint8_t  media_type;            // 0x15
    uint16_t fat_size_16;           // 0x16 // sectors_per_fat 0xee
    uint16_t sectors_per_track;     // 0x18
    uint16_t num_heads;             // 0x1A
    uint32_t hidden_sectors;        // 0x1C
    uint32_t total_sectors_32;      // 0x20
    
    // Boot code and Sector Signature
    union {
        struct fat16_bootsec_t e16; // 0x24
		struct fat32_bootsec_t e32; // 0x24
	} ext;

    uint16_t signature;          // 0x1FE (0xAA55)
} __attribute__((packed));

struct fat_dir_entry_t {
    uint8_t  name[11];      // 8.3 format
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  ctime_tenth;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t cluster_hi;
    uint16_t wtime;
    uint16_t wdate;
    uint16_t cluster_lo;
    uint32_t size;
} __attribute__((packed));

// --- Runtime Structures ---
struct fat_fs_t {
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t root_dir_lba;          // LBA of root directory (FAT16 only)
    uint32_t root_dir_sectors;      // number of sectors in root dir (FAT16)
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t root_cluster;          // FAT32 root cluster, 0 for FAT16
    uint32_t total_clusters;
    uint32_t fat_size_sectors;
    uint8_t  fat_entry_size;        // 2 for FAT16, 4 for FAT32
    uint16_t root_entry_count;      // from BPB (FAT16)
    uint8_t  num_fats;              // number of FAT copies
    
    // Single Sector FAT Cache
    uint32_t cached_fat_sector;
    uint8_t  fat_buffer[512] __attribute__((aligned(32)));
    int      fat_dirty;
};

struct fat_file_t {
    uint32_t start_cluster;
    uint32_t current_cluster;
    uint32_t size;
    uint32_t position;
    uint32_t dir_sector;    // Sector containing the dirent
    uint32_t dir_offset;    // Offset within that sector
};

// --- API ---

int fat_mount(struct fat_fs_t *fs);
int fat_open(struct fat_fs_t *fs, const char *path, struct fat_file_t *out);
int fat_read(struct fat_fs_t *fs, struct fat_file_t *file, void *buf, uint32_t size);
int fat_write(struct fat_fs_t *fs, struct fat_file_t *file, const void *buf, uint32_t size);
int fat_seek(struct fat_fs_t *fs, struct fat_file_t *file, uint32_t offset);
int fat_close(struct fat_fs_t *fs, struct fat_file_t *file);

uint32_t fat_cluster_to_lba(struct fat_fs_t *fs, uint32_t cluster);

int fat_create(struct fat_fs_t *fs, const char *path, struct fat_file_t *out);
#endif // FAT_H