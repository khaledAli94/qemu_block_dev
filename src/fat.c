#include "fat.h"
#include "sdhc.h"
#include "cache.h"

#define FAT16_EOF 0xFFF8
#define FAT32_EOF 0x0FFFFFFF
#define FAT_FREE 0x00000000

// Internal MBR Partition Entry Structure
struct mbr_partition_entry_t {
    uint8_t  status;
    uint8_t  chs_start[3];
    uint8_t  type;
    uint8_t  chs_end[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed));

// --- Internal Helpers ---

static void format_83_name(const char *path, int len, char *dest) {
    memset(dest, ' ', 11);
    int i = 0, ext_mode = 0, dest_idx = 0;
    for (i = 0; i < len; i++) {
        char c = path[i];
        if (c == '.') { ext_mode = 1; dest_idx = 8; continue; }
        if (c >= 'a' && c <= 'z') c -= 32;
        if (ext_mode) { if (dest_idx < 11) dest[dest_idx++] = c; }
        else { if (dest_idx < 8) dest[dest_idx++] = c; }
    }
}

static uint32_t get_next_cluster(struct fat_fs_t *fs, uint32_t current_cluster) {
    uint32_t entry_size = fs->fat_entry_size; // 2 or 4
    uint32_t fat_offset = current_cluster * entry_size;
    uint32_t fat_sector = fs->fat_start_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    if (fs->cached_fat_sector != fat_sector) {
        if (fs->fat_dirty) {
            cache_clean(fs->fat_buffer, 512);
            sd_write_block(fs->cached_fat_sector, fs->fat_buffer);
            fs->fat_dirty = 0;
        }
        if (sd_read_block(fat_sector, fs->fat_buffer) != 0) return (entry_size == 2) ? FAT16_EOF : FAT32_EOF;
        cache_invalidate(fs->fat_buffer, 512);
        fs->cached_fat_sector = fat_sector;
    }

    if (entry_size == 2) {
        uint16_t *entry = (uint16_t *)&fs->fat_buffer[ent_offset];
        return *entry;
    } else {
        uint32_t *entry = (uint32_t *)&fs->fat_buffer[ent_offset];
        return (*entry) & 0x0FFFFFFF;
    }
}

static int set_next_cluster(struct fat_fs_t *fs, uint32_t current_cluster, uint32_t next_cluster) {
    uint32_t entry_size = fs->fat_entry_size;
    uint32_t fat_offset = current_cluster * entry_size;
    uint32_t fat_sector = fs->fat_start_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    if (fs->cached_fat_sector != fat_sector) {
        if (fs->fat_dirty) {
            cache_clean(fs->fat_buffer, 512);
            sd_write_block(fs->cached_fat_sector, fs->fat_buffer);
        }
        if (sd_read_block(fat_sector, fs->fat_buffer) != 0) return -1;
        cache_invalidate(fs->fat_buffer, 512);
        fs->cached_fat_sector = fat_sector;
    }

    if (entry_size == 2) {
        uint16_t *entry = (uint16_t *)&fs->fat_buffer[ent_offset];
        *entry = (uint16_t)next_cluster;
    } else {
        uint32_t *entry = (uint32_t *)&fs->fat_buffer[ent_offset];
        *entry = (*entry & 0xF0000000) | (next_cluster & 0x0FFFFFFF);
    }
    fs->fat_dirty = 1;
    cache_clean(fs->fat_buffer, 512);
    return sd_write_block(fat_sector, fs->fat_buffer);
}

static uint32_t find_free_cluster(struct fat_fs_t *fs) {
    uint32_t eoc = (fs->fat_entry_size == 2) ? FAT16_EOF : FAT32_EOF;
    for (uint32_t i = 2; i < fs->total_clusters; i++) {
        if (get_next_cluster(fs, i) == FAT_FREE) return i;
    }
    return 0;
}

// --- Public API ---
uint32_t fat_cluster_to_lba(struct fat_fs_t *fs, uint32_t cluster) {
    if (cluster < 2) return 0;
    return fs->data_start_lba + ((cluster - 2) * fs->sectors_per_cluster);
}

