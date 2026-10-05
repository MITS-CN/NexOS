#include "vfs.h"
#include "ramfs.h"
#include "heap.h"

/* nxfs 的内部接口（供 vfs 调用） */
extern uint32_t nxfs_read_chain(uint32_t start, uint32_t offset,
                                uint8_t *buf, uint32_t len);
extern uint32_t nxfs_write_chain(uint32_t start, uint32_t offset,
                                 const uint8_t *buf, uint32_t len);
extern uint32_t nxfs_alloc_block(void);
extern void     nxfs_free_chain(uint32_t start);

static fs_driver_t *backend = 0;
static vfs_node_t  *root_node = 0;

typedef struct {
    vfs_node_t *node;
    uint32_t    offset;
    int         flags;
    int         used;
} file_t;

static file_t fd_table[MAX_FDS];

/* --- 路径工具 --- */

static int name_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* 合法文件名：字母、数字、. _ - */
static int name_ok(const char *name) {
    if (!name || !*name) return 0;
    for (int i = 0; name[i]; i++) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-')
            continue;
        return 0;
    }
    return 1;
}

static int split_path(const char *path, char *name, const char **rest) {
    while (*path == '/') path++;
    if (*path == 0) return 0;
    int i = 0;
    while (*path && *path != '/' && i < MAX_NAME - 1)
        name[i++] = *path++;
    name[i] = 0;
    while (*path == '/') path++;
    *rest = path;
    return 1;
}

static vfs_node_t *find_child(vfs_node_t *dir, const char *name) {
    if (!dir || dir->type != VFS_DIR) return 0;
    for (vfs_node_t *c = dir->children; c; c = c->next)
        if (name_eq(c->name, name)) return c;
    return 0;
}

/* --- 对外接口 --- */

void vfs_init(void) {
    /* 初始用 ramfs 作为 fallback，等待 nxfs 挂载 */
    ramfs_init();
    root_node = ramfs_root();
    backend   = ramfs_driver();
    for (int i = 0; i < MAX_FDS; i++) fd_table[i].used = 0;
}

void vfs_use_nxfs(void) {
    extern vfs_node_t  *nxfs_root(void);
    extern fs_driver_t *nxfs_driver(void);
    root_node = nxfs_root();
    backend   = nxfs_driver();
    for (int i = 0; i < MAX_FDS; i++) fd_table[i].used = 0;
}

vfs_node_t *vfs_root(void) { return root_node; }

vfs_node_t *vfs_lookup(const char *path) {
    if (!path) return 0;
    if (*path == 0) return 0;
    if (path[0] == '/' && path[1] == 0) return root_node;

    vfs_node_t *cur = root_node;
    char name[MAX_NAME];
    const char *rest = path;

    while (split_path(rest, name, &rest)) {
        vfs_node_t *c = find_child(cur, name);
        if (!c) return 0;
        cur = c;
    }
    return cur;
}

vfs_node_t *vfs_create(const char *path, int type) {
    if (!backend || !backend->create) return 0;

    char parent_path[MAX_PATH];
    char base[MAX_NAME];

    int len = 0;
    while (path[len] && len < MAX_PATH - 1) { parent_path[len] = path[len]; len++; }
    parent_path[len] = 0;

    int last_slash = -1;
    for (int i = 0; parent_path[i]; i++)
        if (parent_path[i] == '/') last_slash = i;

    const char *base_src;
    if (last_slash < 0) {
        parent_path[0] = '/'; parent_path[1] = 0;
        base_src = path;
    } else if (last_slash == 0) {
        parent_path[1] = 0;
        base_src = path + 1;
    } else {
        parent_path[last_slash] = 0;
        base_src = path + last_slash + 1;
    }

    int i = 0;
    while (base_src[i] && i < MAX_NAME - 1) { base[i] = base_src[i]; i++; }
    base[i] = 0;
    if (i == 0) return 0;
    if (!name_ok(base)) return 0;

    vfs_node_t *parent = vfs_lookup(parent_path);
    if (!parent || parent->type != VFS_DIR) return 0;
    if (find_child(parent, base)) return 0;

    return backend->create(parent, base, type);
}

int vfs_unlink(const char *path) {
    if (!backend || !backend->unlink) return -1;
    vfs_node_t *n = vfs_lookup(path);
    if (!n || n == root_node) return -1;
    return backend->unlink(n);
}

