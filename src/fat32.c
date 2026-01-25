#include "fat32.h"
#include "sdhc.h"
#include "cache.h"

#define FAT_EOF 0x0FFFFFFF
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

static uint32_t get_next_cluster(struct fat32_fs_t *fs, uint32_t current_cluster) {
    uint32_t fat_offset = current_cluster * 4;
    uint32_t fat_sector = fs->fat_start_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    if (fs->cached_fat_sector != fat_sector) {
        if (fs->fat_dirty) {
            cache_clean(fs->fat_buffer, 512);
            sd_write_block(fs->cached_fat_sector, fs->fat_buffer);
            fs->fat_dirty = 0;
        }
        if (sd_read_block(fat_sector, fs->fat_buffer) != 0) return FAT_EOF;
        cache_invalidate(fs->fat_buffer, 512);
        fs->cached_fat_sector = fat_sector;
    }
    uint32_t *entry = (uint32_t *)&fs->fat_buffer[ent_offset];
    return (*entry) & 0x0FFFFFFF;
}

static int set_next_cluster(struct fat32_fs_t *fs, uint32_t current_cluster, uint32_t next_cluster) {
    uint32_t fat_offset = current_cluster * 4;
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
    uint32_t *entry = (uint32_t *)&fs->fat_buffer[ent_offset];
    *entry = (*entry & 0xF0000000) | (next_cluster & 0x0FFFFFFF);
    fs->fat_dirty = 1;
    cache_clean(fs->fat_buffer, 512);
    return sd_write_block(fat_sector, fs->fat_buffer);
}

static uint32_t find_free_cluster(struct fat32_fs_t *fs) {
    for (uint32_t i = 2; i < fs->total_clusters; i++) {
        if (get_next_cluster(fs, i) == FAT_FREE) return i;
    }
    return 0; 
}

// --- Public API ---

uint32_t fat32_cluster_to_lba(struct fat32_fs_t *fs, uint32_t cluster) {
    if (cluster < 2) return 0;
    return fs->data_start_lba + ((cluster - 2) * fs->sectors_per_cluster);
}

int fat32_mount(struct fat32_fs_t *fs) {
    uint8_t buffer[512] __attribute__((aligned(32)));
    uint32_t partition_lba = 0;

    // 1. Read Sector 0
    if (sd_read_block(0, buffer) != 0) return -1;
    cache_invalidate(buffer, 512);

    struct fat32_bootsector_t *bpb = (struct fat32_bootsector_t *)buffer;

    // 2. MBR Check
    if (bpb->bytes_per_sector != 512) {
        struct mbr_partition_entry_t *part = (struct mbr_partition_entry_t *)(buffer + 0x1BE);
        memcpy(&partition_lba, &part->lba_start, 4);
        if (partition_lba == 0) return -2;

        if (sd_read_block(partition_lba, buffer) != 0) return -3;
        cache_invalidate(buffer, 512);
        bpb = (struct fat32_bootsector_t *)buffer;
        if (bpb->bytes_per_sector != 512) return -4;
    }

    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->bytes_per_cluster = bpb->sectors_per_cluster * 512;
    fs->fat_start_lba = partition_lba + bpb->reserved_sectors;
    fs->fat_size_sectors = bpb->fat_size_32;
    uint32_t root_dir_lba = fs->fat_start_lba + (bpb->num_fats * fs->fat_size_sectors);
    fs->data_start_lba = root_dir_lba;
    fs->root_cluster = bpb->root_cluster;
    fs->total_clusters = bpb->total_sectors_32 / bpb->sectors_per_cluster;
    fs->cached_fat_sector = 0xFFFFFFFF;
    fs->fat_dirty = 0;
    return 0;
}

