#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include "thread.h"

#define IRQ_MAX   16
#define MSG_IRQ   0x100

/* 注册：把 irq 绑定到 tid。返回 0 成功，<0 失败 */
int  irq_register(int irq, int tid);
/* 解绑：只有 owner 能解 */
int  irq_unregister(int irq, int tid);
/* 查询当前 owner tid，-1 表示未注册 */
int  irq_owner(int irq);
/* 转发一次：把 scancode 通过 IPC 发给 owner */
void irq_dispatch(int irq, uint32_t scancode);
/* 线程死亡时清掉它的所有 IRQ 绑定 */
void irq_release_all(int tid);

#endif