// sched.h
#ifndef SCHED_H
#define SCHED_H

#include "thread.h"

void sched_add(thread_t *t);
void sched_start(void);
void sched_tick(void);
void sched_yield(void);

thread_t *sched_find(int tid);

/* ★ C2.5: 遍历线程列表 */
int       sched_thread_count(void);
thread_t *sched_thread_at(int idx);

#endif