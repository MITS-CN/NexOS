#ifndef VFS_H
#define VFS_H

#include <stdint.h>

#define VFS_FILE 1
#define VFS_DIR  2

#define MAX_FDS  16
#define MAX_PATH 256
#define MAX_NAME 64

#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0100
#define O_TRUNC   0x0200

#define NO_DISK_BLOCK  0x80000000u

/* ★ S7c: 权限位（低 9 位）
   bit 0-2: other  r/w/x
   bit 3-5: group  r/w/x
   bit 6-8: owner  r/w/x
   bit 9-15: 保留（setuid/setgid/sticky 等）—— 目前永不设、不检查 */
#define VFS_X_OTH   0001
#define VFS_W_OTH   0002
#define VFS_R_OTH   0004
#define VFS_X_GRP   0010
#define VFS_W_GRP   0020
#define VFS_R_GRP   0040
#define VFS_X_USR   0100
#define VFS_W_USR   0200
#define VFS_R_USR   0400

#define VFS_MODE_MASK  0777

/* 默认权限 */
#define VFS_DEF_FILE_MODE  0644
#define VFS_DEF_DIR_MODE   0755

struct vfs_node;

typedef int (*vfs_read_fn)(struct vfs_node *node, uint32_t offset,
                           uint8_t *buf, uint32_t len);

typedef struct vfs_node {
    char             name[MAX_NAME];
    int              type;
    uint32_t         size;
    uint8_t         *data;
    uint32_t         capacity;
    uint32_t         disk_block;
    vfs_read_fn      read_fn;
    int              is_dynamic;
    /* ★ S7c: 权限 */
    uint16_t         mode;
    uint16_t         uid;
    uint16_t         gid;
    struct vfs_node *parent;
    struct vfs_node *children;
    struct vfs_node *next;
} vfs_node_t;

typedef struct {
    const char *name;
    int  (*init)(void);
    vfs_node_t *(*create)(vfs_node_t *parent, const char *name, int type);
    int  (*unlink)(vfs_node_t *node);
} fs_driver_t;

void        vfs_init(void);
void        vfs_use_nxfs(void);
vfs_node_t *vfs_root(void);
vfs_node_t *vfs_lookup(const char *path);
vfs_node_t *vfs_create(const char *path, int type);
int         vfs_unlink(const char *path);
int         vfs_readdir(vfs_node_t *dir, int idx, char *out_name, int *out_type);
int         vfs_write(vfs_node_t *node, uint32_t offset,
                      const uint8_t *data, uint32_t len);
int         vfs_read(vfs_node_t *node, uint32_t offset,
                     uint8_t *buf, uint32_t len);

int  vfs_open(const char *path, int flags);
int  vfs_close(int fd);
int  vfs_fd_read(int fd, uint8_t *buf, uint32_t len);
int  vfs_fd_write(int fd, const uint8_t *buf, uint32_t len);
int  vfs_fd_readdir(int fd, int idx, char *name, int *type);

uint32_t vfs_fd_size(int fd);

/* ★ S7c: 权限检查 + 修改 */
int  vfs_has_perm(vfs_node_t *n, int need);
int  vfs_chmod(vfs_node_t *n, uint16_t mode);
int  vfs_chown(vfs_node_t *n, uint16_t uid);

#endif