#include <uart.h>
#include <sdhc.h>
#include <stddef.h> 

uint8_t buffer[512];
uint8_t write_buffer[512];

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;

    while (n--)
        *d++ = *s++;

    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *p = dst;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return dst;
}

void main() {
    /* just test xfer */
    // sd_read_sector(0, buffer);
    // sd_read_sector(2048, buffer);


    /* Read Sector 0 (First 512 bytes of sdcard.img MBR) */ 
    sd_read_sector(0, buffer);
    // Partition start LBA
    uint32_t partition_start = *(uint32_t*)(&buffer[0x1be + 8]); //454 assume little endian
    
    // Read the FAT32 Boot Sector
    sd_read_sector(partition_start, buffer);

    /*  
    * Field	                Offset	Size
    * bytes_per_sector	    11 0xb	2
    * sectors_per_cluster   13 0xd	1
    * reserved_sectors	    14 0xe	2
    * number_of_fats        16 0x10	1
    * fat_size_32	        36 0x24	4
    * root_cluster	        44 0x2C	4
    * litte endian reads pointed type then puts at LSB
    */
    uint16_t bytes_per_sector   = *(uint16_t*)(&buffer[0x0B]);
    uint8_t  sectors_per_cluster = buffer[0x0D];
    uint16_t reserved_sectors    = *(uint16_t*)(&buffer[0x0E]);
    uint8_t  number_of_fats      = buffer[0x10];
    uint32_t fat_size            = *(uint32_t*)(&buffer[0x24]);
    uint32_t root_cluster        = *(uint32_t*)(&buffer[0x2C]);

    /* fat region start */
    uint32_t fat_start = partition_start + reserved_sectors;
    /* data region start */
    uint32_t data_start = fat_start + (fat_size * number_of_fats);
    /* Compute LBA of root directory cluster */ 
    uint32_t root_lba = data_start + (root_cluster - 2) * sectors_per_cluster;

    /* Read the first sector of the root directory */
    sd_read_sector(root_lba, buffer);



    /*  parse dir entries in root (single cluster for now) */
    int entries_per_sector = bytes_per_sector / 32;

    for (int i = 0; i < entries_per_sector; i++) {

        uint8_t *entry = &buffer[i * 32];

        if (entry[0] == 0x00)
            break;      // no more entries

        if (entry[0] == 0xe5)
            continue;   // deleted

        uint8_t attr = entry[0x0b];
        if (attr == 0x0F)
            continue;   // long filename entry, skip

        /* Extract filename (11 chars) */
        char name[12];
        for (int j = 0; j < 11; j++)
            name[j] = entry[j];
        name[11] = '\0';

        /* Extract first cluster (high + low) */
        uint32_t cl_hi = *(uint16_t*)(&entry[0x14]);
        uint32_t cl_lo = *(uint16_t*)(&entry[0x1A]);
        uint32_t first_cluster = (cl_hi << 16) | cl_lo;

        /* Extract file size */
        uint32_t file_size = *(uint32_t*)(&entry[0x1C]);

        uart_print("FILE: ");
        uart_print(name);
        uart_print("  CLUSTER: ");
        uart_print_hex(first_cluster);
        uart_print("  SIZE: ");
        uart_print_hex(file_size);
        uart_print("\n");

        if (first_cluster == 0)
            continue;   // skip weird entries

        /* STEP 5: READ FILE CONTENT BY FOLLOWING CLUSTER CHAIN */
        uint32_t current_cluster = first_cluster;
        uint32_t remaining = file_size;

        while (1) {
            /* cluster into  LBA */
            uint32_t file_lba = data_start + (current_cluster - 2) * sectors_per_cluster;

            /* read first sector of this cluster */
            sd_read_sector(file_lba, buffer);

            /* dump up to 512 bytes (or remaining) */
            uint32_t to_print = remaining > 512 ? 512 : remaining;

            uart_print("DATA: ");
            for (uint32_t k = 0; k < to_print; k++) {
                uart_putc(buffer[k]);
            }
            uart_print("\n");

            if (remaining <= 512)
                break;

            remaining -= 512;

            /* ---- FOLLOW FAT CHAIN ---- */

            /* FAT32 entry = 4 bytes per cluster */
            uint32_t fat_offset = current_cluster * 4;
            uint32_t fat_sector = fat_start + (fat_offset >> 9); //   = fat_start + (fat_offset / bytes_per_sector);
            uint32_t fat_index  = fat_offset & 0x1ff ; //    = fat_offset % bytes_per_sector;

            sd_read_sector(fat_sector, buffer);

            uint32_t next_cluster = *(uint32_t*)(&buffer[fat_index]);

            /* end-of-chain check (FAT32 EOC >= 0x0FFFFFF8) */
            if (next_cluster >= 0x0FFFFFF8)
                break;

            current_cluster = next_cluster;
        }    


   
    }

    while (1);
}