int fat_mount(struct fat_fs_t *fs) {
    uint8_t buffer[512] __attribute__((aligned(32)));
    uint32_t partition_lba = 0;
    int is_fat32 = 0;

    // 1. Read MBR (sector 0)
    if (sd_read_block(0, buffer) != 0) return -1;
    cache_invalidate(buffer, 512);

    struct fat_bootsector_t *bpb = (struct fat_bootsector_t *)buffer;

    // 2. Check if sector 0 is a boot sector or MBR
    if (bpb->bytes_per_sector != 512) {
        // It's an MBR with partition table
        struct mbr_partition_entry_t *part = (struct mbr_partition_entry_t *)(buffer + 0x1BE);
        int found = 0;
        for (int i = 0; i < 4; i++) {
            uint8_t type = part[i].type;
            if (type == 0x01 || type == 0x04 || type == 0x06 ||
                type == 0x0E || type == 0x0B || type == 0x0C) {
                partition_lba = part[i].lba_start;
                found = 1;
                break;
            }
        }
        if (!found) return -2;
        if (sd_read_block(partition_lba, buffer) != 0) return -3;
        cache_invalidate(buffer, 512);
        bpb = (struct fat_bootsector_t *)buffer;
        if (bpb->bytes_per_sector != 512) return -4;
    }

    // 3. Common BPB fields
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->bytes_per_cluster = fs->sectors_per_cluster * 512;
    fs->fat_start_lba = partition_lba + bpb->reserved_sectors;
    fs->num_fats = bpb->num_fats;
    fs->root_entry_count = bpb->root_entry_count;

    // 4. Detect FAT type
    if (bpb->fat_size_16 != 0) {
        // ---------- FAT16 ----------
        is_fat32 = 0;
        fs->fat_entry_size = 2;
        fs->fat_size_sectors = bpb->fat_size_16;
        uint32_t root_dir_sectors = (fs->root_entry_count * 32 + 511) / 512;
        uint32_t root_dir_lba = fs->fat_start_lba + (fs->num_fats * fs->fat_size_sectors);
        fs->root_dir_lba = root_dir_lba;
        fs->root_dir_sectors = root_dir_sectors;
        fs->data_start_lba = root_dir_lba + root_dir_sectors;
        fs->root_cluster = 0;   // FAT16 has no root cluster

        uint32_t total_sectors = bpb->total_sectors_16 ? bpb->total_sectors_16 : bpb->total_sectors_32;
        uint32_t data_sectors = total_sectors - (fs->data_start_lba - partition_lba);
        fs->total_clusters = data_sectors / fs->sectors_per_cluster;
    } else {
        // ---------- FAT32 ----------
        is_fat32 = 1;
        fs->fat_entry_size = 4;
        fs->fat_size_sectors = bpb->ext.e32.fat_size_32;
        fs->data_start_lba = fs->fat_start_lba + (fs->num_fats * fs->fat_size_sectors);
        fs->root_cluster = bpb->ext.e32.root_cluster;
        // For FAT32, root directory is a cluster chain, no fixed LBA
        fs->root_dir_lba = 0;
        fs->root_dir_sectors = 0;

        uint32_t total_sectors = bpb->total_sectors_16 ? bpb->total_sectors_16 : bpb->total_sectors_32;
        uint32_t data_sectors = total_sectors - (fs->data_start_lba - partition_lba);
        fs->total_clusters = data_sectors / fs->sectors_per_cluster;
    }

    fs->cached_fat_sector = 0xFFFFFFFF;
    fs->fat_dirty = 0;
    return 0;
}

