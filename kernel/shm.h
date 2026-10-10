#ifndef SHM_H
#define SHM_H

#include <stdint.h>

#define SHM_SLOTS       32
#define SHM_SIZE        4096
#define SHM_USER_BASE   0x20000000u

/* 延迟初始化；首次 SYS_SHM_ALLOC 自动触发 */
void      shm_init(void);

/* 分配一块共享内存给 owner_tid，返回 slot 索引；-1 = 满了 */
int       shm_alloc_for(int owner_tid);

/* 释放该 owner 的所有块（线程退出时调用） */
void      shm_free_owner(int owner_tid);

/* 取 slot 的物理页地址；0 = 无效 */
uint32_t  shm_get_phys(int slot);

/* 取 slot 的当前 owner tid；-1 = 无 */
int       shm_get_owner(int slot);

#endif