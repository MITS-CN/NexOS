#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include "thread.h"

#define IRQ_MAX      16
#define MSG_IRQ            0x100
#define MSG_IRQ_OWNER_DIED 0x101   /* ★ S4.6.2: 内核通知父进程 IRQ owner 已死 */
#define IRQ_NO_READ  0xFFFFu

int  irq_register(int irq, int tid, uint16_t read_port);
int  irq_unregister(int irq, int tid);
int  irq_owner(int irq);
int  irq_find_by_owner(int tid);   /* ★ S4.6.2: 返回 tid 持有的第一个 IRQ，或 -1 */
void irq_dispatch(int irq);
void irq_release_all(int tid);

#endif