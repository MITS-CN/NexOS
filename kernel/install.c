#include "install.h"
#include "ata.h"

extern const uint8_t stage1_data_start[];
extern const uint8_t stage1_data_end[];
extern const uint8_t stage2_data_start[];
extern const uint8_t stage2_data_end[];
extern const uint8_t *g_init_elf_data;
extern uint32_t       g_init_elf_size;
extern char _kernel_start[];
extern char _kernel_raw_end[];

#define NXFS_PART_LBA       2048
#define NXFS_BLOCK_SECTORS  8
#define NXFS_BLOCK_SIZE     (NXFS_BLOCK_SECTORS * 512)

static int installer_mode = 0;

void install_set_installer_mode(int on) { installer_mode = on; }
int  install_get_installer_mode(void)   { return installer_mode; }

static uint32_t raw_kernel_size(void) {
    return (uint32_t)(_kernel_raw_end - _kernel_start);
}

/* 1. stage1 → LBA 0 */
static int write_stage1(int drv) {
    uint32_t sz = (uint32_t)(stage1_data_end - stage1_data_start);
    if (sz != 512) return -1;
    return ata_write_sectors_ex(drv, 0, 1, stage1_data_start);
}

/* 2. stage2 → LBA 1（16 扇区） */
static int write_stage2(int drv) {
    uint32_t sz = (uint32_t)(stage2_data_end - stage2_data_start);
    if (sz > 16 * 512) return -1;

    uint8_t buf[16 * 512];
    for (uint32_t i = 0; i < sizeof(buf); i++) buf[i] = 0;
    for (uint32_t i = 0; i < sz; i++) buf[i] = stage2_data_start[i];

    return ata_write_sectors_ex(drv, 1, 16, buf);
}

/* 3. kernel payload（512B 头 + raw kernel）→ LBA 17 */
static int write_kernel(int drv) {
    uint32_t ksize = raw_kernel_size();

    uint8_t hdr[512];
    for (int i = 0; i < 512; i++) hdr[i] = 0;
    hdr[0] = 'N'; hdr[1] = 'E'; hdr[2] = 'X'; hdr[3] = 'K';
    hdr[4] = ksize & 0xFF;
    hdr[5] = (ksize >> 8) & 0xFF;
    hdr[6] = (ksize >> 16) & 0xFF;
    hdr[7] = (ksize >> 24) & 0xFF;

    if (ata_write_sectors_ex(drv, 17, 1, hdr) < 0) return -1;

    const uint8_t *p = (const uint8_t *)_kernel_start;
    uint32_t ksecs = (ksize + 511) / 512;
    uint32_t lba = 18;
    uint32_t left = ksecs;

    while (left > 0) {
        uint32_t cnt = left > 128 ? 128 : left;
        if (ata_write_sectors_ex(drv, lba, (uint8_t)cnt, p) < 0) return -1;
        lba  += cnt;
        p    += cnt * 512;
        left -= cnt;
    }
    return 0;
}

