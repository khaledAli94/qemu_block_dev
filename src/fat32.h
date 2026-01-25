#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

// FAT32 BIOS Parameter Block (BPB) and Extended BPB
struct fat32_bootsector_t {
    // Jump instruction
    uint8_t jmp_boot[3];
    uint8_t oem_name[8];
    
    // BPB BIOS Parameter Block
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    
    // Extended BPB for FAT32
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
    
    // Boot code and signature
    uint8_t boot_code[420];
    uint16_t boot_signature_word;
} __attribute__((packed));

// FAT32 Directory Entry
struct fat32_dir_entry_t {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_res;
    uint8_t crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t first_cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} __attribute__((packed));

// FAT32 File Handle
struct fat32_file_t {
    uint32_t first_cluster;   // First cluster of file
    uint32_t current_cluster; // Current cluster being read
    uint32_t size;            // File size in bytes
    uint32_t position;        // Current read position
    uint32_t sector_in_cluster; // Sector within current cluster (0-based)
    uint32_t byte_in_sector;  // Byte within current sector
    uint32_t sectors_per_cluster;
    uint32_t data_start_sector; // First sector of data region
};

// FAT32 File System Structure
struct fat32_fs_t {
    struct fat32_bootsector_t bpb;
    uint32_t fat_start_sector;
    uint32_t data_start_sector;
    uint32_t root_dir_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_sector;
    uint32_t fat_size_sectors;
    uint32_t total_clusters;
    uint8_t fat_cache[512];  // Cache for FAT entries
    uint32_t fat_cache_sector; // Which FAT sector is cached
};

// File Attributes
#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LONG_NAME  0x0F

// FAT32 API Functions
int fat32_init(struct fat32_fs_t *fs, uint32_t start_sector);
int fat32_open_root(struct fat32_fs_t *fs, struct fat32_file_t *dir);
int fat32_open_file(struct fat32_fs_t *fs, struct fat32_file_t *file, 
                    const char *path);
int fat32_read_dir(struct fat32_fs_t *fs, struct fat32_file_t *dir, 
                   struct fat32_dir_entry_t *entry);
int fat32_read_file(struct fat32_fs_t *fs, struct fat32_file_t *file, 
                    void *buffer, uint32_t size);
int fat32_seek(struct fat32_fs_t *fs, struct fat32_file_t *file, 
               uint32_t position);
void fat32_close(struct fat32_file_t *file);
int fat32_file_exists(struct fat32_fs_t *fs, const char *path);

// Utility functions
uint32_t fat32_get_cluster_sector(struct fat32_fs_t *fs, uint32_t cluster);
uint32_t fat32_get_next_cluster(struct fat32_fs_t *fs, uint32_t cluster);
void fat32_filename_to_dir_format(const char *filename, char *dir_format);
void fat32_dir_format_to_filename(const char *dir_format, char *filename);

#endif // FAT32_H