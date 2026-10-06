#include "irq.h"
#include "ipc.h"
#include "sched.h"

static int  irq_owners[IRQ_MAX];
static int  inited = 0;

static void irq_lazy_init(void) {
    if (inited) return;
    for (int i = 0; i < IRQ_MAX; i++) irq_owners[i] = -1;
    inited = 1;
}

int irq_register(int irq, int tid) {
    irq_lazy_init();
    if (irq < 0 || irq >= IRQ_MAX) return -1;   /* 越界 */
    if (tid < 0) return -2;                      /* 非法 tid */
    if (irq_owners[irq] != -1 && irq_owners[irq] != tid)
        return -3;                               /* 已被别人占用 */
    irq_owners[irq] = tid;
    return 0;
}

int irq_unregister(int irq, int tid) {
    irq_lazy_init();
    if (irq < 0 || irq >= IRQ_MAX) return -1;
    if (irq_owners[irq] != tid) return -2;       /* 不是你的 */
    irq_owners[irq] = -1;
    return 0;
}

int irq_owner(int irq) {
    irq_lazy_init();
    if (irq < 0 || irq >= IRQ_MAX) return -1;
    return irq_owners[irq];
}

void irq_dispatch(int irq, uint32_t scancode) {
    irq_lazy_init();
    if (irq < 0 || irq >= IRQ_MAX) return;

    int tid = irq_owners[irq];
    if (tid < 0) return;

    thread_t *t = sched_find(tid);
    if (!t) return;   /* owner 线程已不存在，静默忽略 */

    message_t m;
    m.sender  = -1;              /* 内核 */
    m.type    = MSG_IRQ;
    m.data[0] = (uint32_t)irq;
    m.data[1] = scancode;
    for (int i = 2; i < 8; i++) m.data[i] = 0;

    /* ipc_send 内部 kmalloc，失败返回 -1，静默忽略 */
    ipc_send(tid, &m);
}

void irq_release_all(int tid) {
    irq_lazy_init();
    for (int i = 0; i < IRQ_MAX; i++) {
        if (irq_owners[i] == tid) irq_owners[i] = -1;
    }
}