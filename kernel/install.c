#include "install.h"
#include "ata.h"

extern const uint8_t stage1_data_start[];
extern const uint8_t stage1_data_end[];
extern const uint8_t stage2_data_start[];
extern const uint8_t stage2_data_end[];
extern char _kernel_start[];
extern char _kernel_raw_end[];

#define NXFS_PART_LBA       2048
#define NXFS_BLOCK_SECTORS  8
#define NXFS_BLOCK_SIZE     (NXFS_BLOCK_SECTORS * 512)
#define NXFS_DIRENT_SIZE    36

static int installer_mode = 0;

void install_set_installer_mode(int on) { installer_mode = on; }
int  install_get_installer_mode(void)   { return installer_mode; }

static uint32_t raw_kernel_size(void) {
    return (uint32_t)(_kernel_raw_end - _kernel_start);
}

static int write_stage1(int drv) {
    uint32_t sz = (uint32_t)(stage1_data_end - stage1_data_start);
    if (sz != 512) return -1;
    return ata_write_sectors_ex(drv, 0, 1, stage1_data_start);
}

static int write_stage2(int drv) {
    uint32_t sz = (uint32_t)(stage2_data_end - stage2_data_start);
    if (sz > 16 * 512) return -1;

    uint8_t buf[16 * 512];
    for (uint32_t i = 0; i < sizeof(buf); i++) buf[i] = 0;
    for (uint32_t i = 0; i < sz; i++) buf[i] = stage2_data_start[i];

    return ata_write_sectors_ex(drv, 1, 16, buf);
}

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

static void set_dirent(uint8_t *block, int slot,
                       const char *name, int type,
                       uint32_t size, uint32_t first_block) {
    uint8_t *e = block + slot * NXFS_DIRENT_SIZE;
    for (int i = 0; i < NXFS_DIRENT_SIZE; i++) e[i] = 0;
    int i = 0;
    while (name[i] && i < 23) { e[i] = (uint8_t)name[i]; i++; }
    e[24] = (uint8_t)type;
    e[28] =  size        & 0xFF;
    e[29] = (size >> 8)  & 0xFF;
    e[30] = (size >> 16) & 0xFF;
    e[31] = (size >> 24) & 0xFF;
    e[32] =  first_block        & 0xFF;
    e[33] = (first_block >> 8)  & 0xFF;
    e[34] = (first_block >> 16) & 0xFF;
    e[35] = (first_block >> 24) & 0xFF;
}

static int write_data_block(int drv, uint32_t block, const uint8_t *data) {
    uint32_t lba = NXFS_PART_LBA + 66 + block * NXFS_BLOCK_SECTORS;
    return ata_write_sectors_ex(drv, lba, NXFS_BLOCK_SECTORS, data);
}

