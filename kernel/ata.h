#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_SECTOR_SIZE 512

#define ATA_DRIVE_MASTER  0
#define ATA_DRIVE_SLAVE   1

/* ★ S6c: 共享内存 ATA 请求头（放在共享页的第 0 个 4KB 开头 32 字节） */
struct ata_shm {
    uint32_t magic;
    uint32_t op;
    uint32_t drive;
    uint32_t lba;
    uint32_t count;
    uint32_t status;
    uint32_t result;
    uint32_t _pad;
};

#define ATA_SHM_MAGIC    0x41544131u
#define ATA_OP_READ      0
#define ATA_OP_WRITE     1
#define ATA_ST_IDLE      0
#define ATA_ST_BUSY      1
#define ATA_ST_DONE      2
#define ATA_ST_ERR       3

#define ATA_SH_DATA_OFF  4096       /* 数据在共享页第 2 页 */
#define ATA_SH_MAX_SEC   8          /* 一次最多 8 扇区（4KB） */

int ata_init(void);

int ata_read_sectors_ex(int drive, uint32_t lba, uint8_t count, void *buf);
int ata_write_sectors_ex(int drive, uint32_t lba, uint8_t count, const void *buf);

int ata_read_sectors(uint32_t lba, uint8_t count, void *buf);
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf);

/* ★ S6c: IPC 通道开关 */
void ata_ipc_set_owner(int shm_slot);   /* 开启，指定 atad 的 shm slot */
void ata_ipc_clear(void);               /* 关闭，回退 PIO */
int  ata_ipc_get_slot(void);            /* -1 = 关 */

#endif