int fat_open(struct fat_fs_t *fs, const char *path, struct fat_file_t *out) {
    char target_name[11];
    uint32_t curr_cluster = fs->root_cluster;   // 0 for FAT16, valid cluster for FAT32
    const char *p = path;
    if (*p == '/') p++;

    while (*p) {
        const char *end = p;
        while (*end && *end != '/') end++;
        int len = end - p;
        format_83_name(p, len, target_name);

        int found = 0;
        struct fat_dir_entry_t found_entry;
        uint32_t found_dir_sector = 0;
        uint32_t found_dir_offset = 0;

        // ---------- Root Directory ----------
        if (curr_cluster == 0) {
            // FAT16: root directory in fixed sectors
            uint32_t root_lba = fs->root_dir_lba;
            uint32_t root_sectors = fs->root_dir_sectors;

            for (uint32_t s = 0; s < root_sectors; s++) {
                uint8_t buffer[512] __attribute__((aligned(32)));
                if (sd_read_block(root_lba + s, buffer) != 0) break;
                cache_invalidate(buffer, 512);
                struct fat_dir_entry_t *entries = (struct fat_dir_entry_t *)buffer;
                // 512 bytes / 32 = 16 entries per sector
                for (int i = 0; i < 16; i++) {
                    if (entries[i].name[0] == 0x00) goto root_done; // end of directory
                    if (entries[i].name[0] == 0xE5) continue;       // deleted entry
                    if (memcmp(entries[i].name, target_name, 11) == 0) {
                        found = 1;
                        memcpy(&found_entry, &entries[i], sizeof(struct fat_dir_entry_t));
                        found_dir_sector = root_lba + s;
                        found_dir_offset = i * 32;
                        goto root_done;
                    }
                }
            }
            root_done:
            if (!found) return -2;
        } else {
            // ---------- Subdirectory (FAT32 or FAT16 subdir) ----------
            uint32_t search_cluster = curr_cluster;
            uint32_t eoc = (fs->fat_entry_size == 2) ? FAT16_EOF : FAT32_EOF;
            while (search_cluster >= 2 && search_cluster < eoc) {
                uint32_t lba = fat_cluster_to_lba(fs, search_cluster);
                uint8_t buffer[512] __attribute__((aligned(32)));
                for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
                    if (sd_read_block(lba + s, buffer) != 0) return -1;
                    cache_invalidate(buffer, 512);
                    struct fat_dir_entry_t *entries = (struct fat_dir_entry_t *)buffer;
                    for (int i = 0; i < 16; i++) {
                        if (entries[i].name[0] == 0x00) goto cluster_done;
                        if (entries[i].name[0] == 0xE5) continue;
                        if (memcmp(entries[i].name, target_name, 11) == 0) {
                            found = 1;
                            memcpy(&found_entry, &entries[i], sizeof(struct fat_dir_entry_t));
                            found_dir_sector = lba + s;
                            found_dir_offset = i * 32;
                            goto cluster_done;
                        }
                    }
                }
                search_cluster = get_next_cluster(fs, search_cluster);
            }
            cluster_done:
            if (!found) return -2;
        }

        // Move to next path component
        curr_cluster = (found_entry.cluster_hi << 16) | found_entry.cluster_lo;
        p = end;
        if (*p == '/') p++;
        if (*p == '\0') {
            out->start_cluster = curr_cluster;
            out->current_cluster = curr_cluster;
            out->size = found_entry.size;
            out->position = 0;
            out->dir_sector = found_dir_sector;
            out->dir_offset = found_dir_offset;
            return 0;
        }
    }
    return -3;
}

// Helper: find a free entry in a FAT16 root directory (fixed sectors)
static int find_free_entry_in_root(struct fat_fs_t *fs, uint32_t *out_sector, uint32_t *out_offset) {
    for (uint32_t s = 0; s < fs->root_dir_sectors; s++) {
        uint8_t buffer[512] __attribute__((aligned(32)));
        if (sd_read_block(fs->root_dir_lba + s, buffer) != 0) continue;
        cache_invalidate(buffer, 512);
        struct fat_dir_entry_t *entries = (struct fat_dir_entry_t *)buffer;
        for (int i = 0; i < 16; i++) {
            if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                *out_sector = fs->root_dir_lba + s;
                *out_offset = i * 32;
                return 1;
            }
        }
    }
    return 0; // no free slot
}

// Helper: find a free entry in a directory cluster chain
static int find_free_entry_in_cluster(struct fat_fs_t *fs, uint32_t dir_cluster, uint32_t *out_sector, uint32_t *out_offset) {
    uint32_t eoc = (fs->fat_entry_size == 2) ? FAT16_EOF : FAT32_EOF;
    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < eoc) {
        uint32_t lba = fat_cluster_to_lba(fs, cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            uint8_t buffer[512] __attribute__((aligned(32)));
            if (sd_read_block(lba + s, buffer) != 0) continue;
            cache_invalidate(buffer, 512);
            struct fat_dir_entry_t *entries = (struct fat_dir_entry_t *)buffer;
            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                    *out_sector = lba + s;
                    *out_offset = i * 32;
                    return 1;
                }
            }
        }
        cluster = get_next_cluster(fs, cluster);
    }
    return 0;
}