static int write_nxfs(int drv) {
    uint32_t init_size  = g_init_elf_size;
    uint32_t kbd_size   = g_kbd_elf_size;
    uint32_t vga_size   = g_vga_elf_size;
    uint32_t mouse_size = g_mouse_elf_size;
    if (!g_init_elf_data  || init_size  == 0) return -1;
    if (!g_kbd_elf_data   || kbd_size   == 0) return -1;
    if (!g_vga_elf_data   || vga_size   == 0) return -1;
    if (!g_mouse_elf_data || mouse_size == 0) return -1;

    uint32_t init_blocks  = (init_size  + NXFS_BLOCK_SIZE - 1) / NXFS_BLOCK_SIZE;
    uint32_t kbd_blocks   = (kbd_size   + NXFS_BLOCK_SIZE - 1) / NXFS_BLOCK_SIZE;
    uint32_t vga_blocks   = (vga_size   + NXFS_BLOCK_SIZE - 1) / NXFS_BLOCK_SIZE;
    uint32_t mouse_blocks = (mouse_size + NXFS_BLOCK_SIZE - 1) / NXFS_BLOCK_SIZE;

    uint32_t init_first  = 4;
    uint32_t kbd_first   = init_first  + init_blocks;
    uint32_t vga_first   = kbd_first   + kbd_blocks;
    uint32_t mouse_first = vga_first   + vga_blocks;
    uint32_t total_used  = mouse_first + mouse_blocks;

    if (total_used > 128) return -1;

    /* 超级块 */
    uint8_t sb[512];
    for (int i = 0; i < 512; i++) sb[i] = 0;
    uint32_t *s = (uint32_t *)sb;
    s[0] = 0x5346584E;
    s[1] = 1;
    s[2] = 8;
    s[3] = 8192;
    s[4] = 2;
    s[5] = 64;
    s[6] = 66;
    s[7] = 0;
    if (ata_write_sectors_ex(drv, NXFS_PART_LBA + 1, 1, sb) < 0) return -1;

    /* FAT 清零 */
    uint8_t zero512[512];
    for (int i = 0; i < 512; i++) zero512[i] = 0;
    for (uint32_t i = 0; i < 64; i++) {
        if (ata_write_sectors_ex(drv, NXFS_PART_LBA + 2 + i, 1, zero512) < 0)
            return -1;
    }

    /* FAT 第一扇区 */
    uint8_t fat_sec[512];
    for (int i = 0; i < 512; i++) fat_sec[i] = 0;
    uint32_t *t32 = (uint32_t *)fat_sec;

    for (int i = 0; i < 4; i++) t32[i] = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < init_blocks; i++) {
        uint32_t b = init_first + i;
        t32[b] = (i == init_blocks - 1) ? 0xFFFFFFFFu : (b + 1);
    }
    for (uint32_t i = 0; i < kbd_blocks; i++) {
        uint32_t b = kbd_first + i;
        t32[b] = (i == kbd_blocks - 1) ? 0xFFFFFFFFu : (b + 1);
    }
    for (uint32_t i = 0; i < vga_blocks; i++) {
        uint32_t b = vga_first + i;
        t32[b] = (i == vga_blocks - 1) ? 0xFFFFFFFFu : (b + 1);
    }
    for (uint32_t i = 0; i < mouse_blocks; i++) {
        uint32_t b = mouse_first + i;
        t32[b] = (i == mouse_blocks - 1) ? 0xFFFFFFFFu : (b + 1);
    }

    if (ata_write_sectors_ex(drv, NXFS_PART_LBA + 2, 1, fat_sec) < 0)
        return -1;

    /* 目录块 */
    uint8_t blk[NXFS_BLOCK_SIZE];

    for (uint32_t i = 0; i < NXFS_BLOCK_SIZE; i++) blk[i] = 0;
    set_dirent(blk, 0, "system", 2, 0, 1);
    if (write_data_block(drv, 0, blk) < 0) return -1;

    for (uint32_t i = 0; i < NXFS_BLOCK_SIZE; i++) blk[i] = 0;
    set_dirent(blk, 0, "init",  2, 0, 2);
    set_dirent(blk, 1, "drive", 2, 0, 3);
    if (write_data_block(drv, 1, blk) < 0) return -1;

    for (uint32_t i = 0; i < NXFS_BLOCK_SIZE; i++) blk[i] = 0;
    set_dirent(blk, 0, "init.elf", 1, init_size, init_first);
    if (write_data_block(drv, 2, blk) < 0) return -1;

    for (uint32_t i = 0; i < NXFS_BLOCK_SIZE; i++) blk[i] = 0;
    set_dirent(blk, 0, "kbd.elf",   1, kbd_size,   kbd_first);
    set_dirent(blk, 1, "vga.elf",   1, vga_size,   vga_first);
    set_dirent(blk, 2, "mouse.elf", 1, mouse_size, mouse_first);
    if (write_data_block(drv, 3, blk) < 0) return -1;

    /* 四个 ELF 数据 */
    struct { const uint8_t *p; uint32_t size; uint32_t first; } jobs[4] = {
        { g_init_elf_data,  init_size,  init_first  },
        { g_kbd_elf_data,   kbd_size,   kbd_first   },
        { g_vga_elf_data,   vga_size,   vga_first   },
        { g_mouse_elf_data, mouse_size, mouse_first },
    };

    for (int j = 0; j < 4; j++) {
        const uint8_t *p = jobs[j].p;
        uint32_t remaining = jobs[j].size;
        uint32_t blk_no = jobs[j].first;
        while (remaining > 0) {
            uint32_t take = remaining > NXFS_BLOCK_SIZE
                          ? NXFS_BLOCK_SIZE : remaining;
            for (uint32_t i = 0; i < NXFS_BLOCK_SIZE; i++) blk[i] = 0;
            for (uint32_t i = 0; i < take; i++) blk[i] = p[i];
            if (write_data_block(drv, blk_no, blk) < 0) return -1;
            p += take;
            remaining -= take;
            blk_no++;
        }
    }

    return 0;
}

int install_to_drive(int drive) {
    if (!installer_mode) return -100;
    if (!g_init_elf_data  || g_init_elf_size  == 0) return -1;
    if (!g_kbd_elf_data   || g_kbd_elf_size   == 0) return -1;
    if (!g_vga_elf_data   || g_vga_elf_size   == 0) return -1;
    if (!g_mouse_elf_data || g_mouse_elf_size == 0) return -1;

    if (write_stage1(drive) < 0) return -2;
    if (write_stage2(drive) < 0) return -3;
    if (write_kernel(drive) < 0) return -4;
    if (write_nxfs(drive)   < 0) return -5;

    return 0;
}