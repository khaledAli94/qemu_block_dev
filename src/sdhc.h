#include <stdint.h>

// --- Hardware Registers (VExpress-A9) ---
#define MMCI_BASE       0x10005000
#define MMCI_POWER      (*(volatile uint32_t *)(MMCI_BASE + 0x000))
#define MMCI_CLOCK      (*(volatile uint32_t *)(MMCI_BASE + 0x004))
#define MMCI_ARG        (*(volatile uint32_t *)(MMCI_BASE + 0x008))
#define MMCI_CMD        (*(volatile uint32_t *)(MMCI_BASE + 0x00C))
#define MMCI_RESP0      (*(volatile uint32_t *)(MMCI_BASE + 0x014))
#define MMCI_DATATIMER  (*(volatile uint32_t *)(MMCI_BASE + 0x024))
#define MMCI_DATALEN    (*(volatile uint32_t *)(MMCI_BASE + 0x028))
#define MMCI_DATACTRL   (*(volatile uint32_t *)(MMCI_BASE + 0x02C))
#define MMCI_STATUS     (*(volatile uint32_t *)(MMCI_BASE + 0x034))
#define MMCI_CLEAR      (*(volatile uint32_t *)(MMCI_BASE + 0x038))
#define MMCI_FIFO       (*(volatile uint32_t *)(MMCI_BASE + 0x080))



void sd_init();
int sd_send_cmd(int cmd, uint32_t arg, int resp);
void sd_read_sector(uint32_t sector, uint8_t *buffer);