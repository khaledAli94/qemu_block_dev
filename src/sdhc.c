#include <sdhc.h>
#include <uart.h>

// --- SD / PL181 Driver ---

uint32_t rca = 0; // Relative Card Address
static void sd_delay(void) {
    volatile int i;
    for (i = 0; i < 100; i++);
}

void sd_init() {
    printf("Initializing SD Card...\n");

    // 1. Power On
    MMCI_POWER = 0x86; // Power up
    MMCI_CLOCK = 0x1FF; // Enable clock, slow speed

    // 2. CMD0: Go Idle State
    sd_send_cmd(0, 0, 0);

    // 3. CMD8: Send Interface Condition (Check voltage)
    // 0x1AA = 2.7-3.6V, Check Pattern 0xAA
    sd_send_cmd(8, 0x1AA, 1);
    if ((MMCI_RESP0 & 0xFF) != 0xAA) {
        printf("Error: CMD8 Failed\n");
        return;
    }

    // 4. ACMD41 Loop (Send Op Cond)
    // We must poll this until the card is ready (Bit 31 set)
    int retries = 1000;
    while (retries--) {
        sd_send_cmd(55, 0, 1); // CMD55 (App Cmd)
        // 0x40... means HCS (High Capacity Support), 0x0030... is voltage window
        sd_send_cmd(41, 0x40300000, 1); 
        
        if (MMCI_RESP0 & (1U << 31)) { // Check "Busy" bit (Bit 31)
            printf("Card Ready!\n");
            break;
        }
    }

    // 5. CMD2: All Send CID (Get Card ID) - Long Response
    sd_send_cmd(2, 0, 2);

    // 6. CMD3: Send Relative Address (Ask card to publish a new RCA)
    sd_send_cmd(3, 0, 1);
    rca = MMCI_RESP0 & 0xFFFF0000; // Save the RCA
    printf("RCA received: 0x%x\n", rca);

    // 7. CMD7: Select Card (Move to Transfer State)
    sd_send_cmd(7, rca, 1);
    
    printf("SD Card Initialized.\n");
}

// Send a command to the SD card
int sd_send_cmd(int cmd, uint32_t arg, int resp_type) {
    MMCI_ARG = arg;
    MMCI_CLEAR = 0xFFFFFFFF;  // Clear all status
    
    uint32_t cmd_reg = cmd | (1 << 10);  // Enable state machine
    
    if (resp_type) {
        cmd_reg |= (1 << 6);  // Expect response
        if (resp_type == 2) {
            cmd_reg |= (1 << 7);  // Long response
        }
    }
    
    MMCI_CMD = cmd_reg;
    
    // Wait for completion (QEMU completes instantly)
    int timeout = 10000;  // Small timeout for QEMU
    uint32_t wait_for = resp_type ? STAT_CMD_RESP_END : STAT_CMD_SENT;
    
    while (timeout--) {
        uint32_t status = MMCI_STATUS;
        
        if (status & STAT_CMD_TIMEOUT) {
            return -1;  // Command timeout
        }
        
        if (status & wait_for) {
            return 0;   // Success
        }
        
        sd_delay();
    }
    
    return -1;  // Timeout
}

void sd_read_sector(uint32_t sector, uint8_t *buffer) {
    printf("Reading Sector %x\n", sector);

    // 1. Setup Data Control
    MMCI_DATALEN = 512;
    MMCI_DATATIMER = 0xFFFF;
    // Enable (1), Direction:Rx (2), BlockSize:512 (9<<4) -> 0x93
    MMCI_DATACTRL = 0x93; 

    // 2. Send CMD17 (Read Single Block)
    sd_send_cmd(17, sector * 512, 1);

    // 3. Read Loop
    int bytes_read = 0;
    while (bytes_read < 512) {
        // Wait for Rx FIFO to have data (RxActive or RxHalfFull)
        if (MMCI_STATUS & (1 << 21)) { 
            uint32_t data = MMCI_FIFO;
            *(uint32_t *)(buffer + bytes_read) = data;
            bytes_read += 4;
        }
    }
    
    // Clear data transfer flags
    MMCI_CLEAR = 0xFFFFFFFF;
    printf("Read Complete.\n");
}

