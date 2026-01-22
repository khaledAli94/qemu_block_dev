#include <sdhc.h>
#include <uart.h>

// --- SD / PL181 Driver ---

uint32_t rca = 0; // Relative Card Address
static void sd_delay(void) {
    volatile int i;
    for (i = 0; i < 100; i++);
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