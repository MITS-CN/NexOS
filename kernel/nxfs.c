#include "nxfs.h"
#include "ata.h"
#include "block_cache.h"   /* ★ 加这行 */
#include "heap.h"

/* 超级块 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_sectors;
    uint32_t total_blocks;
    uint32_t fat_lba;
    uint32_t fat_sectors;
    uint32_t data_lba;
    uint32_t root_block;
    uint8_t  reserved[512 - 32];
} __attribute__((packed)) nxfs_super_t;

/* 目录项 32 字节 */
typedef struct {
    char     name[24];
    uint8_t  type;
    uint8_t  reserved[3];
    uint32_t size;
    uint32_t first_block;
} __attribute__((packed)) nxfs_dirent_t;

static nxfs_super_t super;
static uint32_t    *fat_cache = 0;
static vfs_node_t  *root = 0;
static int          mounted = 0;

/* ---- 块读写 ---- */

int nxfs_read_block(uint32_t block, void *buf) {
    if (block >= super.total_blocks) return -1;
    uint32_t lba = NXFS_PART_LBA + super.data_lba
                 + block * super.block_sectors;
    return ata_read_sectors(lba, (uint8_t)super.block_sectors, buf);
}

int nxfs_write_block(uint32_t block, const void *buf) {
    if (block >= super.total_blocks) return -1;
    uint32_t lba = NXFS_PART_LBA + super.data_lba
                 + block * super.block_sectors;
    return ata_write_sectors(lba, (uint8_t)super.block_sectors, buf);
}

static void free_children_recursive(vfs_node_t *n) {
    vfs_node_t *c = n->children;
    while (c) {
        vfs_node_t *next = c->next;
        free_children_recursive(c);
        kfree(c);
        c = next;
    }
    n->children = 0;
}

/* ---- FAT ---- */

static int fat_flush(void) {
    uint32_t total_bytes = super.fat_sectors * 512;
    uint32_t done = 0;
    uint8_t *p = (uint8_t *)fat_cache;

    while (done < total_bytes) {
        uint32_t left_sectors = (total_bytes - done) / 512;
        if (left_sectors == 0) left_sectors = 1;
        if (left_sectors > 128) left_sectors = 128;
        if (ata_write_sectors(NXFS_PART_LBA + super.fat_lba + done / 512,
                              (uint8_t)left_sectors, p + done) < 0)
            return -1;
        done += left_sectors * 512;
    }
    return 0;
}

static int fat_load(void) {
    uint32_t done = 0;
    while (done < super.fat_sectors) {
        uint32_t cnt = super.fat_sectors - done;
        if (cnt > 128) cnt = 128;
        if (ata_read_sectors(NXFS_PART_LBA + super.fat_lba + done,
                             (uint8_t)cnt,
                             (uint8_t *)fat_cache + done * 512) < 0)
            return -1;
        done += cnt;
    }
    return 0;
}

uint32_t nxfs_alloc_block(void) {
    for (uint32_t i = 0; i < super.total_blocks; i++) {
        if (fat_cache[i] == NXFS_FREE) {
            fat_cache[i] = NXFS_EOF;
            return i;
        }
    }
    return NXFS_EOF;
}

void nxfs_free_chain(uint32_t start) {
    uint32_t guard = 0;
    while (start != NXFS_EOF && start < super.total_blocks) {
        uint32_t next = fat_cache[start];
        fat_cache[start] = NXFS_FREE;
        if (next == NXFS_EOF) break;
        start = next;
        if (++guard > super.total_blocks) break;
    }
}

