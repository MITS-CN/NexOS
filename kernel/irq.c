#include "irq.h"
#include "ipc.h"
#include "sched.h"
#include "io.h"

static int       irq_owners[IRQ_MAX];
static uint16_t  irq_read_ports[IRQ_MAX];
static int       inited = 0;

static void irq_lazy_init(void) {
    if (inited) return;
    for (int i = 0; i < IRQ_MAX; i++) {
        irq_owners[i]     = -1;
        irq_read_ports[i] = IRQ_NO_READ;
    }
    inited = 1;
}

int irq_register(int irq, int tid, uint16_t read_port) {
    irq_lazy_init();
    if (irq < 0 || irq >= IRQ_MAX) return -1;
    if (tid < 0) return -2;
    if (irq_owners[irq] != -1 && irq_owners[irq] != tid)
        return -3;
    irq_owners[irq]     = tid;
    irq_read_ports[irq] = read_port;
    return 0;
}

int irq_unregister(int irq, int tid) {
    irq_lazy_init();
    if (irq < 0 || irq >= IRQ_MAX) return -1;
    if (irq_owners[irq] != tid) return -2;
    irq_owners[irq]     = -1;
    irq_read_ports[irq] = IRQ_NO_READ;
    return 0;
}

int irq_owner(int irq) {
    irq_lazy_init();
    if (irq < 0 || irq >= IRQ_MAX) return -1;
    return irq_owners[irq];
}

/* ★ S4.6.2 */
int irq_find_by_owner(int tid) {
    irq_lazy_init();
    for (int i = 0; i < IRQ_MAX; i++) {
        if (irq_owners[i] == tid) return i;
    }
    return -1;
}

void irq_dispatch(int irq) {
    irq_lazy_init();
    if (irq < 0 || irq >= IRQ_MAX) return;

    int tid = irq_owners[irq];
    if (tid < 0) return;

    thread_t *t = sched_find(tid);
    if (!t) return;

    uint32_t data = 0;
    uint16_t port = irq_read_ports[irq];
    if (port != IRQ_NO_READ) {
        data = (uint32_t)inb(port);
    }

    message_t m;
    m.sender  = -1;
    m.type    = MSG_IRQ;
    m.data[0] = (uint32_t)irq;
    m.data[1] = data;
    for (int i = 2; i < 8; i++) m.data[i] = 0;

    ipc_send(tid, &m);
}

void irq_release_all(int tid) {
    irq_lazy_init();
    for (int i = 0; i < IRQ_MAX; i++) {
        if (irq_owners[i] == tid) {
            irq_owners[i]     = -1;
            irq_read_ports[i] = IRQ_NO_READ;
        }
    }
}