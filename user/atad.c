//user/atad.c —— 用户态 ATA 驱动进程
#include "syscall.h"
#include <stdint.h>

/* ATA 主通道端口 */
#define ATA_DATA       0x1F0
#define ATA_ERR        0x1F1
#define ATA_FEAT       0x1F1
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_CTRL       0x3F6

static int  shell_tid  = -1;
static int  shm_slot   = -1;   /* 只用于 debug 打印 */
static void *shm_va    = 0;

static void atad_loop(void) {
    user_msg_t m;

    for (;;) {
        if (sys_recv(&m) < 0) continue;

        if (m.type == MSG_HELLO) {
            shell_tid = m.sender;
            continue;
        }

        if (m.type == MSG_EXIT) {
            sys_exit();
        }

        /* S6c: MSG_ATA_REQ 会在这里处理 */
        /* 现在忽略 */
    }
}

int main(void) {
    /* 1. 申请 ATA 主通道 + 控制端口 */
    for (int p = 0x1F0; p <= 0x1F7; p++) {
        if (sys_io_perm(p) < 0) sys_exit();
    }
    if (sys_io_perm(ATA_CTRL) < 0) sys_exit();

    /* 2. 分配共享内存块 */
    shm_va = sys_shm_alloc();
    if (!shm_va) sys_exit();

    /* 3. IPC 循环 */
    atad_loop();

    sys_exit();
    return 0;
}