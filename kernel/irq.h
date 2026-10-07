#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include "thread.h"    /* MSG_IRQ / MSG_IRQ_OWNER_DIED 现在集中在这里 */

#define IRQ_MAX      16
#define IRQ_NO_READ  0xFFFFu

int  irq_register(int irq, int tid, uint16_t read_port);
int  irq_unregister(int irq, int tid);
int  irq_owner(int irq);
int  irq_find_by_owner(int tid);
void irq_dispatch(int irq);
void irq_release_all(int tid);

#endif