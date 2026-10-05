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


/* 特殊值 */
#define NO_DISK_BLOCK  0x80000000u   //表示这个节点没有磁盘块（ramfs 内存文件）

typedef struct vfs_node {
    char             name[MAX_NAME];
    int              type;
    uint32_t         size;
    uint8_t         *data;
    uint32_t         capacity;
    uint32_t         disk_block;
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

#endif