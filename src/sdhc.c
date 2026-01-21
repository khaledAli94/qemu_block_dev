#include <sdhc.h>
#include <uart.h>

// --- SD / PL181 Driver ---

uint32_t rca = 0; // Relative Card Address

// Send a command to the SD card
// cmd:  Command Index
// arg:  Argument
// resp: 0 = No Resp, 1 = Short Resp (48 bits), 2 = Long Resp (136 bits)
int sd_send_cmd(int cmd, uint32_t arg, int resp) {
    MMCI_ARG = arg;
    MMCI_CLEAR = 0xFFFFFFFF; // Clear previous status flags

    // 1. Construct the Command Register Value
    // Bit 10: Enable State Machine
    uint32_t cmd_val = cmd | (1 << 10); 
    
    if (resp) {
        cmd_val |= (1 << 6); // Bit 6: Expect Response
        if (resp == 2) {
            cmd_val |= (1 << 7); // Bit 7: Long Response (136-bit)
        }
    }

    MMCI_CMD = cmd_val;

    // 2. Determine which Status Bit to wait for
    // If we expect a response, wait for Bit 6 (CmdRespEnd)
    // If NO response, wait for Bit 7 (CmdSent)
    int wait_mask = (resp) ? (1 << 6) : (1 << 7);

    // 3. Wait for completion
    while (1) {
        volatile uint32_t status = MMCI_STATUS;
        
        // Check for Timeout (Bit 2)
        if (status & (1 << 2)) { 
            uart_print(" [CMD Timeout]\n");
            return -1;
        }
        
        // Check for Completion (Bit 6 or Bit 7)
        if (status & wait_mask) {
            break; 
        }
    }
    return 0;
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