// Main create function
int fat_create(struct fat_fs_t *fs, const char *path, struct fat_file_t *out) {
    if (!path || !*path) return -1;

    // Extract the last component (file name) and the parent directory path
    const char *last_slash = NULL;
    const char *p = path;
    if (*p == '/') p++;
    while (*p) {
        if (*p == '/') last_slash = p;
        p++;
    }
    const char *filename = (last_slash) ? last_slash + 1 : path;
    // Parent path is the substring before last slash (or empty for root)
    char parent_path[256];
    if (last_slash) {
        size_t parent_len = last_slash - path;
        if (parent_len >= sizeof(parent_path)) return -2;
        memcpy(parent_path, path, parent_len);
        parent_path[parent_len] = '\0';
    } else {
        parent_path[0] = '\0'; // root
    }

    // 1. Open the parent directory
    struct fat_file_t parent_file;
    if (parent_path[0] == '\0') {
        // Root directory – special handling
        parent_file.start_cluster = 0;      // signal root
        parent_file.current_cluster = 0;
        parent_file.size = 0;               // not used
        parent_file.position = 0;
    } else {
        int ret = fat_open(fs, parent_path, &parent_file);
        if (ret != 0) return ret; // parent not found
    }

    // 2. Format the 8.3 name
    char target_name[11];
    format_83_name(filename, strlen(filename), target_name);

    // 3. Find a free slot in the parent directory
    uint32_t free_sector = 0, free_offset = 0;
    int found = 0;
    if (parent_file.start_cluster == 0) {
        // FAT16 root directory
        found = find_free_entry_in_root(fs, &free_sector, &free_offset);
    } else {
        // Subdirectory (FAT32 or FAT16 subdir)
        found = find_free_entry_in_cluster(fs, parent_file.start_cluster, &free_sector, &free_offset);
    }
    if (!found) return -3; // no free entry

    // 4. Allocate a new cluster for the file (start with empty)
    uint32_t new_cluster = find_free_cluster(fs);
    if (new_cluster == 0) return -4;
    uint32_t eoc = (fs->fat_entry_size == 2) ? FAT16_EOF : FAT32_EOF;
    set_next_cluster(fs, new_cluster, eoc);
    // Clear the cluster (optional, but good practice)
    uint8_t zero_sector[512] __attribute__((aligned(32)));
    memset(zero_sector, 0, 512);
    cache_clean(zero_sector, 512);
    uint32_t lba = fat_cluster_to_lba(fs, new_cluster);
    for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
        sd_write_block(lba + i, zero_sector);
    }

    // 5. Write the directory entry
    uint8_t sector_buf[512] __attribute__((aligned(32)));
    if (sd_read_block(free_sector, sector_buf) != 0) return -5;
    cache_invalidate(sector_buf, 512);
    struct fat_dir_entry_t *d = (struct fat_dir_entry_t *)(sector_buf + free_offset);
    memset(d, 0, 32);
    memcpy(d->name, target_name, 11);
    d->attr = 0x20;                     // archive
    d->cluster_hi = (uint16_t)(new_cluster >> 16);
    d->cluster_lo = (uint16_t)(new_cluster & 0xFFFF);
    d->size = 0;
    cache_clean(sector_buf, 512);
    sd_write_block(free_sector, sector_buf);

    // 6. Initialize the output file structure
    out->start_cluster = new_cluster;
    out->current_cluster = new_cluster;
    out->size = 0;
    out->position = 0;
    out->dir_sector = free_sector;
    out->dir_offset = free_offset;

    return 0;
}

int fat_read(struct fat_fs_t *fs, struct fat_file_t *file, void *buf, uint32_t size) {
    if (file->position >= file->size) return 0;
    if (file->position + size > file->size) size = file->size - file->position;

    uint8_t *ptr = (uint8_t *)buf;
    uint32_t bytes_read = 0;
    uint8_t scratch[512] __attribute__((aligned(32)));

    while (size > 0) {
        uint32_t cluster_offset = file->position % fs->bytes_per_cluster;
        uint32_t sector_idx = cluster_offset / 512;
        uint32_t byte_idx = cluster_offset % 512;

        uint32_t lba = fat_cluster_to_lba(fs, file->current_cluster) + sector_idx;
        int is_aligned = (((uintptr_t)ptr & 0x3) == 0);

        if (byte_idx == 0 && size >= 512 && is_aligned) {
            if (sd_read_block(lba, ptr) != 0) break;
            cache_invalidate(ptr, 512);
            ptr += 512; size -= 512; file->position += 512; bytes_read += 512;
        } else {
            if (sd_read_block(lba, scratch) != 0) break;
            cache_invalidate(scratch, 512);
            uint32_t chunk = 512 - byte_idx;
            if (chunk > size) chunk = size;
            memcpy(ptr, scratch + byte_idx, chunk);
            ptr += chunk; size -= chunk; file->position += chunk; bytes_read += chunk;
        }

        if (file->position % fs->bytes_per_cluster == 0 && file->position < file->size) {
            file->current_cluster = get_next_cluster(fs, file->current_cluster);
        }
    }
    return bytes_read;
}

