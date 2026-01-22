#ifndef SDHC_H
#define SDHC_H
#include <stdint.h>

// --- Hardware Registers (VExpress-A9) ---
#define MMCI_BASE       0x10005000

// PL181 Memory-mapped registers (from ARM PrimeCell PL181 specification)
#define MMCI_POWER      (*(volatile uint32_t *)(MMCI_BASE + 0x000))
#define MMCI_CLOCK      (*(volatile uint32_t *)(MMCI_BASE + 0x004))
#define MMCI_ARG        (*(volatile uint32_t *)(MMCI_BASE + 0x008))
#define MMCI_CMD        (*(volatile uint32_t *)(MMCI_BASE + 0x00C))
#define MMCI_RESP0      (*(volatile uint32_t *)(MMCI_BASE + 0x014))  // Response 0
#define MMCI_RESP1      (*(volatile uint32_t *)(MMCI_BASE + 0x018))  // Response 1
#define MMCI_RESP2      (*(volatile uint32_t *)(MMCI_BASE + 0x01C))  // Response 2
#define MMCI_RESP3      (*(volatile uint32_t *)(MMCI_BASE + 0x020))  // Response 3
#define MMCI_DATATIMER  (*(volatile uint32_t *)(MMCI_BASE + 0x024))
#define MMCI_DATALEN    (*(volatile uint32_t *)(MMCI_BASE + 0x028))
#define MMCI_DATACTRL   (*(volatile uint32_t *)(MMCI_BASE + 0x02C))
#define MMCI_STATUS     (*(volatile uint32_t *)(MMCI_BASE + 0x034))
#define MMCI_CLEAR      (*(volatile uint32_t *)(MMCI_BASE + 0x038))
#define MMCI_MASK0      (*(volatile uint32_t *)(MMCI_BASE + 0x03C))
#define MMCI_FIFO_CNT   (*(volatile uint32_t *)(MMCI_BASE + 0x048))  // FIFO count register
#define MMCI_FIFO       (*(volatile uint32_t *)(MMCI_BASE + 0x080))  // FIFO data register

// SD Commands (QEMU compatible)
#define CMD0   0   // GO_IDLE_STATE
#define CMD2   2   // ALL_SEND_CID
#define CMD3   3   // SEND_RELATIVE_ADDR
#define CMD7   7   // SELECT_CARD
#define CMD8   8   // SEND_IF_COND
#define CMD9   9   // SEND_CSD
#define CMD13  13  // SEND_STATUS
#define CMD16  16  // SET_BLOCKLEN
#define CMD17  17  // READ_SINGLE_BLOCK
#define CMD18  18  // READ_MULTIPLE_BLOCK
#define CMD24  24  // WRITE_SINGLE_BLOCK
#define CMD25  25  // WRITE_MULTIPLE_BLOCK
#define CMD38  38  // ERASE (for SD erase command)
#define CMD55  55  // APP_CMD
#define ACMD41 41  // SD_SEND_OP_COND

// Status bits (from PL181 specification)
#define STAT_CMD_CRC_FAIL      (1 << 0)
#define STAT_DATA_CRC_FAIL     (1 << 1)
#define STAT_CMD_TIMEOUT       (1 << 2)
#define STAT_DATA_TIMEOUT      (1 << 3)
#define STAT_TX_UNDERRUN       (1 << 4)
#define STAT_RX_OVERRUN        (1 << 5)
#define STAT_CMD_RESP_END      (1 << 6)
#define STAT_CMD_SENT          (1 << 7)
#define STAT_DATA_END          (1 << 8)
#define STAT_START_BIT_ERR     (1 << 9)
#define STAT_DATA_BLOCK_END    (1 << 10)  // This is the correct one
#define STAT_CMD_ACTIVE        (1 << 11)
#define STAT_TX_ACTIVE         (1 << 12)
#define STAT_RX_ACTIVE         (1 << 13)
#define STAT_TX_FIFO_HALF_EMPTY (1 << 14)  // TX FIFO half empty
#define STAT_RX_FIFO_HALF_FULL  (1 << 15)  // RX FIFO half full
#define STAT_TX_FIFO_FULL       (1 << 16)
#define STAT_RX_FIFO_FULL       (1 << 17)
#define STAT_TX_FIFO_EMPTY      (1 << 18)
#define STAT_RX_FIFO_EMPTY      (1 << 19)
#define STAT_TX_DATA_AVAIL      (1 << 20)
#define STAT_RX_DATA_AVAIL      (1 << 21)

// Legacy/compatibility definitions 
#define STAT_TX_FIFO_HALF       STAT_TX_FIFO_HALF_EMPTY
#define STAT_RX_FIFO_HALF       STAT_RX_FIFO_HALF_FULL

// QEMU-specific: SD card in VExpress-A9 is usually 64MB (131072 blocks of 512 bytes)
#define SD_BLOCK_SIZE   512
#define SD_BLOCK_COUNT  131072  // QEMU's default SD card size

// Card status bits (from SD specification)
#define CARD_STATUS_READY_FOR_DATA    (1 << 8)
#define CARD_STATUS_CURRENT_STATE(x)  (((x) >> 9) & 0xF)
#define CARD_STATE_IDLE       0
#define CARD_STATE_READY      1
#define CARD_STATE_IDENT      2
#define CARD_STATE_STBY       3
#define CARD_STATE_TRAN       4  // Transfer state
#define CARD_STATE_DATA       5
#define CARD_STATE_RCV        6
#define CARD_STATE_PRG        7
#define CARD_STATE_DIS        8


void sd_init();
int sd_send_cmd(int cmd, uint32_t arg, int resp);
void sd_read_sector(uint32_t sector, uint8_t *buffer);
int sd_read_multiple_sectors(uint32_t sector, uint32_t count, uint8_t *buffer);

void sd_write_sector(uint32_t sector, const uint8_t *buffer);
int sd_write_multiple_sectors(uint32_t start_sector, uint32_t count, const uint8_t *buffer);

int sd_erase_sector(uint32_t start_sector, uint32_t end_sector);

#endif