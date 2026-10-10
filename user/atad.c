/* user/atad.c —— 用户态 ATA 驱动进程（最终版：纯轮询 PIO） */

#include "syscall.h"
#include <stdint.h>

#define ATA_DATA       0x1F0
#define ATA_ERR        0x1F1
#define ATA_FEAT       0x1F1
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_CTRL       0x3F6

#define ATA_SR_BSY     0x80
#define ATA_SR_DRDY    0x40
#define ATA_SR_DF      0x20
#define ATA_SR_DRQ     0x08
#define ATA_SR_ERR     0x01

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_CACHE_FLUSH 0xE7

static volatile struct ata_shm *head = 0;
static volatile uint8_t        *data = 0;

static void ata_400ns_delay(void) {
    inb(ATA_STATUS); inb(ATA_STATUS);
    inb(ATA_STATUS); inb(ATA_STATUS);
}

static int ata_wait_bsy(void) {
    for (int i = 0; i < 1000000; i++) {
        if (!(inb(ATA_STATUS) & ATA_SR_BSY)) return 0;
    }
    return -1;
}

/* 轮询等 DRQ（PIO 模式唯一正确做法） */
static int ata_wait_drq(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DF)  return -2;
        if (s & ATA_SR_DRQ) return 0;
    }
    return -3;
}

static int ata_pio_read(uint32_t lba, uint8_t count, void *buf) {
    if (count == 0) return 0;
    if (count > ATA_SH_MAX_SEC) count = ATA_SH_MAX_SEC;

    if (ata_wait_bsy() < 0) return -1;

    outb(ATA_DRIVE,    0xE0 | ((lba >> 24) & 0x0F));
    ata_400ns_delay();
    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA_LO,   (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI,   (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND,  ATA_CMD_READ_PIO);

    uint16_t *p = (uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_bsy() < 0) return -2;
        if (ata_wait_drq() < 0) return -3;
        for (int i = 0; i < 256; i++)
            *p++ = inw(ATA_DATA);
    }
    return 0;
}

static int ata_pio_write(uint32_t lba, uint8_t count, const void *buf) {
    if (count == 0) return 0;
    if (count > ATA_SH_MAX_SEC) count = ATA_SH_MAX_SEC;

    if (ata_wait_bsy() < 0) return -1;

    outb(ATA_DRIVE,    0xE0 | ((lba >> 24) & 0x0F));
    ata_400ns_delay();
    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA_LO,   (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI,   (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND,  ATA_CMD_WRITE_PIO);

    const uint16_t *p = (const uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_bsy() < 0) return -2;
        if (ata_wait_drq() < 0) return -3;
        for (int i = 0; i < 256; i++)
            outw(ATA_DATA, *p++);
    }

    outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy();
    return 0;
}

static void handle_ata_req(void) {
    head->status = ATA_ST_BUSY;
    __asm__ volatile("" ::: "memory");

    int r;
    uint32_t op    = head->op;
    uint32_t lba   = head->lba;
    uint32_t count = head->count;

    if (count == 0 || count > ATA_SH_MAX_SEC) {
        head->result = -1;
        head->status = ATA_ST_ERR;
        __asm__ volatile("" ::: "memory");
        return;
    }

    if (op == ATA_OP_READ) {
        r = ata_pio_read(lba, (uint8_t)count, (void *)data);
    } else if (op == ATA_OP_WRITE) {
        r = ata_pio_write(lba, (uint8_t)count, (const void *)data);
    } else {
        r = -1;
    }

    head->result = (uint32_t)r;
    head->status = (r == 0) ? ATA_ST_DONE : ATA_ST_ERR;
    __asm__ volatile("" ::: "memory");
}

static int shell_tid = -1;

static void atad_loop(void) {
    user_msg_t m;

    for (;;) {
        if (sys_recv(&m) < 0) continue;

        if (m.type == MSG_HELLO) {
            shell_tid = m.sender;
            sys_ata_activate();
            continue;
        }

        if (m.type == MSG_EXIT) {
            sys_exit();
        }

        if (m.type == MSG_ATA_REQ) {
            handle_ata_req();
            sys_yield();
            continue;
        }
    }
}

int main(void) {
    for (int p = 0x1F0; p <= 0x1F7; p++) {
        if (sys_io_perm(p) < 0) sys_exit();
    }
    if (sys_io_perm(ATA_CTRL) < 0) sys_exit();

    /* 清 nIEN（虽然 PIO 用不上，但也无害） */
    outb(ATA_CTRL, 0x00);

    /* 不再注册 IRQ14 */

    void *va = sys_shm_alloc();
    if (!va) sys_exit();

    head = (volatile struct ata_shm *)va;
    data = (volatile uint8_t *)va + SHM_HEAD_SIZE;

    head->magic  = ATA_SHM_MAGIC;
    head->status = ATA_ST_IDLE;
    head->result = 0;

    atad_loop();

    sys_exit();
    return 0;
}