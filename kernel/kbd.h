#ifndef KBD_H
#define KBD_H

#include "thread.h"

void kbd_init(void);
void kbd_irq(void);
int  kbd_getchar(void);
int  kbd_poll(void);
int  kbd_confirm(const char *prompt);
void kbd_wait(void);

thread_t *kbd_get_owner(void);
void      kbd_set_owner(thread_t *t);

#endif