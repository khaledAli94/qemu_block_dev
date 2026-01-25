#ifndef SDHC_H
#define SDHC_H

#include <sdhc.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>

// --- Register Map ---
#define H3_SD_MMC0 ((struct sdmmc_reg_t *)0x01c0f000)

struct sdmmc_reg_t {
    volatile uint32_t GCTL;       /* 0x00 Global Control */
    volatile uint32_t CKCR;       /* 0x04 Clock Control */
    volatile uint32_t TMOR;       /* 0x08 Timeout */
    volatile uint32_t BWDR;       /* 0x0C Bus Width */
    volatile uint32_t BKSR;       /* 0x10 Block Size */
    volatile uint32_t BYCR;       /* 0x14 Byte Count */
    volatile uint32_t CMDR;       /* 0x18 Command */
    volatile uint32_t CAGR;       /* 0x1C Command Argument */
    volatile uint32_t RESP0;      /* 0x20 Response 0 */
    volatile uint32_t RESP1;      /* 0x24 Response 1 */
    volatile uint32_t RESP2;      /* 0x28 Response 2 */
    volatile uint32_t RESP3;      /* 0x2C Response 3 */
    volatile uint32_t IMKR;       /* 0x30 Interrupt Mask */
    volatile uint32_t MISR;       /* 0x34 Masked Interrupt Status */
    volatile uint32_t RISR;       /* 0x38 Raw Interrupt Status */
    volatile uint32_t STAR;       /* 0x3C Status */
    volatile uint32_t FWLR;       /* 0x40 FIFO Water Level .aka FIFOTH */
    volatile uint32_t FUNS;       /* 0x44 FIFO Function Select */
    volatile uint32_t _RES1[4];
    volatile uint32_t A12A;       /* 0x58 Auto CMD12 Arg */
    volatile uint32_t NTSR;       /* 0x5C New Timing Set */
    volatile uint32_t SDBG;       /* 0x60 New Timing Debug */
    volatile uint32_t _RES2[5];
    volatile uint32_t HWRST;      /* 0x78 Hardware Reset */
    volatile uint32_t _RES3[1];
    volatile uint32_t DMAC;       /* 0x80 DMA Control */
    volatile uint32_t DLBA;       /* 0x84 Desc Base */
    volatile uint32_t IDST;       /* 0x88 DMA Status */
    volatile uint32_t IDIE;       /* 0x8C DMA IE */
    volatile uint32_t _RES4[28];
    volatile uint32_t THLDC;      /* 0x100 Threshold */
    volatile uint32_t _RES5[2];
    volatile uint32_t DSBD;       /* 0x10C DDR Start Bit Detection */
    volatile uint32_t RES_CRC;    /* 0x110 Write operation CRC status */
    volatile uint32_t DATA7_CRC;  /* 0x114 */
    volatile uint32_t DATA6_CRC;  /* 0x118 */
    volatile uint32_t DATA5_CRC;  /* 0x11C */
    volatile uint32_t DATA4_CRC;  /* 0x120 */
    volatile uint32_t DATA3_CRC;  /* 0x124 */
    volatile uint32_t DATA2_CRC;  /* 0x128 */
    volatile uint32_t DATA1_CRC;  /* 0x12C */
    volatile uint32_t DATA0_CRC;  /* 0x130 */
    volatile uint32_t CRC_STA;    /* 0x134 Response CRC from card/eMMC */
    volatile uint32_t _RES6[50];
    volatile uint32_t FIFO;       /* 0x200 Read/Write FIFO */
};

// --- Bit Definitions for Allwinner H3 SDHOST ---

