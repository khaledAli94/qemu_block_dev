#include <stdint.h>
#include "sdhc.h"
#include "fat32.h"
#include "uart.h"

// --- Main Test Suite ---

int main(void) {
    struct fat32_fs_t fs;
    struct fat32_file_t file;
    int res;

    // Aligned buffers are mandatory for DMA/Cache consistency
    uint8_t buf_read[512] __attribute__((aligned(32)));
    uint8_t buf_write[1024] __attribute__((aligned(32)));

    printf("\r\n=== FAT32 BARE-METAL TEST SUITE ===\r\n");

    // 1. Mount Filesystem
    printf("[1/4] Mounting FAT32...\r\n");
    res = fat32_mount(&fs);
    if (res != 0) {
        printf("FAIL: Mount error code %d\r\n", res);
        return -1;
    }
    printf("PASS: Mounted. Root Cluster: %u\r\n", fs.root_cluster);

    // 2. Read Test (HELLO_~1.TXT)
    // Note: We use the 8.3 Short Name alias for "hello_world.txt"
    printf("[2/4] Reading HELLO_~1.TXT...\r\n");
    
    res = fat32_open(&fs, "HELLO_~1.TXT", &file);
    if (res == 0) {
        printf("PASS: File Open. Size: %u bytes\r\n", file.size);
        
        // Read Sector 0
        memset(buf_read, 0, 512);
        int bytes = fat32_read(&fs, &file, buf_read, 512);
        
        // Null-terminate for string printing safety (assuming text file)
        if (bytes < 512) buf_read[bytes] = 0;
        else buf_read[511] = 0;
        printf("Read %d bytes. Content:\r\n" GR BG_YEL "%s" RS "\r\n", bytes, buf_read);

        // Seek Test
        printf("Testing Seek to offset 50...\r\n");
        if (fat32_seek(&fs, &file, 50) == 0) {
            fat32_read(&fs, &file, buf_read, 4);
            printf("Bytes at offset 50: %02X %02X %02X %02X\r\n", 
                   buf_read[0], buf_read[1], buf_read[2], buf_read[3]);
        } else {
            printf("FAIL: Seek error.\r\n");
        }
        fat32_close(&fs, &file);
    } else {
        printf("WARN: HELLO_~1.TXT not found (Code %d). Skipping Read Test.\r\n", res);
    }

    // 3. Write Test (WRITE.TXT)
    printf("[3/4] Writing to WRITE.TXT...\r\n");
    
    // Prepare Pattern: 512 bytes of 'A', 512 bytes of 'B'
    memset(buf_write, 'A', 512);
    memset(buf_write + 512, 'B', 512);

    // Try to open existing, or create new
    res = fat32_open(&fs, "WRITE.TXT", &file);
    if (res != 0) {
        // File doesn't exist, create it
        printf("File not found, creating new...\r\n");
        res = fat32_create(&fs, "WRITE.TXT", &file);
    }

    if (res == 0) { 
        int written = fat32_write(&fs, &file, buf_write, 1024);
        printf("Written %d bytes.\r\n", written);
        
        fat32_close(&fs, &file);
        printf("PASS: File closed and FAT updated.\r\n");
    } else {
        printf("FAIL: Could not open/create WRITE.TXT (Code %d)\r\n", res);
        return -1;
    }

    // 4. Verify Data Integrity
    printf("[4/4] Verifying WRITE.TXT...\r\n");
    res = fat32_open(&fs, "WRITE.TXT", &file);
    if (res != 0) {
        printf("FAIL: Could not re-open WRITE.TXT\r\n");
        return -1;
    }

    // Read back first 512 bytes
    memset(buf_read, 0, 512);
    fat32_read(&fs, &file, buf_read, 512);
    
    if (memcmp(buf_read, buf_write, 512) != 0) {
        printf("FAIL: Data Mismatch in Sector 1 (Expected 'A's...)\r\n");
    } else {
        printf("PASS: Sector 1 Data Verified.\r\n");
    }

    // Read back next 512 bytes
    memset(buf_read, 0, 512);
    fat32_read(&fs, &file, buf_read, 512);

    if (memcmp(buf_read, buf_write + 512, 512) != 0) {
        printf("FAIL: Data Mismatch in Sector 2 (Expected 'B's...)\r\n");
    } else {
        printf("PASS: Sector 2 Data Verified.\r\n");
    }

    fat32_close(&fs, &file);
    return 0;
}