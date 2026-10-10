// timer.h
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init(void);
void timer_tick(void);
uint32_t timer_get_ticks(void);   /* ★ C2 */

#endif