uint32_t nxfs_read_chain(uint32_t start, uint32_t offset,
                         uint8_t *buf, uint32_t len) {
    if (start == NXFS_EOF || start >= super.total_blocks) return 0;

    uint8_t block_buf[NXFS_BLOCK_SIZE];

    uint32_t skip  = offset / NXFS_BLOCK_SIZE;
    uint32_t inner = offset % NXFS_BLOCK_SIZE;
    uint32_t cur = start;
    uint32_t guard = 0;
    while (skip > 0 && cur != NXFS_EOF) {
        cur = fat_cache[cur];
        skip--;
        if (++guard > super.total_blocks) return 0;
    }
    if (cur == NXFS_EOF || cur >= super.total_blocks) return 0;

    uint32_t done = 0;
    while (done < len && cur != NXFS_EOF) {
        if (nxfs_read_block(cur, block_buf) < 0) break;

        uint32_t avail = NXFS_BLOCK_SIZE - inner;
        uint32_t take = len - done;
        if (take > avail) take = avail;

        for (uint32_t i = 0; i < take; i++)
            buf[done + i] = block_buf[inner + i];

        done += take;
        inner = 0;
        cur = fat_cache[cur];
        if (++guard > super.total_blocks) break;
    }
    return done;
}

uint32_t nxfs_write_chain(uint32_t start, uint32_t offset,
                          const uint8_t *buf, uint32_t len) {
    uint8_t block_buf[NXFS_BLOCK_SIZE];

    if (start == NXFS_EOF || start == 0) {
        if (start == 0 || start == NXFS_EOF) {
            uint32_t b = nxfs_alloc_block();
            if (b == NXFS_EOF) return 0;
            start = b;
        }
    }

    uint32_t skip  = offset / NXFS_BLOCK_SIZE;
    uint32_t inner = offset % NXFS_BLOCK_SIZE;
    uint32_t cur = start;
    uint32_t prev = NXFS_EOF;
    uint32_t guard = 0;

    while (skip > 0) {
        prev = cur;
        cur = fat_cache[cur];
        if (cur == NXFS_EOF) {
            uint32_t nb = nxfs_alloc_block();
            if (nb == NXFS_EOF) return 0;
            fat_cache[prev] = nb;
            cur = nb;
        }
        skip--;
        if (++guard > super.total_blocks) return 0;
    }

    uint32_t done = 0;
    while (done < len) {
        if (nxfs_read_block(cur, block_buf) < 0) break;

        uint32_t avail = NXFS_BLOCK_SIZE - inner;
        uint32_t take = len - done;
        if (take > avail) take = avail;

        for (uint32_t i = 0; i < take; i++)
            block_buf[inner + i] = buf[done + i];

        if (nxfs_write_block(cur, block_buf) < 0) break;

        done += take;
        inner = 0;

        if (done < len) {
            uint32_t next = fat_cache[cur];
            if (next == NXFS_EOF) {
                uint32_t nb = nxfs_alloc_block();
                if (nb == NXFS_EOF) break;
                fat_cache[cur] = nb;
                next = nb;
            }
            cur = next;
        }
        if (++guard > super.total_blocks) break;
    }
    return done;
}

/* ---- 目录项 ---- */

static int name_match(const char *a, const char *b) {
    int i = 0;
    while (1) {
        if (a[i] != b[i]) return 0;
        if (a[i] == 0) return 1;
        i++;
        if (i >= 24) return 1;
    }
}

static int dir_find_entry(uint32_t dir_block, int idx,
                          nxfs_dirent_t *out) {
    uint8_t block_buf[NXFS_BLOCK_SIZE];
    uint32_t cur = dir_block;
    int count = 0;
    uint32_t guard = 0;

    while (cur != NXFS_EOF && cur < super.total_blocks) {
        if (nxfs_read_block(cur, block_buf) < 0) return -1;

        nxfs_dirent_t *ents = (nxfs_dirent_t *)block_buf;
        int per_block = NXFS_BLOCK_SIZE / sizeof(nxfs_dirent_t);
        for (int i = 0; i < per_block; i++) {
            if (ents[i].type == 0) continue;
            if (count == idx) { *out = ents[i]; return 0; }
            count++;
        }
        cur = fat_cache[cur];
        if (++guard > super.total_blocks) break;
    }
    return -1;
}

