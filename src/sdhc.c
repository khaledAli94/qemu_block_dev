#include "sdhc.h"

// implmentation 1-bit mode at Low Speed (400kHz)
static uint32_t rca = 0;             // Relative Card Address
static int is_high_capacity = 0;     // 0 = SDSC (Byte Addr), 1 = SDHC/SDXC (Block Addr)

// --- Internal Helpers ---
static void delay_cycles(volatile int cycles) {
    while(cycles--) __asm__("nop");
}

static int sd_update_clock(void) {
    H3_SD_MMC0->CMDR = CMD_START | CMD_UP_CLK | CMD_WAIT_PRE;
    int timeout = 100000;
    while ((H3_SD_MMC0->CMDR & CMD_START) && timeout--);
    return (timeout > 0) ? 0 : -1;
}

static int sd_send_cmd(uint32_t cmd, uint32_t arg, uint32_t flags) {
    H3_SD_MMC0->RISR = 0xFFFFFFFF; // Clear interrupts
    H3_SD_MMC0->CAGR = arg;
    H3_SD_MMC0->CMDR = (cmd & 0x3F) | flags | CMD_START;

    int timeout = 1000000;
    while (timeout--) {
        uint32_t risr = H3_SD_MMC0->RISR;
        if (risr & RISR_ERRORS) {
            return -1; 
        }
        if (risr & RISR_CMD_DONE) {
            H3_SD_MMC0->RISR = RISR_CMD_DONE;
            return 0; 
        }
    }
    return -2; // Timeout
}

int sd_init(void) {
    // 1. Reset & Setup
    H3_SD_MMC0->GCTL = GCTL_SOFT_RST | GCTL_FIFO_RST | GCTL_DMA_RST;
    delay_cycles(1000);
    H3_SD_MMC0->GCTL = GCTL_HC_EN; // Enable Controller, DMA is OFF by default
    
    H3_SD_MMC0->CKCR = (1U << 16) | (1U << 24); 
    sd_update_clock();
    
    // 2. Init Commands
    if (sd_send_cmd(CMD0, 0, CMD_USE_HOLD) != 0) return -1;
    
    // CMD8: voltage check
    if (sd_send_cmd(CMD8, 0x1AA, CMD_RESP_EXP | CMD_CHECK_CRC) != 0) return -2;
    if ((H3_SD_MMC0->RESP0 & 0xFF) != 0xAA) return -3;

    // 3. ACMD41 with Capacity Check
    int retries = 1000;
    while (retries--) {
        sd_send_cmd(CMD55, 0, CMD_RESP_EXP | CMD_CHECK_CRC);
        
        // Arg 0x40... sets HCS (High Capacity Support) bit to 1
        sd_send_cmd(ACMD41, 0x40FF8000, CMD_RESP_EXP);
        
        if (H3_SD_MMC0->RESP0 & (1U << 31)) { // Card Ready (Busy bit)
            // Check CCS bit (Card Capacity Status, Bit 30)
            // If 1, it's SDHC (Block Addressing). If 0, it's SDSC (Byte Addressing).
            if (H3_SD_MMC0->RESP0 & (1U << 30)) {
                is_high_capacity = 1; 
            } else {
                is_high_capacity = 0;
            }
            break; 
        }
        delay_cycles(1000);
    }
    if (retries <= 0) return -4;

    // 4. Finalize Setup
    if (sd_send_cmd(CMD2, 0, CMD_RESP_EXP | CMD_LONG_RESP | CMD_CHECK_CRC) != 0) return -5;
    if (sd_send_cmd(CMD3, 0, CMD_RESP_EXP | CMD_CHECK_CRC) != 0) return -6;
    rca = H3_SD_MMC0->RESP0 >> 16;
    
    if (sd_send_cmd(CMD7, rca << 16, CMD_RESP_EXP | CMD_CHECK_CRC) != 0) return -7;
    if (sd_send_cmd(CMD16, 512, CMD_RESP_EXP | CMD_CHECK_CRC) != 0) return -8; // Set Block Size
    
    return 0;
}