/* 4. NXFS 区（含 /init.elf） */
static int write_nxfs(int drv) {
    uint32_t elf_size   = g_init_elf_size;
    uint32_t elf_blocks = (elf_size + NXFS_BLOCK_SIZE - 1) / NXFS_BLOCK_SIZE;

    /* 4a. 超级块 */
    uint8_t sb[512];
    for (int i = 0; i < 512; i++) sb[i] = 0;
    uint32_t *s = (uint32_t *)sb;
    s[0] = 0x5346584E;   /* magic */
    s[1] = 1;            /* version */
    s[2] = 8;            /* block_sectors */
    s[3] = 8192;         /* total_blocks */
    s[4] = 2;            /* fat_lba */
    s[5] = 64;           /* fat_sectors */
    s[6] = 66;           /* data_lba */
    s[7] = 0;            /* root_block */
    if (ata_write_sectors_ex(drv, NXFS_PART_LBA + 1, 1, sb) < 0) return -1;

    /* 4b. FAT：全 0，块 0 = EOF，块 1..elf_blocks = 链 */
    uint8_t fat_sec[512];
    for (int i = 0; i < 512; i++) fat_sec[i] = 0;
    uint32_t *f = (uint32_t *)fat_sec;
    f[0] = 0xFFFFFFFF;   /* 根目录块 */
    if (ata_write_sectors_ex(drv, NXFS_PART_LBA + 2, 1, fat_sec) < 0) return -1;

    /* 剩余 FAT 扇区先全 0 */
    for (int i = 0; i < 512; i++) fat_sec[i] = 0;
    for (uint32_t i = 1; i < 64; i++) {
        if (ata_write_sectors_ex(drv, NXFS_PART_LBA + 2 + i, 1, fat_sec) < 0)
            return -1;
    }

    /* 4c. 更新 FAT 项 1..elf_blocks */
    for (uint32_t b = 1; b <= elf_blocks; b++) {
        uint32_t next = (b == elf_blocks) ? 0xFFFFFFFF : (b + 1);
        uint32_t byte_off = b * 4;
        uint32_t sec = byte_off / 512;
        uint32_t off = byte_off % 512;

        uint8_t tmp[512];
        if (ata_read_sectors_ex(drv, NXFS_PART_LBA + 2 + sec, 1, tmp) < 0)
            return -1;
        uint32_t *t32 = (uint32_t *)tmp;
        t32[off / 4] = next;
        if (ata_write_sectors_ex(drv, NXFS_PART_LBA + 2 + sec, 1, tmp) < 0)
            return -1;
    }

    /* 4d. 根目录块（块 0）含 /init.elf 目录项 */
    uint8_t root[NXFS_BLOCK_SIZE];
    for (uint32_t i = 0; i < sizeof(root); i++) root[i] = 0;

    uint8_t *e = root;
    const char *name = "init.elf";
    for (int i = 0; i < 24; i++) e[i] = 0;
    for (int i = 0; name[i] && i < 23; i++) e[i] = name[i];
    e[24] = 1;              /* type = file */
    e[28] =  elf_size        & 0xFF;
    e[29] = (elf_size >> 8)  & 0xFF;
    e[30] = (elf_size >> 16) & 0xFF;
    e[31] = (elf_size >> 24) & 0xFF;
    e[32] = 1;              /* first_block = 1 */
    e[33] = 0; e[34] = 0; e[35] = 0;

    /* 数据块 0 的 LBA = NXFS_PART_LBA + 66 */
    if (ata_write_sectors_ex(drv, NXFS_PART_LBA + 66, 8, root) < 0) return -1;

    /* 4e. init.elf 数据写到块 1..elf_blocks */
    const uint8_t *p = g_init_elf_data;
    uint32_t remaining = elf_size;
    uint32_t blk = 1;

    while (remaining > 0) {
        uint32_t take = remaining > NXFS_BLOCK_SIZE
                      ? NXFS_BLOCK_SIZE : remaining;

        uint8_t buf[NXFS_BLOCK_SIZE];
        for (uint32_t i = 0; i < NXFS_BLOCK_SIZE; i++) buf[i] = 0;
        for (uint32_t i = 0; i < take; i++) buf[i] = p[i];

        uint32_t lba = NXFS_PART_LBA + 66 + blk * 8;
        if (ata_write_sectors_ex(drv, lba, 8, buf) < 0) return -1;

        p += take;
        remaining -= take;
        blk++;
    }

    return 0;
}

int install_to_drive(int drive) {
    if (!installer_mode) {
        return -100;   /* 不是安装器模式 */
    }
    if (!g_init_elf_data || g_init_elf_size == 0) return -1;

    if (write_stage1(drive) < 0) return -2;
    if (write_stage2(drive) < 0) return -3;
    if (write_kernel(drive) < 0) return -4;
    if (write_nxfs(drive)   < 0) return -5;

    return 0;
}