int sd_read_multiple_sectors(uint32_t sector, uint32_t count, uint8_t *buffer) {
    if (!count)
        return -1;
        
    printf("Multi-read: %x sectors from %x\n", count, sector);
    
    // Setup data transfer parameters
    MMCI_DATALEN = 512 * count;        // Total bytes to transfer
    MMCI_DATATIMER = 0xFFFFF;          // Timeout value
    // Enable (1), Direction:Rx (2), BlockSize:512 (9<<4), BlockMode (1<<10)
    MMCI_DATACTRL = 0x493;
    
    // Clear any previous status
    MMCI_CLEAR = 0xFFFFFFFF;
    
    // 1. Send CMD18 (Read Multiple Block)
    if (sd_send_cmd(18, sector * 512, 1) != 0) {
        printf("Error: CMD18 failed\n");
        return -1;
    }
    
    printf("Reading data");
    
    // 2. Read all data
    uint32_t bytes_read = 0;
    uint32_t total_bytes = 512 * count;
    int timeout = 1000000;  // Timeout counter
    
    while (bytes_read < total_bytes) {
        if (timeout-- <= 0) {
            printf("\nError: Read timeout\n");
            
            // Try to send stop command before returning
            sd_send_cmd(12, 0, 1);
            MMCI_CLEAR = 0xFFFFFFFF;
            return -1;
        }
        
        // Check if data is available in FIFO
        if (MMCI_STATUS & STAT_RX_DATA_AVAIL) {
            uint32_t data = MMCI_FIFO;
            
            // Store 4 bytes
            if (bytes_read + 4 <= total_bytes) {
                *(uint32_t *)(buffer + bytes_read) = data;
                bytes_read += 4;
            } else {
                // Handle last partial word
                uint8_t *src = (uint8_t *)&data;
                for (int i = 0; i < 4 && bytes_read < total_bytes; i++) {
                    buffer[bytes_read++] = src[i];
                }
            }
        }
    }
    
    printf("\nRead %x bytes\n", bytes_read);
    
    // 3. Wait for data transfer completion
    timeout = 100000;
    while (timeout--) {
        if (MMCI_STATUS & STAT_DATA_BLOCK_END) {
            break;
        }
    }
    
    // 4. Send CMD12 to stop multiple block transfer
    if (sd_send_cmd(12, 0, 1) != 0) {
        printf("Warning: CMD12 failed\n");
    } else {
        printf("OK\n");
    }
    
    // 5. Clear all status flags
    MMCI_CLEAR = 0xFFFFFFFF;
    
    printf("Multiple read completed\n");
    return 0;
}

