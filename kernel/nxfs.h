#ifndef NXFS_H
#define NXFS_H

#include <stdint.h>
#include "vfs.h"

#define NXFS_PART_LBA       2048  //避开 LBA 0-16（stage1 + stage2）+ 内核区，从 1MB 处开始

#define NXFS_MAGIC          0x5346584E
#define NXFS_VERSION        1
#define NXFS_BLOCK_SECTORS  8
#define NXFS_BLOCK_SIZE     (NXFS_BLOCK_SECTORS * 512)
#define NXFS_FAT_LBA        2
#define NXFS_FAT_SECTORS    64
#define NXFS_DATA_LBA       66
#define NXFS_MAX_BLOCKS     8192

#define NXFS_FREE           0x00000000
#define NXFS_EOF            0xFFFFFFFF

int          nxfs_init(void);
int          nxfs_format(void);
vfs_node_t  *nxfs_root(void);
fs_driver_t *nxfs_driver(void);

int  nxfs_read_block(uint32_t block, void *buf);
int  nxfs_write_block(uint32_t block, const void *buf);
uint32_t nxfs_alloc_block(void);
void nxfs_free_chain(uint32_t start);
uint32_t nxfs_read_chain(uint32_t start, uint32_t offset,
                         uint8_t *buf, uint32_t len);
uint32_t nxfs_write_chain(uint32_t start, uint32_t offset,
                          const uint8_t *buf, uint32_t len);

/* 新增 */
int nxfs_sync_dirent(vfs_node_t *node);   /* 把 node->size/first_block 写回目录项 */
int nxfs_commit(void);                    /* 刷 FAT + 块缓存到磁盘 */

#endif