static int dir_insert(uint32_t dir_block, const nxfs_dirent_t *ent) {

    static uint8_t block_buf[NXFS_BLOCK_SIZE];
    uint32_t cur = dir_block;
    uint32_t prev = NXFS_EOF;
    uint32_t guard = 0;

    while (cur != NXFS_EOF && cur < super.total_blocks) {
        if (nxfs_read_block(cur, block_buf) < 0) return -1;
        nxfs_dirent_t *ents = (nxfs_dirent_t *)block_buf;
        int per_block = NXFS_BLOCK_SIZE / sizeof(nxfs_dirent_t);
        for (int i = 0; i < per_block; i++) {
            if (ents[i].type == 0) {
                ents[i] = *ent;
                return nxfs_write_block(cur, block_buf);
            }
        }
        prev = cur;
        cur = fat_cache[cur];
        if (++guard > super.total_blocks) break;
    }

    if (prev == NXFS_EOF) return -1;

    uint32_t nb = nxfs_alloc_block();
    if (nb == NXFS_EOF) return -1;
    fat_cache[prev] = nb;

    /* 用 block_buf 作为清零缓冲区，避免额外 4KB 栈 */
    for (uint32_t i = 0; i < NXFS_BLOCK_SIZE; i++) block_buf[i] = 0;
    nxfs_dirent_t *ents = (nxfs_dirent_t *)block_buf;
    ents[0] = *ent;
    return nxfs_write_block(nb, block_buf);
}

static int dir_remove(uint32_t dir_block, const char *name) {
    uint8_t block_buf[NXFS_BLOCK_SIZE];
    uint32_t cur = dir_block;
    uint32_t guard = 0;

    while (cur != NXFS_EOF && cur < super.total_blocks) {
        if (nxfs_read_block(cur, block_buf) < 0) return -1;

        nxfs_dirent_t *ents = (nxfs_dirent_t *)block_buf;
        int per_block = NXFS_BLOCK_SIZE / sizeof(nxfs_dirent_t);
        for (int i = 0; i < per_block; i++) {
            if (ents[i].type == 0) continue;
            if (name_match(ents[i].name, name)) {
                ents[i].type = 0;
                return nxfs_write_block(cur, block_buf);
            }
        }
        cur = fat_cache[cur];
        if (++guard > super.total_blocks) break;
    }
    return -1;
}

/* ---- 内存树构建 ---- */

static vfs_node_t *node_alloc(const char *name, int type,
                              uint32_t disk_block, uint32_t size) {
    vfs_node_t *n = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!n) return 0;

    int i = 0;
    while (name[i] && i < MAX_NAME - 1) { n->name[i] = name[i]; i++; }
    n->name[i] = 0;

    n->type       = type;
    n->size       = size;
    n->data       = 0;
    n->capacity   = 0;
    n->disk_block = disk_block;
    n->parent     = 0;
    n->children   = 0;
    n->next       = 0;
    return n;
}

static void build_tree(vfs_node_t *dir_node, uint32_t dir_block) {
    for (int i = 0; ; i++) {
        nxfs_dirent_t ent;
        if (dir_find_entry(dir_block, i, &ent) < 0) break;

        int type = (ent.type == 2) ? VFS_DIR : VFS_FILE;
        vfs_node_t *child = node_alloc(ent.name, type,
                                       ent.first_block, ent.size);
        if (!child) continue;

        child->parent = dir_node;

        if (!dir_node->children) {
            dir_node->children = child;
        } else {
            vfs_node_t *c = dir_node->children;
            while (c->next) c = c->next;
            c->next = child;
        }

        if (type == VFS_DIR)
            build_tree(child, ent.first_block);
    }
}

/* ---- 后端接口 ---- */

static vfs_node_t *nxfs_create(vfs_node_t *parent, const char *name, int type) {
    if (!parent || parent->type != VFS_DIR) {
        return 0;
    }

    uint32_t first_block = 0xFFFFFFFFu;

    if (type == VFS_DIR) {
        first_block = nxfs_alloc_block();
        if (first_block == NXFS_EOF) {
            return 0;
        }
                static uint8_t zero[NXFS_BLOCK_SIZE];
        for (uint32_t i = 0; i < NXFS_BLOCK_SIZE; i++) zero[i] = 0;
        if (nxfs_write_block(first_block, zero) < 0) {
            return 0;
        }
    }

    nxfs_dirent_t ent;
    for (int i = 0; i < 24; i++) ent.name[i] = 0;
    int i = 0;
    while (name[i] && i < 23) { ent.name[i] = name[i]; i++; }
    ent.type = (type == VFS_DIR) ? 2 : 1;
    ent.reserved[0] = ent.reserved[1] = ent.reserved[2] = 0;
    ent.size = 0;
    ent.first_block = first_block;

    if (dir_insert(parent->disk_block, &ent) < 0) {
        return 0;
    }
    vfs_node_t *n = node_alloc(name, type, first_block, 0);
    if (!n) return 0;
    n->parent = parent;

    if (!parent->children) {
        parent->children = n;
    } else {
        vfs_node_t *c = parent->children;
        while (c->next) c = c->next;
        c->next = n;
    }

    fat_flush();
    return n;

}