int sd_read_block(uint32_t sector, uint8_t *buffer) {
    H3_SD_MMC0->BKSR = 512;
    H3_SD_MMC0->BYCR = 512;

    // --- FIX: ADDRESSING MODE ---
    uint32_t addr;
    if (is_high_capacity) {
        addr = sector;        // SDHC/SDXC
    } else {
        addr = sector * 512;  // SDSC (Standard QEMU Image)
    }

    // Send Read Command
    // CMD_WAIT_PRE is important for H3 to ensure previous data is flushed
    uint32_t flags = CMD_RESP_EXP | CMD_CHECK_CRC | CMD_DATA_EXP | CMD_WAIT_PRE;
    if (sd_send_cmd(CMD17, addr, flags) != 0) return -1;

    // --- FIX: ROBUST FIFO READ ---
    uint32_t *buf_u32 = (uint32_t *)buffer;
    int words_read = 0;
    int timeout = 0xFFFFF;

    // Loop until we have read all 128 words (512 bytes)
    while (words_read < 128 && timeout--) {
        // Check for Errors
        if (H3_SD_MMC0->RISR & RISR_ERRORS) {
            return -2; // Hardware Error
        }

        // Check if FIFO is empty by reading Status Register (STAR)
        // Bit 2: FIFO Empty
        if (!(H3_SD_MMC0->STAR & (1U << 2))) { 
            // FIFO is NOT empty, safe to read
            buf_u32[words_read++] = H3_SD_MMC0->FIFO;
        }
    }

    // Acknowledge Data Transfer Over
    H3_SD_MMC0->RISR = RISR_DATA_OVER;

    return (timeout > 0) ? 0 : -3;
}

int sd_read_blocks(uint32_t sector, int count, uint8_t *buffer) {
    if (count <= 0) return -1;
    if (count == 1) return sd_read_block(sector, buffer); // Fallback optimization

    // 1. Configure Data Transfer Size
    // BKSR is always 512 for SD cards
    H3_SD_MMC0->BKSR = 512;
    // BYCR is the TOTAL number of bytes to transfer
    H3_SD_MMC0->BYCR = 512 * count;

    // 2. Handle Addressing (Byte vs Block)
    uint32_t addr;
    if (is_high_capacity) {
        addr = sector;        // SDHC/SDXC: Block Address
    } else {
        addr = sector * 512;  // SDSC: Byte Address
    }

    // 3. Send CMD18 (Read Multiple)
    // We add CMD_AUTO_STOP so the Hardware Controller sends CMD12 automatically
    // when BYCR countdown reaches 0.
    uint32_t flags = CMD_RESP_EXP | CMD_CHECK_CRC | CMD_DATA_EXP | CMD_WAIT_PRE | CMD_AUTO_STOP;
    
    if (sd_send_cmd(CMD18, addr, flags) != 0) return -2;

    // 4. Read Loop
    uint32_t *buf_u32 = (uint32_t *)buffer;
    int total_words = (512 * count) / 4;
    int words_read = 0;
    int timeout = 0xFFFFFF; // Larger timeout for multiple blocks

    while (words_read < total_words && timeout--) {
        // Check for Errors
        if (H3_SD_MMC0->RISR & RISR_ERRORS) {
            // Optional: Send manual CMD12 here if error occurs to reset card
            return -3;
        }

        // Check FIFO Status
        // Bit 2 of STAR register = FIFO Empty. We read if NOT empty.
        if (!(H3_SD_MMC0->STAR & (1U << 2))) { 
            buf_u32[words_read++] = H3_SD_MMC0->FIFO;
        }
    }

    if (timeout <= 0) return -4; // Timeout

    // 5. Wait for Data Over & Auto-Command Done
    // Since we used Auto-Stop, we should technically wait for the specific Auto-Command done flag
    // but waiting for DATA_OVER is usually sufficient for the data phase.
    timeout = 0xFFFF;
    while (!(H3_SD_MMC0->RISR & RISR_DATA_OVER) && timeout--) {
         // Wait for controller to finish
    }
    
    // Clear Interrupt Flags
    H3_SD_MMC0->RISR = RISR_DATA_OVER | RISR_CMD_DONE;

    return 0;
}

