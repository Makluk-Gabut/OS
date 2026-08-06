#include "ata.h"
#include "io.h"

#define ATA_REG_DATA     0x1F0
#define ATA_REG_SECCOUNT 0x1F2
#define ATA_REG_LBA_LO   0x1F3
#define ATA_REG_LBA_MID  0x1F4
#define ATA_REG_LBA_HI   0x1F5
#define ATA_REG_DRIVE    0x1F6
#define ATA_REG_STATUS   0x1F7
#define ATA_REG_COMMAND  0x1F7

#define ATA_CMD_READ_PIO     0x20
#define ATA_CMD_WRITE_PIO    0x30
#define ATA_CMD_CACHE_FLUSH  0xE7

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

static void ata_wait_bsy(void) {
    while (inb(ATA_REG_STATUS) & ATA_SR_BSY) {
    }
}

static int ata_wait_drq(void) {
    uint8_t status;
    do {
        status = inb(ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return 0;
    } while (!(status & ATA_SR_DRQ));
    return 1;
}

static void ata_select_lba(uint32_t lba) {
    outb(ATA_REG_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_REG_SECCOUNT, 1);
    outb(ATA_REG_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_REG_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
}

int ata_read_sector(uint32_t lba, uint8_t* buffer) {
    ata_wait_bsy();
    ata_select_lba(lba);
    outb(ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    if (!ata_wait_drq()) return 0;

    uint16_t* buf16 = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        buf16[i] = inw(ATA_REG_DATA);
    }
    return 1;
}

int ata_write_sector(uint32_t lba, const uint8_t* buffer) {
    ata_wait_bsy();
    ata_select_lba(lba);
    outb(ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    if (!ata_wait_drq()) return 0;

    const uint16_t* buf16 = (const uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        outw(ATA_REG_DATA, buf16[i]);
    }

    outb(ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy();
    return 1;
}
