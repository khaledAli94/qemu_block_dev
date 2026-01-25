#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stddef.h>

// --- On-Disk Structures ---

// Full 512-byte Boot Sector Definition
struct fat32_bootsector_t {
    // Jump instruction
    uint8_t jmp_boot[3];           // 0x00
    uint8_t oem_name[8];           // 0x03
    
    // BPB (BIOS Parameter Block)
    uint16_t bytes_per_sector;     // 0x0B
    uint8_t  sectors_per_cluster;  // 0x0D
    uint16_t reserved_sectors;     // 0x0E
    uint8_t  num_fats;             // 0x10
    uint16_t root_entry_count;     // 0x11
    uint16_t total_sectors_16;     // 0x13
    uint8_t  media_type;           // 0x15
    uint16_t fat_size_16;          // 0x16
    uint16_t sectors_per_track;    // 0x18
    uint16_t num_heads;            // 0x1A
    uint32_t hidden_sectors;       // 0x1C
    uint32_t total_sectors_32;     // 0x20
    
    // Extended BPB for FAT32
    uint32_t fat_size_32;          // 0x24
    uint16_t ext_flags;            // 0x28
    uint16_t fs_version;           // 0x2A
    uint32_t root_cluster;         // 0x2C
    uint16_t fs_info_sector;       // 0x30
    uint16_t backup_boot_sector;   // 0x32
    uint8_t  reserved[12];         // 0x34
    uint8_t  drive_number;         // 0x40
    uint8_t  reserved1;            // 0x41
    uint8_t  boot_signature;       // 0x42 (Extended Boot Sig)
    uint32_t volume_id;            // 0x43
    uint8_t  volume_label[11];     // 0x47
    uint8_t  fs_type[8];           // 0x52
    
    // Boot code and Sector Signature
    uint8_t  boot_code[420];       // 0x5A
    uint16_t boot_signature_word;  // 0x1FE (0xAA55)
} __attribute__((packed));

struct fat32_dir_entry_t {
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

struct fat32_fs_t {
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t root_cluster;
    uint32_t total_clusters;
    uint32_t fat_size_sectors;
    
    // Single Sector FAT Cache
    uint32_t cached_fat_sector; 
    uint8_t  fat_buffer[512] __attribute__((aligned(32))); 
    int      fat_dirty;
};

struct fat32_file_t {
    uint32_t start_cluster;
    uint32_t current_cluster;
    uint32_t size;
    uint32_t position;
    uint32_t dir_sector;    // Sector containing the dirent
    uint32_t dir_offset;    // Offset within that sector
};

// --- API ---

int fat32_mount(struct fat32_fs_t *fs);
int fat32_open(struct fat32_fs_t *fs, const char *path, struct fat32_file_t *out);
int fat32_read(struct fat32_fs_t *fs, struct fat32_file_t *file, void *buf, uint32_t size);
int fat32_write(struct fat32_fs_t *fs, struct fat32_file_t *file, const void *buf, uint32_t size);
int fat32_seek(struct fat32_fs_t *fs, struct fat32_file_t *file, uint32_t offset);
int fat32_close(struct fat32_fs_t *fs, struct fat32_file_t *file);

uint32_t fat32_cluster_to_lba(struct fat32_fs_t *fs, uint32_t cluster);

int fat32_create(struct fat32_fs_t *fs, const char *path, struct fat32_file_t *out);
#endif // FAT32_H