static int nxfs_unlink(vfs_node_t *node) {
    if (!node || !node->parent) return -1;

    /* ★ 目录是否为空：查磁盘，不查内存 */
    if (node->type == VFS_DIR) {
        nxfs_dirent_t ent;
        if (dir_find_entry(node->disk_block, 0, &ent) == 0)
            return -1;   /* 磁盘上还有有效项，拒绝 */
    }

    dir_remove(node->parent->disk_block, node->name);

    if (node->disk_block != 0xFFFFFFFFu && node->disk_block != 0)
        nxfs_free_chain(node->disk_block);

    vfs_node_t *parent = node->parent;
    if (parent->children == node) {
        parent->children = node->next;
    } else {
        vfs_node_t *c = parent->children;
        while (c && c->next != node) c = c->next;
        if (!c) return -1;
        c->next = node->next;
    }

    fat_flush();
    free_children_recursive(node);
    kfree(node);
    return 0;
}

static fs_driver_t nxfs_drv = {
    .name   = "nxfs",
    .init   = 0,
    .create = nxfs_create,
    .unlink = nxfs_unlink,
};

/* ---- 挂载 / 格式化 ---- */

int nxfs_format(void) {
    super.magic         = NXFS_MAGIC;
    super.version       = NXFS_VERSION;
    super.block_sectors = NXFS_BLOCK_SECTORS;
    super.total_blocks  = NXFS_MAX_BLOCKS;
    super.fat_lba       = NXFS_FAT_LBA;
    super.fat_sectors   = NXFS_FAT_SECTORS;
    super.data_lba      = NXFS_DATA_LBA;
    super.root_block    = 0;

    uint8_t sb_buf[512];
    for (int i = 0; i < 512; i++) sb_buf[i] = 0;
    for (uint32_t i = 0; i < sizeof(super); i++)
        sb_buf[i] = ((uint8_t *)&super)[i];
    if (ata_write_sectors(NXFS_PART_LBA + 1, 1, sb_buf) < 0) return -1;

    for (uint32_t i = 0; i < super.total_blocks; i++)
        fat_cache[i] = NXFS_FREE;
    fat_cache[0] = NXFS_EOF;

    uint8_t zero[NXFS_BLOCK_SIZE];
    for (uint32_t i = 0; i < NXFS_BLOCK_SIZE; i++) zero[i] = 0;
    if (nxfs_write_block(0, zero) < 0) return -2;

    if (fat_flush() < 0) return -3;
    return 0;
}