int sd_write_block(uint32_t sector, const uint8_t *buffer) {
    // 1. Setup Block Size & Byte Count
    H3_SD_MMC0->BKSR = 512;
    H3_SD_MMC0->BYCR = 512;

    // 2. Addressing (Inline logic since helper is removed)
    uint32_t arg = sector;
    if (!is_high_capacity) {
        arg *= 512; // SDSC requires byte addressing
    }

    // 3. Send CMD24
    // Flags: Response | Check CRC | Data Expected | Wait Pre-load | WRITE MODE
    uint32_t flags = CMD_RESP_EXP | CMD_CHECK_CRC | CMD_DATA_EXP | CMD_WAIT_PRE | CMD_WRITE;
    
    if (sd_send_cmd(CMD24, arg, flags) != 0) return -1;

    // 4. Write Loop
    const uint32_t *buf_u32 = (const uint32_t *)buffer;
    int words_to_write = 128; // 512 bytes / 4
    int timeout = 0xFFFFF;

    while (words_to_write > 0 && timeout--) {
        // Check for Errors
        if (H3_SD_MMC0->RISR & RISR_ERRORS) {
            return -2;
        }

        // Check FIFO Status (Bit 3 in STAR usually indicates FIFO Full)
        // If Bit 3 is 0, FIFO has space.
        if (!(H3_SD_MMC0->STAR & (1U << 3))) { 
            H3_SD_MMC0->FIFO = *buf_u32++;
            words_to_write--;
        }
    }

    if (timeout <= 0) return -3; // Write Timeout

    // 5. Wait for Data Transfer Complete
    timeout = 0xFFFFF;
    while (!(H3_SD_MMC0->RISR & RISR_DATA_OVER) && timeout--) {
        if (H3_SD_MMC0->RISR & RISR_ERRORS) return -4;
    }

    // Clear Flags
    H3_SD_MMC0->RISR = RISR_DATA_OVER | RISR_CMD_DONE;

    return (timeout > 0) ? 0 : -5;
}

int sd_write_blocks(uint32_t sector, int count, const uint8_t *buffer) {
    if (count <= 0) return -1;
    if (count == 1) return sd_write_block(sector, buffer); // Optimization

    // 1. Setup Block Size & Total Byte Count
    H3_SD_MMC0->BKSR = 512;
    H3_SD_MMC0->BYCR = 512 * count; // Total bytes to write

    // 2. Addressing (Inline logic)
    uint32_t arg = sector;
    if (!is_high_capacity) {
        arg *= 512; // SDSC requires byte addressing
    }

    // 3. Send CMD25 (Write Multiple)
    // Flags: Response | Check CRC | Data Exp | Wait Pre | WRITE | AUTO STOP
    // CMD_AUTO_STOP (Bit 12) is crucial here! It tells the controller to 
    // send CMD12 automatically when BYCR reaches 0.
    uint32_t flags = CMD_RESP_EXP | CMD_CHECK_CRC | CMD_DATA_EXP | 
                     CMD_WAIT_PRE | CMD_WRITE | CMD_AUTO_STOP;
    
    if (sd_send_cmd(CMD25, arg, flags) != 0) return -2;

    // 4. Write Loop
    const uint32_t *buf_u32 = (const uint32_t *)buffer;
    int total_words = (512 * count) / 4;
    int words_written = 0;
    int timeout = 0xFFFFFF; // Larger timeout for multiple blocks

    while (words_written < total_words && timeout--) {
        // Check for Errors
        if (H3_SD_MMC0->RISR & RISR_ERRORS) {
            return -3;
        }

        // Check FIFO Status
        // Bit 3 in STAR register = FIFO Full. 
        // We write only if the FIFO is NOT full.
        if (!(H3_SD_MMC0->STAR & (1U << 3))) { 
            H3_SD_MMC0->FIFO = buf_u32[words_written++];
        }
    }

    if (timeout <= 0) return -4; // Write Data Timeout

    // 5. Wait for Auto-Stop and Data Complete
    // We wait for DATA_OVER, which implies the Auto-CMD12 is also finished.
    timeout = 0xFFFFF;
    while (!(H3_SD_MMC0->RISR & RISR_DATA_OVER) && timeout--) {
        if (H3_SD_MMC0->RISR & RISR_ERRORS) return -5;
    }

    // Clear Interrupt Flags
    H3_SD_MMC0->RISR = RISR_DATA_OVER | RISR_CMD_DONE;

    return (timeout > 0) ? 0 : -6;
}

