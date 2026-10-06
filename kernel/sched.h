// sched.h
#ifndef SCHED_H
#define SCHED_H

#include "thread.h"

void sched_add(thread_t *t);
void sched_start(void);
void sched_tick(void);
void sched_yield(void);

/* S4: 补上 sched_find 声明，ipc.c / irq.c 都要用 */
thread_t *sched_find(int tid);

#endif