void sd_write_sector(uint32_t sector, const uint8_t *buffer) {
    printf("Writing Sector %x\n", sector);

    // 1. Clear any pending status flags before starting
    MMCI_CLEAR = 0xFFFFFFFF;
    
    // 2. Setup Data Control for write
    MMCI_DATALEN = 512;          // 512 bytes to transfer
    MMCI_DATATIMER = 0xFFFFF;    // Generous timeout for QEMU
    
    // Configure Data Control Register
    MMCI_DATACTRL = 0x491; 

    // 3. Send CMD24 (Write Single Block)
    if (sd_send_cmd(24, sector * 512, 1) != 0) {
        printf("Error: CMD24 failed\n");
        return;
    }

    // 4. Write data to FIFO
    int bytes_written = 0;
    int timeout = 100000;  // Large timeout for safety
    
    while (bytes_written < 512 && timeout > 0) {
        uint32_t status = MMCI_STATUS;
        
        // Check for errors first
        if (status & STAT_DATA_TIMEOUT) {
            printf("Error: Data timeout during write\n");
            MMCI_CLEAR = STAT_DATA_TIMEOUT;
            return;
        }
        
        // Method 1: Check if TX FIFO is half empty
        if (status & STAT_TX_FIFO_HALF) {
            for (int i = 0; i < 4 && bytes_written < 512; i++) {
                uint32_t data = 0;
                data |= buffer[bytes_written];
                if (bytes_written + 1 < 512) data |= buffer[bytes_written + 1] << 8;
                if (bytes_written + 2 < 512) data |= buffer[bytes_written + 2] << 16;
                if (bytes_written + 3 < 512) data |= buffer[bytes_written + 3] << 24;
                
                MMCI_FIFO = data;
                bytes_written += 4;
            }
        }
        // Method 2: Check if FIFO empty
        else if (status & STAT_TX_FIFO_EMPTY) {
            uint32_t data = 0;
            data |= buffer[bytes_written];
            if (bytes_written + 1 < 512) data |= buffer[bytes_written + 1] << 8;
            if (bytes_written + 2 < 512) data |= buffer[bytes_written + 2] << 16;
            if (bytes_written + 3 < 512) data |= buffer[bytes_written + 3] << 24;
            
            MMCI_FIFO = data;
            bytes_written += 4;
        }
        
        timeout--;
        if (timeout % 10000 == 0) {
            sd_delay();
        }
    }
    
    if (timeout <= 0) {
        printf("Error: Write loop timeout\n");
        MMCI_CLEAR = 0xFFFFFFFF;
        return;
    }
    
    printf("Data written to FIFO: %x bytes\n", bytes_written);

    // 5. Wait for data transfer to complete
    timeout = 1000000;  // Very large timeout for QEMU
    
    while (timeout--) {
        uint32_t status = MMCI_STATUS;
        
        if (status & STAT_DATA_BLOCK_END) {
            printf("Data block transfer complete\n");
            break;
        }
        
        if (status & STAT_DATA_TIMEOUT) {
            printf("Error: Data timeout after write\n");
            MMCI_CLEAR = STAT_DATA_TIMEOUT;
            return;
        }
        
        if (timeout % 100000 == 0) {
            sd_delay();  // Periodic delay
        }
    }
    
    if (timeout <= 0) {
        printf("Warning: Data block end timeout (may be OK in QEMU)\n");
    }
    
    // 6. Clear all status flags
    MMCI_CLEAR = 0xFFFFFFFF;
    
    // 7. Send CMD13 to verify write was successful
    for (volatile int i = 0; i < 1000; i++);
    
    if (sd_send_cmd(13, rca, 1) == 0) {
        uint32_t card_status = MMCI_RESP0;
        
        printf("Card status after write: %x\n", card_status);
        
        // Simplified check: if high bit is set, card is not busy
        if (card_status & (1U << 31)) {
            printf("Write verified successful\n");
        } else {
            printf("Card still busy after write\n");
        }
    } else {
        printf("Could not verify write status\n");
    }
}

