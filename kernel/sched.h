// sched.h
#ifndef SCHED_H
#define SCHED_H

#include "thread.h"

void sched_add(thread_t *t);
void sched_start(void);
void sched_tick(void);
void sched_yield(void);

thread_t *sched_find(int tid);

int       sched_thread_count(void);
thread_t *sched_thread_at(int idx);

/* ★ S6.5-2: IRQ 立即抢占支持 */
void sched_set_need_resched(void);
int  sched_take_need_resched(void);

#endif