int fat32_open(struct fat32_fs_t *fs, const char *path, struct fat32_file_t *out) {
    char target_name[11];
    uint32_t curr_cluster = fs->root_cluster;
    const char *p = path;
    if (*p == '/') p++;

    while (*p) {
        const char *end = p;
        while (*end && *end != '/') end++;
        int len = end - p;
        
        format_83_name(p, len, target_name);
        
        uint32_t search_cluster = curr_cluster;
        int found = 0;
        struct fat32_dir_entry_t found_entry;
        uint32_t found_dir_sector = 0;
        uint32_t found_dir_offset = 0;

        while (search_cluster >= 2 && search_cluster < FAT_EOF) {
            uint32_t lba = fat32_cluster_to_lba(fs, search_cluster);
            uint8_t buffer[512] __attribute__((aligned(32)));

            for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
                if (sd_read_block(lba + s, buffer) != 0) return -1;
                cache_invalidate(buffer, 512);
                struct fat32_dir_entry_t *entries = (struct fat32_dir_entry_t *)buffer;
                for (int i = 0; i < 16; i++) {
                    if (entries[i].name[0] == 0x00) goto chain_end;
                    if (entries[i].name[0] == 0xE5) continue;
                    if (memcmp(entries[i].name, target_name, 11) == 0) {
                        found = 1;
                        memcpy(&found_entry, &entries[i], sizeof(struct fat32_dir_entry_t));
                        found_dir_sector = lba + s;
                        found_dir_offset = i * 32;
                        goto entry_found;
                    }
                }
            }
            search_cluster = get_next_cluster(fs, search_cluster);
        }
chain_end:
        if (!found) return -2;

entry_found:
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

// Create a file (simple implementation: assumes path contains existing parent dirs)
int fat32_create(struct fat32_fs_t *fs, const char *path, struct fat32_file_t *out) {
    char target_name[11];
    uint32_t parent_cluster = fs->root_cluster;
    
    // Parse Path - Find Parent Dir
    const char *p = path;
    const char *last_slash = NULL;
    
    if (*p == '/') p++;
    
    // Walk to find last component
    const char *temp = p;
    while (*temp) {
        if (*temp == '/') last_slash = temp;
        temp++;
    }

    // If path has subdirs, we need to navigate there first (Omitted for brevity, assuming Root)
    // To properly support "DIR/FILE.TXT", we would reuse the logic in fat32_open
    // For now, we assume creation in the implied root/current context or handle simple parsing.
    
    // Simple: Extract Filename
    const char *filename_start = (last_slash) ? last_slash + 1 : p;
    format_83_name(filename_start, 0xFF, target_name); // 0xFF uses string length
    
    // Find Free Entry in Parent Cluster
    uint32_t search_cluster = parent_cluster;
    uint32_t free_sector = 0;
    uint32_t free_offset = 0;
    int found_slot = 0;

    while (search_cluster >= 2 && search_cluster < FAT_EOF) {
        uint32_t lba = fat32_cluster_to_lba(fs, search_cluster);
        uint8_t buffer[512] __attribute__((aligned(32)));

        for (uint32_t s = 0; s < fs->sectors_per_cluster; s++) {
            if (sd_read_block(lba + s, buffer) != 0) return -1;
            cache_invalidate(buffer, 512);

            struct fat32_dir_entry_t *entries = (struct fat32_dir_entry_t *)buffer;
            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                    free_sector = lba + s;
                    free_offset = i * 32;
                    found_slot = 1;
                    goto slot_found;
                }
            }
        }
        
        // Extend Directory if needed
        uint32_t next = get_next_cluster(fs, search_cluster);
        if (next >= FAT_EOF) {
            uint32_t new_c = find_free_cluster(fs);
            if (new_c == 0) return -2; // Full
            set_next_cluster(fs, search_cluster, new_c);
            set_next_cluster(fs, new_c, FAT_EOF);
            
            // Clear new cluster
            uint8_t zero[512] __attribute__((aligned(32)));
            memset(zero, 0, 512);
            cache_clean(zero, 512);
            uint32_t lba_n = fat32_cluster_to_lba(fs, new_c);
            for(uint32_t k=0; k<fs->sectors_per_cluster; k++) sd_write_block(lba_n+k, zero);
            
            search_cluster = new_c;
        } else {
            search_cluster = next;
        }
    }

slot_found:
    if (!found_slot) return -3;

    // Create Entry
    uint8_t sector_buf[512] __attribute__((aligned(32)));
    if (sd_read_block(free_sector, sector_buf) != 0) return -4;
    cache_invalidate(sector_buf, 512);

    struct fat32_dir_entry_t *d = (struct fat32_dir_entry_t *)(sector_buf + free_offset);
    memset(d, 0, 32);
    memcpy(d->name, target_name, 11);
    d->attr = 0x20; // Archive
    
    cache_clean(sector_buf, 512);
    sd_write_block(free_sector, sector_buf);

    out->start_cluster = 0;
    out->current_cluster = 0;
    out->size = 0;
    out->position = 0;
    out->dir_sector = free_sector;
    out->dir_offset = free_offset;

    return 0;
}

