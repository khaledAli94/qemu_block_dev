#include <stdint.h>
#include "sdhc.h"
#include "fat.h"
#include "uart.h"

/*
=== FILES ===
[FS] FAT16 Root LBA: 608, sectors: 32
[DIR]  SYSTEM~1 (0 bytes)
[FILE] LONG_F~1.TXT (63 bytes)
[FILE] TEST.TXT (24 bytes)
[FILE] 1920X1~2.BMP (6220854 bytes)
[FILE] 480X27~1.BMP (388854 bytes)
=================
*/

static void format_83_name(const char *path, unsigned len, char dest[11]) {
    int i = 0, j = 0;
    memset(dest, ' ', 11);

    // Copy name part
    while (i < len && path[i] != '.' && j < 8) {
        char c = path[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        dest[j++] = c;
    }
    // Skip dot
    while (i < len && path[i] != '.') i++;
    if (i < len && path[i] == '.') i++;
    // Copy extension
    j = 8;
    while (i < len && j < 11) {
        char c = path[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        dest[j++] = c;
    }
}

int main(void) {
    struct fat_fs_t fs;
    struct fat_file_t file;
    int res;

    // Aligned buffers are mandatory for DMA/Cache consistency
    uint8_t buf_read[512] __attribute__((aligned(32)));
    uint8_t buf_write[1024] __attribute__((aligned(32)));

    printf("\r\n=== FAT32 BARE-METAL TEST SUITE ===\r\n");

    // 1. Mount Filesystem
    printf("[1/4] Mounting FAT32...\r\n");
    res = fat_mount(&fs);
    if (res != 0) {
        printf("FAIL: Mount error code %d\r\n", res);
        return -1;
    }
    printf("PASS: Mounted. Root Cluster: %u\r\n", fs.root_cluster);

    // 2. Read short file name Test
    char *sfn = "480X27~1.BMP";
    printf("[2/4] Reading SFN %s\r\n", sfn);
    
    res = fat_open(&fs, sfn, &file);
    if (res == 0) {
        printf("PASS: File Open. Size: %u bytes\r\n", file.size);
        
        // Read Sector 0
        memset(buf_read, 0, 512);
        int bytes = fat_read(&fs, &file, buf_read, 512);
        
        // Null-terminate for string printing safety (assuming text file)
        if (bytes < 512) buf_read[bytes] = 0;
        else buf_read[511] = 0;
        printf("Read %d bytes. Content:\r\n" GR BG_YEL "%s" RS "\r\n", bytes, buf_read);

        // Seek Test
        printf("Testing Seek to offset 0x50...\r\n");
        if (fat_seek(&fs, &file, 0x50) == 0) {
            fat_read(&fs, &file, buf_read, 4);
            printf("Bytes at offset 0x50: 0x%X 0x%X 0x%X 0x%X\r\n", buf_read[0], buf_read[1], buf_read[2], buf_read[3]);
        } else {
            printf("FAIL: Seek error.\r\n");
        }
        fat_close(&fs, &file);
    } else {
        printf("WARN: %s not found (Code %d). Skipping Read Test.\r\n", sfn, res);
    }

    #if 0
    // 3. Write Test (WRITE.TXT)
    printf("[3/4] Writing to WRITE.TXT...\r\n");
    
    // Prepare Pattern: 512 bytes of 'A', 512 bytes of 'B'
    memset(buf_write, 'A', 512);
    memset(buf_write + 512, 'B', 512);

    // Try to open existing, or create new
    res = fat_open(&fs, "WRITE.TXT", &file);
    if (res != 0) {
        // File doesn't exist, create it
        printf("File not found, creating new...\r\n");
        res = fat_create(&fs, "WRITE.TXT", &file);
    }
    
    if (res == 0) { 
        int written = fat_write(&fs, &file, buf_write, 1024);
        printf("Written %d bytes.\r\n", written);
        
        fat_close(&fs, &file);
        printf("PASS: File closed and FAT updated.\r\n");
    } else {
        printf("FAIL: Could not open/create WRITE.TXT (Code %d)\r\n", res);
        return -1;
    }
    
    // 4. Verify Data Integrity
    printf("[4/4] Verifying WRITE.TXT...\r\n");
    res = fat_open(&fs, "WRITE.TXT", &file);
    if (res != 0) {
        printf("FAIL: Could not re-open WRITE.TXT\r\n");
        return -1;
    }

    // Read back first 512 bytes
    memset(buf_read, 0, 512);
    fat_read(&fs, &file, buf_read, 512);
    
    if (memcmp(buf_read, buf_write, 512) != 0) {
        printf("FAIL: Data Mismatch in Sector 1 (Expected 'A's...)\r\n");
    } else {
        printf("PASS: Sector 1 Data Verified.\r\n");
    }

    // Read back next 512 bytes
    memset(buf_read, 0, 512);
    fat_read(&fs, &file, buf_read, 512);
    
    if (memcmp(buf_read, buf_write + 512, 512) != 0) {
        printf("FAIL: Data Mismatch in Sector 2 (Expected 'B's...)\r\n");
    } else {
        printf("PASS: Sector 2 Data Verified.\r\n");
    }
    fat_close(&fs, &file);
    #endif
    return 0;
}