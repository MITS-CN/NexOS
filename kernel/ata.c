#include "ata.h"
#include "io.h"
#include "ipc.h"      /* ★ S6c */
#include "shm.h"      /* ★ S6c */
#include "thread.h"   /* ★ S6c */

#define ATA_PRIMARY_IO    0x1F0
#define ATA_PRIMARY_CTRL  0x3F6

#define ATA_REG_DATA      0
#define ATA_REG_SECCOUNT  2
#define ATA_REG_LBA_LO    3
#define ATA_REG_LBA_MID   4
#define ATA_REG_LBA_HI    5
#define ATA_REG_DRIVE     6
#define ATA_REG_STATUS    7
#define ATA_REG_COMMAND   7

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_CACHE_FLUSH 0xE7

/* ★ S6c: IPC 通道状态 */
static int ata_ipc_slot = -1;   /* -1 = 关闭，走 PIO */

void ata_ipc_set_owner(int shm_slot) { ata_ipc_slot = shm_slot; }
void ata_ipc_clear(void)             { ata_ipc_slot = -1; }
int  ata_ipc_get_slot(void)          { return ata_ipc_slot; }

/* ---- PIO 实现（原样保留） ---- */

static void ata_400ns_delay(void) {
    inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
}

static int ata_wait_bsy(void) {
    for (int i = 0; i < 1000000; i++) {
        if (!(inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY))
            return 0;
    }
    return -1;
}

static int ata_wait_drq(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DF)  return -2;
        if (s & ATA_SR_DRQ) return 0;
    }
    return -3;
}

static int ata_select(int drive) {
    uint8_t sel = (drive == ATA_DRIVE_MASTER) ? 0xA0 : 0xB0;
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, sel);
    ata_400ns_delay();

    for (int i = 0; i < 100000; i++) {
        uint8_t s = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (s == 0) return -1;
        if (s & ATA_SR_ERR) return -1;
        if (!(s & ATA_SR_BSY) && (s & ATA_SR_DRDY)) return 0;
    }
    return -1;
}

static int ata_identify(int drive) {
    if (ata_select(drive) < 0) return -1;

    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO,   0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID,  0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI,   0);
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND,  ATA_CMD_IDENTIFY);

    uint8_t s = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (s == 0) return -1;

    if (ata_wait_bsy() < 0) return -1;

    if (inb(ATA_PRIMARY_IO + ATA_REG_LBA_MID) != 0) return -1;
    if (inb(ATA_PRIMARY_IO + ATA_REG_LBA_HI)  != 0) return -1;

    if (ata_wait_drq() < 0) return -1;

    for (int i = 0; i < 256; i++) inw(ATA_PRIMARY_IO + ATA_REG_DATA);

    return 0;
}

int ata_init(void) {
    outb(ATA_PRIMARY_CTRL, 0x04);
    ata_400ns_delay();
    outb(ATA_PRIMARY_CTRL, 0x00);
    ata_400ns_delay();

    if (ata_identify(ATA_DRIVE_MASTER) < 0) return -1;

    return 0;
}

static int ata_pio_read_ex(int drive, uint32_t lba, uint8_t count, void *buf) {
    if (count == 0) return 0;

    uint8_t sel = (drive == ATA_DRIVE_MASTER) ? 0xE0 : 0xF0;

    if (ata_wait_bsy() < 0) return -1;

    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, sel | ((lba >> 24) & 0x0F));
    ata_400ns_delay();

    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO,   (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI,   (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND,  ATA_CMD_READ_PIO);

    uint16_t *p = (uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_bsy() < 0) return -2;
        if (ata_wait_drq() < 0) return -3;
        for (int i = 0; i < 256; i++)
            *p++ = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }
    return 0;
}

static int ata_pio_write_ex(int drive, uint32_t lba, uint8_t count, const void *buf) {
    if (count == 0) return 0;

    uint8_t sel = (drive == ATA_DRIVE_MASTER) ? 0xE0 : 0xF0;

    if (ata_wait_bsy() < 0) return -1;

    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, sel | ((lba >> 24) & 0x0F));
    ata_400ns_delay();

    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO,   (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI,   (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND,  ATA_CMD_WRITE_PIO);

    const uint16_t *p = (const uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_bsy() < 0) return -2;
        if (ata_wait_drq() < 0) return -3;
        for (int i = 0; i < 256; i++)
            outw(ATA_PRIMARY_IO + ATA_REG_DATA, *p++);
    }

    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy();
    return 0;
}

/* ---- ★ S6c: IPC 路径 ---- */

static int ata_ipc_do(int op, uint32_t lba, uint8_t count, void *buf) {
    if (ata_ipc_slot < 0) return -1;

    int atad_tid = shm_get_owner(ata_ipc_slot);
    if (atad_tid < 0) return -1;

    uint32_t phys = shm_get_phys(ata_ipc_slot);
    if (phys == 0) return -1;

    volatile struct ata_shm *head = (volatile struct ata_shm *)phys;
    uint8_t *data = (uint8_t *)(phys + ATA_SH_DATA_OFF);

    uint8_t *io_buf = (uint8_t *)buf;
    uint8_t remaining = count;

    while (remaining > 0) {
        uint8_t batch = remaining > ATA_SH_MAX_SEC ? ATA_SH_MAX_SEC : remaining;

        /* WRITE: 数据从 buf 拷进共享页 */
        if (op == ATA_OP_WRITE) {
            for (uint32_t i = 0; i < batch * 512; i++)
                data[i] = io_buf[i];
        }

        head->magic  = ATA_SHM_MAGIC;
        head->op     = (uint32_t)op;
        head->drive  = 0;
        head->lba    = lba;
        head->count  = (uint32_t)batch;
        head->result = 0;
        head->status = ATA_ST_IDLE;

        __asm__ volatile("" ::: "memory");

        message_t m;
        m.sender  = -1;
        m.type    = MSG_ATA_REQ;
        m.data[0] = 0;
        for (int i = 1; i < 8; i++) m.data[i] = 0;
        ipc_send(atad_tid, &m);

        while (head->status != ATA_ST_DONE && head->status != ATA_ST_ERR) {
            __asm__ volatile("sti; hlt");
        }

        if (head->status == ATA_ST_ERR) return -1;

        /* READ: 数据从共享页拷出 */
        if (op == ATA_OP_READ) {
            for (uint32_t i = 0; i < batch * 512; i++)
                io_buf[i] = data[i];
        }

        io_buf    += batch * 512;
        lba       += batch;
        remaining -= batch;
    }

    return 0;
}

/* ---- 对外接口：入口检查 IPC 开关 ---- */

int ata_read_sectors_ex(int drive, uint32_t lba, uint8_t count, void *buf) {
    if (drive == ATA_DRIVE_MASTER && ata_ipc_slot >= 0)
        return ata_ipc_do(ATA_OP_READ, lba, count, buf);
    return ata_pio_read_ex(drive, lba, count, buf);
}

int ata_write_sectors_ex(int drive, uint32_t lba, uint8_t count, const void *buf) {
    if (drive == ATA_DRIVE_MASTER && ata_ipc_slot >= 0)
        return ata_ipc_do(ATA_OP_WRITE, lba, count, (void *)buf);
    return ata_pio_write_ex(drive, lba, count, buf);
}

int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    return ata_read_sectors_ex(ATA_DRIVE_MASTER, lba, count, buf);
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    return ata_write_sectors_ex(ATA_DRIVE_MASTER, lba, count, buf);
}