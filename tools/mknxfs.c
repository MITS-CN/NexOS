#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define NXFS_MAGIC          0x5346584E
#define NXFS_VERSION        1
#define NXFS_BLOCK_SECTORS  8
#define NXFS_BLOCK_SIZE     (NXFS_BLOCK_SECTORS * 512)
#define NXFS_FAT_SECTORS    64
#define NXFS_DATA_LBA       66
#define NXFS_MAX_BLOCKS     8192
#define NXFS_FREE           0x00000000u
#define NXFS_EOF            0xFFFFFFFFu

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

typedef struct {
    char     name[24];
    uint8_t  type;      /* 1=file, 2=dir */
    uint8_t  reserved[3];
    uint32_t size;
    uint32_t first_block;
} __attribute__((packed)) nxfs_dirent_t;

#define DIRENTS_PER_BLOCK (NXFS_BLOCK_SIZE / sizeof(nxfs_dirent_t))

static FILE          *img;
static uint32_t       fat[NXFS_MAX_BLOCKS];

/* 内存中的目录块缓存 */
typedef struct {
    uint32_t       block;
    nxfs_dirent_t  ents[DIRENTS_PER_BLOCK];
    int            used;
} dbuf_t;

static dbuf_t  dbufs[32];
static int     dbuf_count = 0;

static void die(const char *m) { perror(m); exit(1); }

static void write_at(uint64_t byte_off, const void *buf, size_t len) {
    if (fseek(img, byte_off, SEEK_SET) != 0) die("fseek");
    if (fwrite(buf, 1, len, img) != len) die("fwrite");
}

static uint32_t alloc_block(void) {
    for (uint32_t i = 0; i < NXFS_MAX_BLOCKS; i++) {
        if (fat[i] == NXFS_FREE) {
            fat[i] = NXFS_EOF;
            return i;
        }
    }
    fprintf(stderr, "mknxfs: out of blocks\n");
    exit(1);
}

static void write_block(uint32_t block, const void *data) {
    uint64_t off = ((uint64_t)NXFS_DATA_LBA +
                    (uint64_t)block * NXFS_BLOCK_SECTORS) * 512;
    write_at(off, data, NXFS_BLOCK_SIZE);
}

static uint32_t write_file_chain(const void *data, uint32_t size) {
    if (size == 0) return NXFS_EOF;

    uint32_t first = alloc_block();
    uint32_t cur = first;
    uint32_t done = 0;

    uint8_t buf[NXFS_BLOCK_SIZE];
    while (done < size) {
        uint32_t take = size - done;
        if (take > NXFS_BLOCK_SIZE) take = NXFS_BLOCK_SIZE;
        memset(buf, 0, NXFS_BLOCK_SIZE);
        memcpy(buf, (const uint8_t *)data + done, take);
        write_block(cur, buf);
        done += take;
        if (done < size) {
            uint32_t nxt = alloc_block();
            fat[cur] = nxt;
            cur = nxt;
        }
    }
    return first;
}

/* ---- 目录块缓存 ---- */

static dbuf_t *dbuf_get(uint32_t block) {
    for (int i = 0; i < dbuf_count; i++) {
        if (dbufs[i].used && dbufs[i].block == block) return &dbufs[i];
    }
    return NULL;
}

static dbuf_t *dbuf_new(uint32_t block) {
    if (dbuf_count >= 32) {
        fprintf(stderr, "mknxfs: too many directories\n");
        exit(1);
    }
    dbuf_t *d = &dbufs[dbuf_count++];
    d->block = block;
    d->used = 1;
    memset(d->ents, 0, sizeof(d->ents));
    return d;
}

/* 在 dir 里找个空槽加一个项，返回 0 成功 */
static int dir_add_entry(dbuf_t *dir, const char *name, uint8_t type,
                         uint32_t size, uint32_t first_block) {
    for (int i = 0; i < DIRENTS_PER_BLOCK; i++) {
        if (dir->ents[i].type == 0) {
            memset(&dir->ents[i], 0, sizeof(nxfs_dirent_t));
            strncpy(dir->ents[i].name, name, 23);
            dir->ents[i].type = type;
            dir->ents[i].size = size;
            dir->ents[i].first_block = first_block;
            return 0;
        }
    }
    return -1;
}

/* 在 dir 里找 name，找到返回槽索引，找不到返回 -1 */
static int dir_find(dbuf_t *dir, const char *name) {
    for (int i = 0; i < DIRENTS_PER_BLOCK; i++) {
        if (dir->ents[i].type == 0) continue;
        if (strncmp(dir->ents[i].name, name, 24) == 0) return i;
    }
    return -1;
}

/* 走路径（如 "system/init"），create=1 时自动创建中间目录。
   返回目标目录的块号，失败返回 NXFS_EOF。 */
