/* user/atad.c —— 用户态 ATA 驱动进程（S6.5：IRQ14 版）
 *
 * 与 S6c 差别：
 *   - 注册 IRQ14（内核转发中断）
 *   - 每个扇区就绪用 IRQ14 唤醒，不再轮询 DRQ
 *   - 但"发命令前 BSY 清除"仍用轮询（IRQ14 在命令之前不会到）
 */

#include "syscall.h"
#include <stdint.h>

/* ATA 主通道端口 */
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

#define IRQ_ATA  14

static volatile struct ata_shm *head = 0;
static volatile uint8_t        *data = 0;

/* ---- helpers ---- */

static void ata_400ns_delay(void) {
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
}

/* 轮询等 BSY 清除（发命令前用；此时 IRQ14 还没来） */
static int ata_wait_bsy(void) {
    for (int i = 0; i < 1000000; i++) {
        if (!(inb(ATA_STATUS) & ATA_SR_BSY)) return 0;
    }
    return -1;
}

/* ★ S6.5: 等一个"有数据"的 IRQ14
   忽略残留 IRQ（status 无 DRQ 时继续等） */
static int wait_ata_drq(void) {
    user_msg_t m;
    for (;;) {
        if (sys_recv(&m) < 0) continue;
        if (m.type != MSG_IRQ) continue;
        if ((int)m.data[0] != IRQ_ATA) continue;

        uint8_t st = inb(ATA_STATUS);
        if (st & ATA_SR_ERR) return -1;
        if (st & ATA_SR_DF)  return -2;
        if (st & ATA_SR_DRQ) return 0;
        /* 没 DRQ 的 IRQ14 是残留，继续等 */
    }
}

/* ---- PIO (IRQ14 版) ---- */

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
        int r = wait_ata_drq();
        if (r < 0) return r - 10;

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
        int r = wait_ata_drq();
        if (r < 0) return r - 20;

        for (int i = 0; i < 256; i++)
            outw(ATA_DATA, *p++);
    }

    /* flush —— 也走 IRQ14 等待 */
    outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);
    {
        /* flush 完成的中断也是 IRQ14，但没有 DRQ；
           这里用一个"宽松"等待：只要收到 IRQ14 就继续 */
        user_msg_t m;
        for (;;) {
            if (sys_recv(&m) < 0) continue;
            if (m.type == MSG_IRQ && (int)m.data[0] == IRQ_ATA) break;
        }
    }

    return 0;
}

/* ---- 处理一次内核请求 ---- */

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

/* ---- 主循环 ---- */

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
            continue;
        }

        /* IRQ14 不在 handle_ata_req 里被消费的，直接忽略
           （正常情况不会有残留） */
    }
}

int main(void) {
    /* 1. 申请 ATA 主通道 + 控制端口 */
    for (int p = 0x1F0; p <= 0x1F7; p++) {
        if (sys_io_perm(p) < 0) sys_exit();
    }
    if (sys_io_perm(ATA_CTRL) < 0) sys_exit();

    /* 2. ★ S6.5: 注册 IRQ14（不读端口） */
    sys_irq_register(IRQ_ATA, IRQ_NO_READ);

    /* 3. 分配 8KB 共享内存块 */
    void *va = sys_shm_alloc();
    if (!va) sys_exit();

    head = (volatile struct ata_shm *)va;
    data = (volatile uint8_t *)va + SHM_HEAD_SIZE;

    head->magic  = ATA_SHM_MAGIC;
    head->status = ATA_ST_IDLE;
    head->result = 0;

    /* 4. IPC 循环 */
    atad_loop();

    sys_exit();
    return 0;
}