int sd_write_multiple_sectors(uint32_t start_sector, uint32_t count, const uint8_t *buffer) {
    if (count == 0) {
        printf("Error: count must be > 0\n");
        return -1;
    }
    
    printf("Multi-write: %x sectors to %x\n", count, start_sector);
    
    // 1. Clear any pending status flags before starting
    MMCI_CLEAR = 0xFFFFFFFF;
    
    // 2. Setup Data Control for write
    MMCI_DATALEN = 512 * count;           // Total bytes to transfer
    MMCI_DATATIMER = 0xFFFFFF;           // Generous timeout for QEMU
    
    // Configure Data Control Register for multi-block write:
    MMCI_DATACTRL = 0x1493;              // 0x93 | (1 << 10) | (1 << 11)
    
    // 3. Send CMD25 (Write Multiple Block)
    if (sd_send_cmd(25, start_sector * 512, 1) != 0) {
        printf("Error: CMD25 (Write Multiple) failed\n");
        MMCI_DATACTRL = 0;  // Disable data controller
        return -1;
    }
    
    printf("Writing data");
    
    // 4. Write all data blocks
    uint32_t total_bytes = 512 * count;
    uint32_t bytes_written = 0;
    int timeout = 1000000;
    int sectors_written = 0;
    
    while (sectors_written < count && timeout > 0) {
        uint32_t status = MMCI_STATUS;
        
        // Check for errors
        if (status & STAT_DATA_TIMEOUT) {
            printf("\nError: Data timeout during write\n");
            MMCI_CLEAR = STAT_DATA_TIMEOUT;
            break;
        }
        
        // Check if FIFO can accept data
        #ifdef STAT_TX_FIFO_HALF
        if ((status & STAT_TX_FIFO_HALF) || (status & STAT_TX_FIFO_EMPTY)) {
        #else
        if (!(status & STAT_TX_FIFO_FULL)) {
        #endif
            // Write one sector at a time (512 bytes)
            int sector_offset = sectors_written * 512;
            
            // Write complete sector in batches
            for (int word = 0; word < 128; word++) {  // 128 words = 512 bytes
                // Wait if FIFO becomes full
                int inner_timeout = 1000;
                #ifdef STAT_TX_FIFO_HALF
                while (!(MMCI_STATUS & STAT_TX_FIFO_HALF) && inner_timeout--) {
                #else
                while ((MMCI_STATUS & STAT_TX_FIFO_FULL) && inner_timeout--) {
                #endif
                    sd_delay();
                }
                
                // Pack 4 bytes into a word
                uint32_t data = 0;
                uint32_t byte_offset = sector_offset + (word * 4);
                
                // Safely read 4 bytes from buffer
                for (int i = 0; i < 4; i++) {
                    if (byte_offset + i < total_bytes) {
                        data |= buffer[byte_offset + i] << (i * 8);
                    }
                }
                
                MMCI_FIFO = data;
                bytes_written += 4;
            }
            
            sectors_written++;
            
            // Print progress every few sectors
            if ((sectors_written % 16) == 0) {
                printf(".");
            }
        }
        
        timeout--;
        if (timeout % 100000 == 0) {
            sd_delay();
        }
    }
    
    printf("\nData written: %x bytes, %x sectors\n", bytes_written, sectors_written);
    
    if (timeout <= 0) {
        printf("Error: Write loop timeout\n");
    }
    
    // 5. Wait for all data to be transferred to card
    timeout = 1000000;
    printf("Waiting for transfer completion");
    
    while (timeout--) {
        uint32_t status = MMCI_STATUS;
        
        // Check for data block end - use whatever is defined
        #ifdef STAT_DATA_BLOCK_END
        if (status & STAT_DATA_BLOCK_END) {
        #elif defined(STAT_DATA_END)
        if (status & STAT_DATA_END) {
        #else
        if (status & (1 << 8)) {
        #endif
            printf(" - Data block end\n");
            break;
        }
        
        if (status & STAT_DATA_TIMEOUT) {
            printf("\nError: Final data timeout\n");
            MMCI_CLEAR = STAT_DATA_TIMEOUT;
            break;
        }
        
        if (timeout % 100000 == 0) {
            printf(".");
        }
    }
    
    if (timeout <= 0) {
        printf("\nWarning: Data block end timeout\n");
    }
    
    // 6. Send stop transmission token (CMD12)
    printf("Sending stop command...");
    MMCI_CLEAR = 0xFFFFFFFF;  // Clear before new command
    
    // Give card time to process final data
    for (volatile int i = 0; i < 10000; i++);
    
    if (sd_send_cmd(12, 0, 1) != 0) {
        printf(" Warning: CMD12 failed\n");
    } else {
        printf(" OK\n");
    }
    
    // 7. Check if card is still busy
    timeout = 100000;
    while (timeout--) {
        uint32_t status = MMCI_STATUS;
        if (status & (1 << 8)) {  // DATAEND bit
            break;
        }
    }
    
    // 8. Verify write with CMD13
    printf("Verifying write...");
    MMCI_CLEAR = 0xFFFFFFFF;
    
    // Wait a bit more for card to settle
    for (volatile int i = 0; i < 5000; i++);
    
    if (sd_send_cmd(13, rca, 1) == 0) {
        uint32_t card_status = MMCI_RESP0;
        
        // Check if card is ready and in transfer state
        uint32_t current_state = (card_status >> 9) & 0xF;
        printf(" Status: 0x%x (State: %x)\n", card_status, current_state);
        
        if ((card_status & (1 << 8)) && (current_state == 4)) {
            printf("Card ready for data in transfer state\n");
        } else {
            printf("Card state/status unexpected\n");
        }
    } else {
        printf(" Failed to get status\n");
    }
    
    // 9. Cleanup
    MMCI_DATACTRL = 0;  // Disable data controller
    MMCI_CLEAR = 0xFFFFFFFF;  // Clear all status
    
    // 10. Reselect card to ensure we're in transfer state
    sd_send_cmd(7, rca, 1);
    
    printf("Multiple write completed\n");
    return (sectors_written == count) ? 0 : -1;
}