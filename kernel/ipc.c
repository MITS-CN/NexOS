// ipc.c
#include "ipc.h"
#include "sched.h"
#include "heap.h"

extern thread_t *sched_find(int tid);

int ipc_send(int tid, message_t *msg) {
    thread_t *t = sched_find(tid);
    if (!t) return -1;

    message_t *m = (message_t *)kmalloc(sizeof(message_t));
    if (!m) return -1;

    *m = *msg;
    m->next = 0;

    /* 入队 */
    if (t->msg_tail)
        t->msg_tail->next = m;
    else
        t->msg_head = m;
    t->msg_tail = m;

    /* 唤醒阻塞的接收方 */
    if (t->state == THREAD_BLOCKED)
        t->state = THREAD_READY;

    return 0;
}

void ipc_recv(message_t *out) {
    for (;;) {
        if (current_thread->msg_head) {
            message_t *m = current_thread->msg_head;
            current_thread->msg_head = m->next;
            if (!current_thread->msg_head)
                current_thread->msg_tail = 0;

            *out = *m;
            kfree(m);
            return;
        }

        /* 队列空：标记阻塞，开中断并休眠，等中断唤醒
           ★ S4 修正：原来是 sched_yield()，单线程场景下会死循环，
              且 syscall 上下文 cli 关中断，IRQ 进不来 */
        current_thread->state = THREAD_BLOCKED;
        __asm__ volatile("sti; hlt");
    }
}