int sd_erase_blocks(uint32_t start_sector, uint32_t count) {
    if (count == 0) return 0;

    // 1. Calculate End Address
    // The Erase range is Inclusive (Start to End)
    uint32_t end_sector = start_sector + count - 1;

    // 2. Handle Addressing (Byte vs Block)
    uint32_t arg_start = start_sector;
    uint32_t arg_end   = end_sector;

    if (!is_high_capacity) {
        arg_start *= 512; // SDSC uses Byte Addressing
        arg_end   *= 512;
    }

    // 3. Send CMD32 (Erase Start Address)
    if (sd_send_cmd(CMD32, arg_start, CMD_RESP_EXP | CMD_CHECK_CRC) != 0) return -1;

    // 4. Send CMD33 (Erase End Address)
    if (sd_send_cmd(CMD33, arg_end, CMD_RESP_EXP | CMD_CHECK_CRC) != 0) return -2;

    // 5. Send CMD38 (Execute Erase)
    // Argument is ignored (0).
    // The card will drive the DAT0 line LOW (Busy) after this command.
    // We do NOT wait here manually. The next command you send will see 
    // the busy line and wait automatically via the hardware's CMD_WAIT_PRE logic.
    if (sd_send_cmd(CMD38, 0, CMD_RESP_EXP | CMD_CHECK_CRC) != 0) return -3;

    return 0; // Erase command accepted successfully
}

uint32_t sd_get_status(void) {
    // CMD13: Send Status (RCA is required)
    if (sd_send_cmd(CMD13, rca << 16, CMD_RESP_EXP | CMD_CHECK_CRC) != 0) {
        return 0xFFFFFFFF; // Error indicator
    }
    return H3_SD_MMC0->RESP0;
}

int sd_wait_ready(void) {
    int timeout = 100000;
    while (timeout--) {
        uint32_t status = sd_get_status();
        if (status == 0xFFFFFFFF) return -1;

        // Check "Current State" (Bits 9-12)
        // 4 = TRAN (Transfer State) - Ready for data
        uint32_t current_state = (status >> 9) & 0x0F;
        if (current_state == 4) return 0;
    }
    return -2; // Card stuck busy
}

int sd_set_bus_width_4bit(void) {
    // 1. Send ACMD6 to Card
    // (Remember: ACMD requires CMD55 first)
    if (sd_send_cmd(CMD55, rca << 16, CMD_RESP_EXP | CMD_CHECK_CRC) != 0) return -1;
    if (sd_send_cmd(ACMD6, 2, CMD_RESP_EXP | CMD_CHECK_CRC) != 0) return -2; // Arg 2 = 4-bit

    // 2. Update Host Controller
    H3_SD_MMC0->BWDR = 1; // 0=1-bit, 1=4-bit

    return 0;
}

// 25000000 MHz
int sd_set_speed(uint32_t frequency_hz) {
    // 1. Disable Clock
    H3_SD_MMC0->CKCR &= ~(1U << 16); 
    sd_update_clock();

    // 2. Calculate Divider (Simplified for H3)
    // For 24MHz Source: Div=0 (Bypass) -> 24MHz output
    // For 400kHz: Div=3 or 4
    uint32_t div = 0; 
    if (frequency_hz <= 400000) div = 4; // Slow speed

    H3_SD_MMC0->CKCR = (1U << 16) | div; // Enable | Divider
    return sd_update_clock();
}