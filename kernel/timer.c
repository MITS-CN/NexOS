// timer.c
#include "timer.h"
#include "io.h"
#include "sched.h"

#define PIT_FREQ  1193182
#define TARGET_HZ 100

static volatile uint32_t ticks = 0;   /* ★ C2 */

void timer_init(void) {
    uint32_t divisor = PIT_FREQ / TARGET_HZ;
    ticks = 0;

    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

void timer_tick(void) {
    ticks++;
    sched_tick();
}

uint32_t timer_get_ticks(void) {   /* ★ C2 */
    return ticks;
}