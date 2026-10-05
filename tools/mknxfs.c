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
    uint8_t  type;
    uint8_t  reserved[3];
    uint32_t size;
    uint32_t first_block;
} __attribute__((packed)) nxfs_dirent_t;

static FILE          *img;
static uint32_t       fat[NXFS_MAX_BLOCKS];
static nxfs_dirent_t  root_dir[NXFS_BLOCK_SIZE / sizeof(nxfs_dirent_t)];

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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <output.img> [hostfile:destname ...]\n", argv[0]);
        return 1;
    }

    img = fopen(argv[1], "wb+");
    if (!img) die("fopen");

    if (fseek(img, 32 * 1024 * 1024 - 1, SEEK_SET) != 0) die("fseek");
    if (fputc(0, img) == EOF) die("fputc");

    for (int i = 0; i < NXFS_MAX_BLOCKS; i++) fat[i] = NXFS_FREE;

    uint32_t root_block = 0;
    fat[root_block] = NXFS_EOF;
    memset(root_dir, 0, sizeof(root_dir));

    int root_slot = 0;

    for (int a = 2; a < argc; a++) {
        char *spec = argv[a];
        char *colon = strchr(spec, ':');
        if (!colon) {
            fprintf(stderr, "mknxfs: bad spec '%s'\n", spec);
            return 1;
        }
        *colon = 0;
        const char *hostpath = spec;
        const char *destname = colon + 1;

        if (strlen(destname) >= 24) {
            fprintf(stderr, "mknxfs: name too long '%s'\n", destname);
            return 1;
        }

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

        nxfs_dirent_t *e = &root_dir[root_slot++];
        memset(e, 0, sizeof(*e));
        strncpy(e->name, destname, 23);
        e->type = 1;
        e->size = (uint32_t)sz;
        e->first_block = first;

        printf("mknxfs: %s -> /%s (%ld bytes, first_block=%u)\n",
               hostpath, destname, sz, first);

        if (root_slot >= (int)(NXFS_BLOCK_SIZE / sizeof(nxfs_dirent_t))) {
            fprintf(stderr, "mknxfs: root dir full\n");
            return 1;
        }
    }

    write_block(root_block, root_dir);
    write_at((uint64_t)2 * 512, fat, NXFS_FAT_SECTORS * 512);

    nxfs_super_t super;
    memset(&super, 0, sizeof(super));
    super.magic         = NXFS_MAGIC;
    super.version       = NXFS_VERSION;
    super.block_sectors = NXFS_BLOCK_SECTORS;
    super.total_blocks  = NXFS_MAX_BLOCKS;
    super.fat_lba       = 2;
    super.fat_sectors   = NXFS_FAT_SECTORS;
    super.data_lba      = NXFS_DATA_LBA;
    super.root_block    = root_block;
    write_at((uint64_t)1 * 512, &super, 512);

    fclose(img);
    printf("mknxfs: wrote %s (32MB)\n", argv[1]);
    return 0;
}