int nxfs_init(void) {
    extern void vga_puts(const char *);
    extern void vga_hex(uint32_t);
    extern int  kbd_confirm(const char *);

    uint8_t sb_buf[512];
    int rr = ata_read_sectors(NXFS_PART_LBA + 1, 1, sb_buf);
    if (rr < 0) return -1;

    for (uint32_t i = 0; i < sizeof(super); i++)
        ((uint8_t *)&super)[i] = sb_buf[i];

    int is_valid_nxfs = 1;
    if (super.magic != NXFS_MAGIC)     is_valid_nxfs = 0;
    if (super.version != NXFS_VERSION) is_valid_nxfs = 0;
    if (super.total_blocks == 0 || super.total_blocks > 65536)
        is_valid_nxfs = 0;

    int is_blank = 1;
    for (int i = 0; i < 512; i++) {
        if (sb_buf[i] != 0) { is_blank = 0; break; }
    }

    int need_format = 0;

    if (is_valid_nxfs) {
        vga_puts("  [nxfs] valid superblock, mounting\n");
    } else if (is_blank) {
        vga_puts("  [nxfs] blank disk detected.\n");
        if (!kbd_confirm("  Format as NXFS?")) {
            vga_puts("  [nxfs] declined, halting.\n");
            return -101;
        }
        super.magic         = NXFS_MAGIC;
        super.version       = NXFS_VERSION;
        super.block_sectors = NXFS_BLOCK_SECTORS;
        super.total_blocks  = NXFS_MAX_BLOCKS;
        super.fat_lba       = NXFS_FAT_LBA;
        super.fat_sectors   = NXFS_FAT_SECTORS;
        super.data_lba      = NXFS_DATA_LBA;
        super.root_block    = 0;
        need_format = 1;
    } else {
        vga_puts("  [nxfs] WARNING: non-NXFS data on disk.\n");
        vga_puts("  [nxfs] magic=");
        vga_hex(super.magic);
        vga_puts(" expected=");
        vga_hex(NXFS_MAGIC);
        vga_puts("\n  [nxfs] ALL DATA WILL BE DESTROYED.\n");
        if (!kbd_confirm("  Format anyway?")) {
            vga_puts("  [nxfs] declined, halting.\n");
            return -101;
        }
        super.magic         = NXFS_MAGIC;
        super.version       = NXFS_VERSION;
        super.block_sectors = NXFS_BLOCK_SECTORS;
        super.total_blocks  = NXFS_MAX_BLOCKS;
        super.fat_lba       = NXFS_FAT_LBA;
        super.fat_sectors   = NXFS_FAT_SECTORS;
        super.data_lba      = NXFS_DATA_LBA;
        super.root_block    = 0;
        need_format = 1;
    }

    fat_cache = (uint32_t *)kmalloc(super.total_blocks * 4);
    if (!fat_cache) { vga_puts("  [nxfs] kmalloc failed\n"); return -2; }

    bc_init(NXFS_PART_LBA + super.data_lba);
    if (!bc_ready()) { vga_puts("  [nxfs] bc not ready\n"); return -6; }

    if (need_format) {
        vga_puts("  [nxfs] formatting...\n");
        if (nxfs_format() < 0) { vga_puts("  [nxfs] format failed\n"); return -3; }
    } else {
        if (fat_load() < 0) { vga_puts("  [nxfs] fat_load failed\n"); return -4; }
    }

    root = node_alloc("/", VFS_DIR, super.root_block, 0);
    if (!root) { vga_puts("  [nxfs] root alloc failed\n"); return -5; }
    build_tree(root, super.root_block);

    vga_puts("  [nxfs] mounted\n");
    mounted = 1;
    return 0;
}

/* 把 node->size 和 node->disk_block 同步回磁盘的目录项 */
int nxfs_sync_dirent(vfs_node_t *node) {
    if (!node || !node->parent) return -1;

    uint8_t block_buf[NXFS_BLOCK_SIZE];
    uint32_t cur = node->parent->disk_block;
    uint32_t guard = 0;

    while (cur != NXFS_EOF && cur < super.total_blocks) {
        if (nxfs_read_block(cur, block_buf) < 0) return -1;

        nxfs_dirent_t *ents = (nxfs_dirent_t *)block_buf;
        int per_block = NXFS_BLOCK_SIZE / sizeof(nxfs_dirent_t);
        for (int i = 0; i < per_block; i++) {
            if (ents[i].type == 0) continue;
            if (name_match(ents[i].name, node->name)) {
                ents[i].size        = node->size;
                ents[i].first_block = node->disk_block;
                return nxfs_write_block(cur, block_buf);
            }
        }
        cur = fat_cache[cur];
        if (++guard > super.total_blocks) break;
    }
    return -1;
}

/* 刷所有脏数据到磁盘 */
int nxfs_commit(void) {
    int r1 = bc_flush();
    int r2 = fat_flush();
    return (r1 < 0 || r2 < 0) ? -1 : 0;
}

vfs_node_t  *nxfs_root(void)   { return root; }
fs_driver_t *nxfs_driver(void) { return &nxfs_drv; }