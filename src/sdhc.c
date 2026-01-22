#include <sdhc.h>
#include <uart.h>

// --- SD / PL181 Driver ---

uint32_t rca = 0; // Relative Card Address
static void sd_delay(void) {
    volatile int i;
    for (i = 0; i < 100; i++);
}

void sd_init() {
    uart_print("Initializing SD Card...\n");

    // 1. Power On
    MMCI_POWER = 0x86; // Power up
    MMCI_CLOCK = 0x1FF; // Enable clock, slow speed

    // 2. CMD0: Go Idle State
    sd_send_cmd(0, 0, 0);

    // 3. CMD8: Send Interface Condition (Check voltage)
    // 0x1AA = 2.7-3.6V, Check Pattern 0xAA
    sd_send_cmd(8, 0x1AA, 1);
    if ((MMCI_RESP0 & 0xFF) != 0xAA) {
        uart_print("Error: CMD8 Failed\n");
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
            uart_print("Card Ready!\n");
            break;
        }
    }

    // 5. CMD2: All Send CID (Get Card ID) - Long Response
    sd_send_cmd(2, 0, 2);

    // 6. CMD3: Send Relative Address (Ask card to publish a new RCA)
    sd_send_cmd(3, 0, 1);
    rca = MMCI_RESP0 & 0xFFFF0000; // Save the RCA
    uart_print("RCA received: "); uart_print_hex(rca); uart_print("\n");

    // 7. CMD7: Select Card (Move to Transfer State)
    sd_send_cmd(7, rca, 1);
    
    uart_print("SD Card Initialized.\n");
}

// Send a command to the SD card
// cmd:  Command Index
// arg:  Argument
// resp: 0 = No Resp, 1 = Short Resp (48 bits), 2 = Long Resp (136 bits)
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
    uart_print("Reading Sector "); uart_print_hex(sector); uart_print("\n");

    // 1. Setup Data Control
    MMCI_DATALEN = 512;
    MMCI_DATATIMER = 0xFFFF;
    // Enable (1), Direction:Rx (2), BlockSize:512 (9<<4) -> 0x93
    MMCI_DATACTRL = 0x93; 

    // 2. Send CMD17 (Read Single Block)
    // Note: QEMU usually defaults to Byte Addressing for small images unless HCS is fully negotiated.
    // We multiply by 512 to be safe for standard .img files. 
    sd_send_cmd(17, sector * 512, 1);

    // 3. Read Loop
    int bytes_read = 0;
    while (bytes_read < 512) {
        // Wait for Rx FIFO to have data (RxActive or RxHalfFull)
        // Check MMCI_STATUS for DataEnd (bit 8) or RxDataAvail (bit 21)
        if (MMCI_STATUS & (1 << 21)) { 
            uint32_t data = MMCI_FIFO;
            // Write 4 bytes into buffer
            *(uint32_t *)(buffer + bytes_read) = data;
            bytes_read += 4;
        }
    }
    
    // Clear data transfer flags
    MMCI_CLEAR = 0xFFFFFFFF;
    uart_print("Read Complete.\n");
}