int fat_write(struct fat_fs_t *fs, struct fat_file_t *file, const void *buf, uint32_t size) {
    if (file->dir_sector == 0) return -9;

    const uint8_t *ptr = (const uint8_t *)buf;
    uint32_t bytes_written = 0;
    uint8_t scratch[512] __attribute__((aligned(32)));

    while (size > 0) {
        if (file->start_cluster == 0) {
            uint32_t new_c = find_free_cluster(fs);
            if (new_c == 0) return -1;
            uint32_t eoc = (fs->fat_entry_size == 2) ? FAT16_EOF : FAT32_EOF;
            set_next_cluster(fs, new_c, eoc);
            memset(scratch, 0, 512);
            cache_clean(scratch, 512);
            uint32_t lba = fat_cluster_to_lba(fs, new_c);
            for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) sd_write_block(lba + i, scratch);
            file->start_cluster = new_c;
            file->current_cluster = new_c;

            // Update directory entry
            if (sd_read_block(file->dir_sector, scratch) == 0) {
                cache_invalidate(scratch, 512);
                struct fat_dir_entry_t *d = (struct fat_dir_entry_t *)(scratch + file->dir_offset);
                d->cluster_hi = (uint16_t)(new_c >> 16);
                d->cluster_lo = (uint16_t)(new_c & 0xFFFF);
                cache_clean(scratch, 512);
                sd_write_block(file->dir_sector, scratch);
            }
        }

        uint32_t cluster_offset = file->position % fs->bytes_per_cluster;
        uint32_t sector_idx = cluster_offset / 512;
        uint32_t byte_idx = cluster_offset % 512;
        uint32_t lba = fat_cluster_to_lba(fs, file->current_cluster) + sector_idx;

        if (byte_idx != 0 || size < 512) {
            sd_read_block(lba, scratch);
            cache_invalidate(scratch, 512);
            uint32_t chunk = 512 - byte_idx;
            if (chunk > size) chunk = size;
            memcpy(scratch + byte_idx, ptr, chunk);
            cache_clean(scratch, 512);
            sd_write_block(lba, scratch);
            ptr += chunk; size -= chunk; file->position += chunk; bytes_written += chunk;
        } else {
            memcpy(scratch, ptr, 512);
            cache_clean(scratch, 512);
            sd_write_block(lba, scratch);
            ptr += 512; size -= 512; file->position += 512; bytes_written += 512;
        }

        if (file->position % fs->bytes_per_cluster == 0 && size > 0) {
            uint32_t next = get_next_cluster(fs, file->current_cluster);
            uint32_t eoc = (fs->fat_entry_size == 2) ? FAT16_EOF : FAT32_EOF;
            if (next >= eoc) {
                uint32_t new_c = find_free_cluster(fs);
                if (new_c == 0) return -1;
                set_next_cluster(fs, file->current_cluster, new_c);
                set_next_cluster(fs, new_c, eoc);
                file->current_cluster = new_c;
                memset(scratch, 0, 512);
                cache_clean(scratch, 512);
                uint32_t lba_next = fat_cluster_to_lba(fs, new_c);
                for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) sd_write_block(lba_next + i, scratch);
            } else {
                file->current_cluster = next;
            }
        }
    }

    if (file->position > file->size) {
        file->size = file->position;
        sd_read_block(file->dir_sector, scratch);
        cache_invalidate(scratch, 512);
        struct fat_dir_entry_t *d = (struct fat_dir_entry_t *)(scratch + file->dir_offset);
        d->size = file->size;
        cache_clean(scratch, 512);
        sd_write_block(file->dir_sector, scratch);
    }
    return bytes_written;
}

int fat_seek(struct fat_fs_t *fs, struct fat_file_t *file, uint32_t offset) {
    if (offset > file->size) return -1;
    file->position = offset;
    file->current_cluster = file->start_cluster;
    uint32_t clusters_to_skip = offset / fs->bytes_per_cluster;
    while (clusters_to_skip--) {
        file->current_cluster = get_next_cluster(fs, file->current_cluster);
    }
    return 0;
}

int fat_close(struct fat_fs_t *fs, struct fat_file_t *file) {
    (void)file;
    if (fs->fat_dirty) {
        cache_clean(fs->fat_buffer, 512);
        sd_write_block(fs->cached_fat_sector, fs->fat_buffer);
        fs->fat_dirty = 0;
    }
    return 0;
}