int fat32_read(struct fat32_fs_t *fs, struct fat32_file_t *file, void *buf, uint32_t size) {
    if (file->position >= file->size) return 0;
    if (file->position + size > file->size) size = file->size - file->position;

    uint8_t *ptr = (uint8_t *)buf;
    uint32_t bytes_read = 0;
    uint8_t scratch[512] __attribute__((aligned(32)));

    while (size > 0) {
        uint32_t cluster_offset = file->position % fs->bytes_per_cluster;
        uint32_t sector_idx = cluster_offset / 512;
        uint32_t byte_idx = cluster_offset % 512;

        uint32_t lba = fat32_cluster_to_lba(fs, file->current_cluster) + sector_idx;
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

int fat32_write(struct fat32_fs_t *fs, struct fat32_file_t *file, const void *buf, uint32_t size) {
    if (file->dir_sector == 0) return -9; // Safety: Invalid file handle

    const uint8_t *ptr = (const uint8_t *)buf;
    uint32_t bytes_written = 0;
    uint8_t scratch[512] __attribute__((aligned(32)));

    while (size > 0) {
        if (file->start_cluster == 0) {
            uint32_t new_c = find_free_cluster(fs);
            if (new_c == 0) return -1;
            
            set_next_cluster(fs, new_c, FAT_EOF);
            
            memset(scratch, 0, 512);
            cache_clean(scratch, 512);
            uint32_t lba = fat32_cluster_to_lba(fs, new_c);
            for(uint32_t i=0; i<fs->sectors_per_cluster; i++) sd_write_block(lba + i, scratch);

            file->start_cluster = new_c;
            file->current_cluster = new_c;
            
            // Update directory entry immediately with new start cluster
            if (sd_read_block(file->dir_sector, scratch) == 0) {
                cache_invalidate(scratch, 512);
                struct fat32_dir_entry_t *d = (struct fat32_dir_entry_t *)(scratch + file->dir_offset);
                d->cluster_hi = (uint16_t)(new_c >> 16);
                d->cluster_lo = (uint16_t)(new_c & 0xFFFF);
                cache_clean(scratch, 512);
                sd_write_block(file->dir_sector, scratch);
            }
        }

        uint32_t cluster_offset = file->position % fs->bytes_per_cluster;
        uint32_t sector_idx = cluster_offset / 512;
        uint32_t byte_idx = cluster_offset % 512;
        uint32_t lba = fat32_cluster_to_lba(fs, file->current_cluster) + sector_idx;

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
            if (next >= FAT_EOF) {
                uint32_t new_c = find_free_cluster(fs);
                if (new_c == 0) return -1;
                set_next_cluster(fs, file->current_cluster, new_c);
                set_next_cluster(fs, new_c, FAT_EOF);
                file->current_cluster = new_c;
                memset(scratch, 0, 512);
                cache_clean(scratch, 512);
                uint32_t lba_next = fat32_cluster_to_lba(fs, new_c);
                for(uint32_t i=0; i<fs->sectors_per_cluster; i++) sd_write_block(lba_next + i, scratch);
            } else {
                file->current_cluster = next;
            }
        }
    }

    if (file->position > file->size) {
        file->size = file->position;
        sd_read_block(file->dir_sector, scratch);
        cache_invalidate(scratch, 512);
        struct fat32_dir_entry_t *d = (struct fat32_dir_entry_t *)(scratch + file->dir_offset);
        d->size = file->size;
        cache_clean(scratch, 512);
        sd_write_block(file->dir_sector, scratch);
    }
    return bytes_written;
}

int fat32_seek(struct fat32_fs_t *fs, struct fat32_file_t *file, uint32_t offset) {
    if (offset > file->size) return -1;
    file->position = offset;
    file->current_cluster = file->start_cluster;
    uint32_t clusters_to_skip = offset / fs->bytes_per_cluster;
    while (clusters_to_skip--) {
        file->current_cluster = get_next_cluster(fs, file->current_cluster);
    }
    return 0;
}

int fat32_close(struct fat32_fs_t *fs, struct fat32_file_t *file) {
    (void)file;
    if (fs->fat_dirty) {
        cache_clean(fs->fat_buffer, 512);
        sd_write_block(fs->cached_fat_sector, fs->fat_buffer);
        fs->fat_dirty = 0;
    }
    return 0;
}