static uint32_t walk_dir(const char *path, int create) {
    uint32_t cur_block = 0;
    dbuf_t  *cur_dir = dbuf_get(0);
    if (!cur_dir) cur_dir = dbuf_new(0);

    if (!path || !*path) return 0;

    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        char seg[24];
        int n = 0;
        while (*p && *p != '/' && n < 23) seg[n++] = *p++;
        seg[n] = 0;

        int idx = dir_find(cur_dir, seg);
        if (idx >= 0) {
            if (cur_dir->ents[idx].type != 2) {
                /* 是文件，不能往下走 */
                return NXFS_EOF;
            }
            cur_block = cur_dir->ents[idx].first_block;
            cur_dir = dbuf_get(cur_block);
            if (!cur_dir) cur_dir = dbuf_new(cur_block);
        } else {
            if (!create) return NXFS_EOF;
            uint32_t nb = alloc_block();
            dbuf_new(nb);
            if (dir_add_entry(cur_dir, seg, 2, 0, nb) < 0) return NXFS_EOF;
            cur_block = nb;
            cur_dir = dbuf_get(nb);
        }
    }
    return cur_block;
}

/* 处理一个 spec "hostpath:destpath"
   destpath 如 "system/init/init.elf" 或 "init.elf" */
static void process_spec(const char *spec) {
    char buf[512];
    if (strlen(spec) >= 512) {
        fprintf(stderr, "mknxfs: spec too long\n");
        exit(1);
    }
    strcpy(buf, spec);

    char *colon = strchr(buf, ':');
    if (!colon) {
        fprintf(stderr, "mknxfs: bad spec '%s'\n", spec);
        return;
    }
    *colon = 0;
    const char *hostpath = buf;
    const char *destpath = colon + 1;

    /* 解析 destpath：拆成父目录 + 文件名 */
    char parent[256];
    char fname[24];
    int last_slash = -1;
    int plen = strlen(destpath);
    for (int i = 0; i < plen; i++) {
        if (destpath[i] == '/') last_slash = i;
    }

    if (last_slash < 0) {
        parent[0] = 0;
        strncpy(fname, destpath, 23);
        fname[23] = 0;
    } else {
        if (last_slash >= 255) {
            fprintf(stderr, "mknxfs: parent path too long\n");
            exit(1);
        }
        memcpy(parent, destpath, last_slash);
        parent[last_slash] = 0;
        strncpy(fname, destpath + last_slash + 1, 23);
        fname[23] = 0;
    }

    if (strlen(fname) == 0) {
        fprintf(stderr, "mknxfs: empty filename in '%s'\n", destpath);
        exit(1);
    }

    /* 找/创建父目录 */
    uint32_t parent_block = walk_dir(parent, 1);
    if (parent_block == NXFS_EOF) {
        fprintf(stderr, "mknxfs: cannot create path '%s'\n", parent);
        exit(1);
    }
    dbuf_t *pdir = dbuf_get(parent_block);
    if (!pdir) pdir = dbuf_new(parent_block);

    if (dir_find(pdir, fname) >= 0) {
        fprintf(stderr, "mknxfs: '%s' already exists\n", destpath);
        exit(1);
    }

    /* 读 host 文件 */
    FILE *f = fopen(hostpath, "rb");
    if (!f) die(hostpath);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = malloc(sz ? sz : 1);
    if (!data) die("malloc");
    if (sz > 0 && fread(data, 1, sz, f) != (size_t)sz) die("fread");
    fclose(f);

    uint32_t first = write_file_chain(data, (uint32_t)sz);
    free(data);

    if (dir_add_entry(pdir, fname, 1, (uint32_t)sz, first) < 0) {
        fprintf(stderr, "mknxfs: dir full for '%s'\n", destpath);
        exit(1);
    }

    printf("mknxfs: %s -> /%s (%ld bytes, first_block=%u)\n",
           hostpath, destpath, sz, first);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <output.img> [hostfile:destpath ...]\n", argv[0]);
        return 1;
    }

    img = fopen(argv[1], "wb+");
    if (!img) die("fopen");

    if (fseek(img, 32 * 1024 * 1024 - 1, SEEK_SET) != 0) die("fseek");
    if (fputc(0, img) == EOF) die("fputc");

    for (int i = 0; i < NXFS_MAX_BLOCKS; i++) fat[i] = NXFS_FREE;

    /* 块 0 = 根目录 */
    fat[0] = NXFS_EOF;
    dbuf_new(0);

    for (int a = 2; a < argc; a++) {
        process_spec(argv[a]);
    }

    /* 所有目录块写回 */
    for (int i = 0; i < dbuf_count; i++) {
        if (dbufs[i].used)
            write_block(dbufs[i].block, dbufs[i].ents);
    }

    /* FAT 写回 */
    write_at((uint64_t)2 * 512, fat, NXFS_FAT_SECTORS * 512);

    /* 超级块 */
    nxfs_super_t super;
    memset(&super, 0, sizeof(super));
    super.magic         = NXFS_MAGIC;
    super.version       = NXFS_VERSION;
    super.block_sectors = NXFS_BLOCK_SECTORS;
    super.total_blocks  = NXFS_MAX_BLOCKS;
    super.fat_lba       = 2;
    super.fat_sectors   = NXFS_FAT_SECTORS;
    super.data_lba      = NXFS_DATA_LBA;
    super.root_block    = 0;
    write_at((uint64_t)1 * 512, &super, 512);

    fclose(img);
    printf("mknxfs: wrote %s (32MB)\n", argv[1]);
    return 0;
}