int sd_read_multiple_sectors(uint32_t sector, uint32_t count, uint8_t *buffer) {
    if (!count)
        return -1;
        
    uart_print("Multi-read: ");uart_print_hex(count);uart_print(" sectors from ");uart_print_hex(sector);uart_print("\n");
    
    // Setup data transfer parameters
    MMCI_DATALEN = 512 * count;        // Total bytes to transfer
    MMCI_DATATIMER = 0xFFFFF;          // Timeout value
    // Enable (1), Direction:Rx (2), BlockSize:512 (9<<4), BlockMode (1<<10)
    MMCI_DATACTRL = 0x493;
    
    // Clear any previous status
    MMCI_CLEAR = 0xFFFFFFFF;
    
    // 1. Send CMD18 (Read Multiple Block)
    if (sd_send_cmd(18, sector * 512, 1) != 0) {
        uart_print("Error: CMD18 failed\n");
        return -1;
    }
    
    uart_print("Reading data");
    
    // 2. Read all data
    uint32_t bytes_read = 0;
    uint32_t total_bytes = 512 * count;
    int timeout = 1000000;  // Timeout counter
    
    while (bytes_read < total_bytes) {
        if (timeout-- <= 0) {
            uart_print("\nError: Read timeout\n");
            
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
    
    uart_print("\nRead "); uart_print_hex(bytes_read); uart_print(" bytes\n");
    
    // 3. Wait for data transfer completion
    timeout = 100000;
    while (timeout--) {
        if (MMCI_STATUS & STAT_DATA_BLOCK_END) {
            break;
        }
    }
    
    // 4. Send CMD12 to stop multiple block transfer
    if (sd_send_cmd(12, 0, 1) != 0) {
        uart_print("Warning: CMD12 failed\n");
    } else {
        uart_print("OK\n");
    }
    
    // 5. Clear all status flags
    MMCI_CLEAR = 0xFFFFFFFF;
    
    uart_print("Multiple read completed\n");
    return 0;
}

void sd_write_sector(uint32_t sector, const uint8_t *buffer) {
    uart_print("Writing Sector "); uart_print_hex(sector); uart_print("\n");

    // 1. Clear any pending status flags before starting
    MMCI_CLEAR = 0xFFFFFFFF;
    
    // 2. Setup Data Control for write
    MMCI_DATALEN = 512;          // 512 bytes to transfer
    MMCI_DATATIMER = 0xFFFFF;    // Generous timeout for QEMU
    
    // Configure Data Control Register:
    // - Bit 0: Enable (1)
    // - Bits 1-2: Direction = 0 (Host to Card/Transmit)
    // - Bits 4-7: Blocksize = 9 (512 bytes)
    // - Bit 10: Use block mode (1)
    // Result: 0x91 | (1 << 10) = 0x491
    MMCI_DATACTRL = 0x491; 

    // 3. Send CMD24 (Write Single Block)
    // QEMU uses byte addressing for .img files
    if (sd_send_cmd(24, sector * 512, 1) != 0) {
        uart_print("Error: CMD24 failed\n");
        return;
    }

    // 4. Write data to FIFO - IMPORTANT: QEMU PL181 requires precise FIFO handling
    int bytes_written = 0;
    int timeout = 100000;  // Large timeout for safety
    
    while (bytes_written < 512 && timeout > 0) {
        uint32_t status = MMCI_STATUS;
        
        // Check for errors first
        if (status & STAT_DATA_TIMEOUT) {
            uart_print("Error: Data timeout during write\n");
            MMCI_CLEAR = STAT_DATA_TIMEOUT;
            return;
        }
        
        // PL181 FIFO is 16 words (64 bytes). We can write when FIFO is not full.
        // For QEMU, we need to check multiple conditions:
        // - TX FIFO not full (FIFO empty or half-empty flags)
        // - Data controller is ready to accept data
        
        // Method 1: Check if TX FIFO is half empty (can accept at least 8 words)
        if (status & STAT_TX_FIFO_HALF) {
            // Write 4 words (16 bytes) at a time to keep FIFO busy
            for (int i = 0; i < 4 && bytes_written < 512; i++) {
                uint32_t data = 0;
                
                // Pack 4 bytes into a word (little-endian)
                data |= buffer[bytes_written];
                if (bytes_written + 1 < 512) data |= buffer[bytes_written + 1] << 8;
                if (bytes_written + 2 < 512) data |= buffer[bytes_written + 2] << 16;
                if (bytes_written + 3 < 512) data |= buffer[bytes_written + 3] << 24;
                
                MMCI_FIFO = data;
                bytes_written += 4;
            }
        }
        
        // Method 2: Alternative check - if FIFO empty flag is set, we can definitely write
        else if (status & STAT_TX_FIFO_EMPTY) {
            // Write 1 word (4 bytes) when FIFO is completely empty
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
            sd_delay();  // Small delay to prevent tight loop issues in QEMU
        }
    }
    
    if (timeout <= 0) {
        uart_print("Error: Write loop timeout\n");
        MMCI_CLEAR = 0xFFFFFFFF;
        return;
    }
    
    uart_print("Data written to FIFO: "); uart_print_hex(bytes_written); uart_print(" bytes\n");

    // 5. Wait for data transfer to complete (Data Block End)
    // This is CRITICAL - the write isn't done until the card acknowledges it
    timeout = 1000000;  // Very large timeout for QEMU
    
    while (timeout--) {
        uint32_t status = MMCI_STATUS;
        
        // Check for completion
        if (status & STAT_DATA_BLOCK_END) {
            uart_print("Data block transfer complete\n");
            break;
        }
        
        // Check for errors
        if (status & STAT_DATA_TIMEOUT) {
            uart_print("Error: Data timeout after write\n");
            MMCI_CLEAR = STAT_DATA_TIMEOUT;
            return;
        }
        
        if (timeout % 100000 == 0) {
            sd_delay();  // Periodic delay
        }
    }
    
    if (timeout <= 0) {
        uart_print("Warning: Data block end timeout (may be OK in QEMU)\n");
    }
    
    // 6. Clear all status flags
    MMCI_CLEAR = 0xFFFFFFFF;
    
    // 7. Send CMD13 to verify write was successful
    // Wait a bit for card to process (QEMU needs this)
    for (volatile int i = 0; i < 1000; i++);
    
    if (sd_send_cmd(13, rca, 1) == 0) {
        uint32_t card_status = MMCI_RESP0;
        
        // Check important status bits:
        // Bit 8: READY_FOR_DATA (1 = ready)
        // Bit 9: APP_CMD (1 = card expects ACMD)
        // Bits 22-12: Current state (should be 4 = transfer state)
        
        uart_print("Card status after write: "); uart_print_hex(card_status); uart_print("\n");
        
        // Simplified check: if high bit is set, card is not busy
        if (card_status & (1U << 31)) {
            uart_print("Write verified successful\n");
        } else {
            uart_print("Card still busy after write\n");
        }
    } else {
        uart_print("Could not verify write status\n");
    }
}

int sd_write_multiple_sectors(uint32_t start_sector, uint32_t count, const uint8_t *buffer) {
    if (count == 0) {
        uart_print("Error: count must be > 0\n");
        return -1;
    }
    
    uart_print("Multi-write: ");
    uart_print_hex(count);
    uart_print(" sectors to ");
    uart_print_hex(start_sector);
    uart_print("\n");
    
    // 1. Clear any pending status flags before starting
    MMCI_CLEAR = 0xFFFFFFFF;
    
    // 2. Setup Data Control for write
    MMCI_DATALEN = 512 * count;           // Total bytes to transfer
    MMCI_DATATIMER = 0xFFFFFF;           // Generous timeout for QEMU
    
    // Configure Data Control Register for multi-block write:
    // - Bit 0: Enable (1)
    // - Bits 1-2: Direction = 0 (Host to Card/Transmit)
    // - Bits 4-7: Blocksize = 9 (512 bytes)
    // - Bit 10: Use block mode (1)
    // - Bit 11: Multi-block mode (1) - IMPORTANT for CMD25
    MMCI_DATACTRL = 0x1493;              // 0x93 | (1 << 10) | (1 << 11)
    
    // 3. Send CMD25 (Write Multiple Block)
    // Byte addressing for .img files (sector * 512)
    if (sd_send_cmd(25, start_sector * 512, 1) != 0) {
        uart_print("Error: CMD25 (Write Multiple) failed\n");
        MMCI_DATACTRL = 0;  // Disable data controller
        return -1;
    }
    
    uart_print("Writing data");
    
    // 4. Write all data blocks
    uint32_t total_bytes = 512 * count;
    uint32_t bytes_written = 0;
    int timeout = 1000000;
    int sectors_written = 0;
    
    while (sectors_written < count && timeout > 0) {
        uint32_t status = MMCI_STATUS;
        
        // Check for errors
        if (status & STAT_DATA_TIMEOUT) {
            uart_print("\nError: Data timeout during write\n");
            MMCI_CLEAR = STAT_DATA_TIMEOUT;
            break;
        }
        
        // Check if FIFO can accept data
        // Use TX_FIFO_HALF or TX_FIFO_EMPTY - whatever is defined in your headers
        #ifdef STAT_TX_FIFO_HALF
        if ((status & STAT_TX_FIFO_HALF) || (status & STAT_TX_FIFO_EMPTY)) {
        #else
        // Alternative: check if FIFO is not full
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
                uart_print(".");
            }
        }
        
        timeout--;
        if (timeout % 100000 == 0) {
            sd_delay();
        }
    }
    
    uart_print("\nData written: ");
    uart_print_hex(bytes_written);
    uart_print(" bytes, ");
    uart_print_hex(sectors_written);
    uart_print(" sectors\n");
    
    if (timeout <= 0) {
        uart_print("Error: Write loop timeout\n");
    }
    
    // 5. Wait for all data to be transferred to card
    timeout = 1000000;
    uart_print("Waiting for transfer completion");
    
    while (timeout--) {
        uint32_t status = MMCI_STATUS;
        
        // Check for data block end - use whatever is defined
        #ifdef STAT_DATA_BLOCK_END
        if (status & STAT_DATA_BLOCK_END) {
        #elif defined(STAT_DATA_END)
        if (status & STAT_DATA_END) {
        #else
        // Check for data transfer complete flag
        if (status & (1 << 8)) {  // Often bit 8 is DATAEND
        #endif
            uart_print(" - Data block end\n");
            break;
        }
        
        if (status & STAT_DATA_TIMEOUT) {
            uart_print("\nError: Final data timeout\n");
            MMCI_CLEAR = STAT_DATA_TIMEOUT;
            break;
        }
        
        if (timeout % 100000 == 0) {
            uart_print(".");
        }
    }
    
    if (timeout <= 0) {
        uart_print("\nWarning: Data block end timeout\n");
    }
    
    // 6. Send stop transmission token (CMD12)
    // IMPORTANT: Must send stop command for multi-block write
    uart_print("Sending stop command...");
    MMCI_CLEAR = 0xFFFFFFFF;  // Clear before new command
    
    // Give card time to process final data
    for (volatile int i = 0; i < 10000; i++);
    
    if (sd_send_cmd(12, 0, 1) != 0) {
        uart_print(" Warning: CMD12 failed\n");
    } else {
        uart_print(" OK\n");
    }
    
    // 7. Check if card is still busy by looking at DATAEND or other busy flag
    timeout = 100000;
    while (timeout--) {
        uint32_t status = MMCI_STATUS;
        // Check if data transfer is complete - often bit 8 is DATAEND
        // If DATAEND is clear, card might still be busy
        if (status & (1 << 8)) {  // DATAEND bit
            break;
        }
    }
    
    // 8. Verify write with CMD13
    uart_print("Verifying write...");
    MMCI_CLEAR = 0xFFFFFFFF;
    
    // Wait a bit more for card to settle
    for (volatile int i = 0; i < 5000; i++);
    
    if (sd_send_cmd(13, rca, 1) == 0) {
        uint32_t card_status = MMCI_RESP0;
        
        // Check if card is ready and in transfer state
        uint32_t current_state = (card_status >> 9) & 0xF;
        uart_print(" Status: 0x");
        uart_print_hex(card_status);
        uart_print(" (State: ");
        uart_print_hex(current_state);
        uart_print(")\n");
        
        // Check READY_FOR_DATA bit (bit 8) and current state
        // State 4 = transfer state, Bit 31 = not busy
        if ((card_status & (1 << 8)) && (current_state == 4)) {
            uart_print("Card ready for data in transfer state\n");
        } else {
            uart_print("Card state/status unexpected\n");
        }
    } else {
        uart_print(" Failed to get status\n");
    }
    
    // 9. Cleanup
    MMCI_DATACTRL = 0;  // Disable data controller
    MMCI_CLEAR = 0xFFFFFFFF;  // Clear all status
    
    // 10. Reselect card to ensure we're in transfer state
    sd_send_cmd(7, rca, 1);
    
    uart_print("Multiple write completed\n");
    return (sectors_written == count) ? 0 : -1;
}