// CMDR Bits
#define CMD_START       (1U << 31)
#define CMD_USE_HOLD    (1U << 29)
#define CMD_UP_CLK      (1U << 21)
#define CMD_WAIT_PRE    (1U << 13) // Wait for data transfer completion
#define CMD_WRITE       (1U << 10) // 0=Read, 1=Write
#define CMD_DATA_EXP    (1U << 9)  // Data transfer expected
#define CMD_CHECK_CRC   (1U << 8)
#define CMD_LONG_RESP   (1U << 7)
#define CMD_RESP_EXP    (1U << 6)
#define CMD_AUTO_STOP   (1U << 12) // Automatically send CMD12 after data transfer

// GCTL Bits
#define GCTL_HC_EN      (1U << 31)
#define GCTL_SOFT_RST   (1U << 0)
#define GCTL_FIFO_RST   (1U << 1)
#define GCTL_DMA_RST    (1U << 2)

// RISR (Interrupt Status) Bits
#define RISR_CMD_DONE   (1U << 2)
#define RISR_DATA_OVER  (1U << 3)
#define RISR_ERRORS     (0xbfc2)   // Mask for various errors

// --- Standard SD Command Definitions ---

/* Initialization & Identification */
#define CMD0    0   // GO_IDLE_STATE: Reset card to idle state
#define CMD2    2   // ALL_SEND_CID: Ask card to send its unique Card ID (CID)
#define CMD3    3   // SEND_RELATIVE_ADDR: Ask card to publish a new Relative Address (RCA)
#define CMD8    8   // SEND_IF_COND: Verify voltage range (Crucial for SDHC/SDXC support)
#define CMD9    9   // SEND_CSD: Ask card to send Card Specific Data (CSD) - Capacity, speed info

/* Configuration & Bus Control */
#define CMD6    6   // SWITCH_FUNC: Switch card function (Enable High Speed Mode)
#define CMD7    7   // SELECT_DESELECT_CARD: Toggle card between Standby and Transfer states
#define CMD16   16  // SET_BLOCKLEN: Set block length (Standard is 512). Required for SDSC cards.

/* Read Operations */
#define CMD17   17  // READ_SINGLE_BLOCK: Read 512 bytes from specific address
#define CMD18   18  // READ_MULTIPLE_BLOCK: Stream blocks until CMD12 is sent

/* Write Operations */
#define CMD24   24  // WRITE_BLOCK: Write 512 bytes to specific address
#define CMD25   25  // WRITE_MULTIPLE_BLOCK: Stream blocks to write until CMD12 is sent

/* Transfer Control & Status */
#define CMD12   12  // STOP_TRANSMISSION: Force stop reading/writing (Required for Multi-Block)
#define CMD13   13  // SEND_STATUS: Get 32-bit Status Register (Check for Ready/Error states)

/* Erase Commands (Production/Filesystem Optimization) */
#define CMD32   32  // ERASE_WR_BLK_START: Set first block address to erase
#define CMD33   33  // ERASE_WR_BLK_END: Set last block address to erase
#define CMD38   38  // ERASE: Execute erase on selected blocks (TRIM)

/* Application Command Prefix */
#define CMD55   55  // APP_CMD: Next command is an Application Specific Command (ACMD)

/* Application Specific Commands (Must send CMD55 first!) */
#define ACMD6   6   // SET_BUS_WIDTH: Switch between 1-bit and 4-bit data bus
#define ACMD41  41  // SD_SEND_OP_COND: Host Capacity Support (HCS) negotiation & Initialization
#define ACMD51  51  // SEND_SCR: Read SD Configuration Register (Find out if card supports 4-bit)

// --- Function Prototypes ---
int sd_init(void);
int sd_read_block(uint32_t sector, uint8_t *buffer);
int sd_read_blocks(uint32_t sector, int count, uint8_t *buffer);

int sd_write_block(uint32_t sector, const uint8_t *buffer);
int sd_write_blocks(uint32_t sector, int count, const uint8_t *buffer);

int sd_erase_blocks(uint32_t start_sector, uint32_t count);

uint32_t sd_get_status(void);
int sd_wait_ready(void);
int sd_set_bus_width_4bit(void);
int sd_set_speed(uint32_t frequency_hz);
#endif