int vfs_readdir(vfs_node_t *dir, int idx, char *out_name, int *out_type) {
    if (!dir || dir->type != VFS_DIR) return -1;
    int i = 0;
    for (vfs_node_t *c = dir->children; c; c = c->next) {
        if (i == idx) {
            int j = 0;
            while (c->name[j] && j < MAX_NAME - 1) { out_name[j] = c->name[j]; j++; }
            out_name[j] = 0;
            *out_type = c->type;
            return 0;
        }
        i++;
    }
    return -1;
}


int vfs_write(vfs_node_t *node, uint32_t offset,
              const uint8_t *data, uint32_t len) {
    if (!node || node->type != VFS_FILE) return -1;

    if (node->disk_block != NO_DISK_BLOCK) {
        /* 磁盘后端 */
        if (node->disk_block == 0xFFFFFFFFu || node->disk_block == 0) {
            uint32_t b = nxfs_alloc_block();
            if (b == 0xFFFFFFFFu) return -1; 
            node->disk_block = b;
        }
        uint32_t written = nxfs_write_chain(node->disk_block, offset, data, len);
        if (offset + written > node->size) node->size = offset + written;
        return (int)written;
    }

    /* 内存后端（ramfs） */
    uint32_t end = offset + len;
    if (end < offset) return -1;

    if (end > node->capacity) {
        uint32_t new_cap = node->capacity ? node->capacity : 256;
        while (new_cap < end) new_cap *= 2;

        uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
        if (!new_data) return -1;

        if (node->data) {
            for (uint32_t i = 0; i < node->size; i++)
                new_data[i] = node->data[i];
            kfree(node->data);
        }
        for (uint32_t i = node->size; i < new_cap; i++)
            new_data[i] = 0;

        node->data     = new_data;
        node->capacity = new_cap;
    }

    for (uint32_t i = 0; i < len; i++)
        node->data[offset + i] = data[i];

    if (end > node->size) node->size = end;
    return (int)len;
}

int vfs_read(vfs_node_t *node, uint32_t offset,
             uint8_t *buf, uint32_t len) {
    if (!node || node->type != VFS_FILE) return -1;
    if (offset >= node->size) return 0;
    if (offset + len > node->size) len = node->size - offset;

    if (node->disk_block != NO_DISK_BLOCK) {
        if (node->disk_block == 0xFFFFFFFFu) return 0;   /* 没数据 */
        return (int)nxfs_read_chain(node->disk_block, offset, buf, len);
    }

    for (uint32_t i = 0; i < len; i++)
        buf[i] = node->data[offset + i];
    return (int)len;
}

/* --- 文件描述符 --- */

int vfs_open(const char *path, int flags) {
    vfs_node_t *n = vfs_lookup(path);
    if (!n) {
        if (!(flags & O_CREAT)) return -1;
        n = vfs_create(path, VFS_FILE);
        if (!n) return -1;
    } else if (flags & O_TRUNC) {
        if (n->type != VFS_FILE) return -1;
        n->size = 0;
        if (n->disk_block != NO_DISK_BLOCK && n->disk_block != 0xFFFFFFFFu) {
            nxfs_free_chain(n->disk_block);
            n->disk_block = 0xFFFFFFFFu;
        }
        extern int nxfs_sync_dirent(vfs_node_t *);
        extern int nxfs_commit(void);
        nxfs_sync_dirent(n);
        nxfs_commit();
    }

    for (int i = 0; i < MAX_FDS; i++) {
        if (!fd_table[i].used) {
            fd_table[i].used   = 1;
            fd_table[i].node   = n;
            fd_table[i].offset = 0;
            fd_table[i].flags  = flags;
            return i;
        }
    }
    return -1;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used) return -1;
    extern int bc_flush(void);
    bc_flush();
    fd_table[fd].used = 0;
    return 0;
}

int vfs_fd_write(int fd, const uint8_t *buf, uint32_t len) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used) return -1;
    int n = vfs_write(fd_table[fd].node, fd_table[fd].offset, buf, len);
    if (n > 0) fd_table[fd].offset += n;


    extern int nxfs_sync_dirent(vfs_node_t *);
    extern int nxfs_commit(void);
    nxfs_sync_dirent(fd_table[fd].node);
    nxfs_commit();

    return n;
}

int vfs_fd_read(int fd, uint8_t *buf, uint32_t len) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used) return -1;

    int n = vfs_read(fd_table[fd].node, fd_table[fd].offset, buf, len);
    if (n > 0) fd_table[fd].offset += n;
    return n;
}

int vfs_fd_readdir(int fd, int idx, char *name, int *type) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used) return -1;
    return vfs_readdir(fd_table[fd].node, idx, name, type);
}