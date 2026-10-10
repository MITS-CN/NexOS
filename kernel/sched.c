#include "sched.h"
#include "paging.h"
#include "heap.h"
#include "irq.h"

extern void gdt_set_kernel_stack(uint32_t esp0);

#define MAX_THREADS 64

static thread_t *threads[MAX_THREADS];
static int       thread_count = 0;
static int       current_idx  = 0;

thread_t *sched_find(int tid) {
    for (int i = 0; i < thread_count; i++)
        if (threads[i]->id == tid) return threads[i];
    return 0;
}

void sched_add(thread_t *t) {
    if (thread_count < MAX_THREADS)
        threads[thread_count++] = t;
}

static int next_ready(int from) {
    for (int i = 1; i <= thread_count; i++) {
        int idx = (from + i) % thread_count;
        if (threads[idx]->state == THREAD_READY) return idx;
    }
    return -1;
}

static void free_msg_chain(message_t *m) {
    while (m) {
        message_t *next = m->next;
        kfree(m);
        m = next;
    }
}

static void reclaim_dead_threads(void) {
    int i = 0;
    while (i < thread_count) {
        thread_t *t = threads[i];
        if (t->state == THREAD_DEAD && t != current_thread) {
            irq_release_all(t->id);

            for (int j = i; j < thread_count - 1; j++)
                threads[j] = threads[j + 1];
            thread_count--;
            if (i < current_idx) current_idx--;

            free_msg_chain(t->msg_head);
            t->msg_head = 0;
            t->msg_tail = 0;

            if (t->kernel_stack) kfree(t->kernel_stack);
            kfree(t);
        } else {
            i++;
        }
    }
    if (thread_count > 0 && current_idx >= thread_count)
        current_idx = 0;
}

static void do_switch(int old_idx, int new_idx) {
    thread_t *old = threads[old_idx];
    thread_t *nxt = threads[new_idx];

    uint32_t *ks = nxt->kernel_stack ? nxt->kernel_stack : nxt->stack_base;
    if (ks) gdt_set_kernel_stack((uint32_t)ks + STACK_SIZE);

    current_thread = nxt;

    if (nxt->page_dir && nxt->page_dir != paging_get_dir())
        paging_switch_dir(nxt->page_dir);

    switch_to(&old->esp, nxt->esp);
}

void sched_tick(void) {
    reclaim_dead_threads();

    if (thread_count < 2 || !current_thread) return;

    int next = next_ready(current_idx);
    if (next < 0 || next == current_idx) return;

    int old_idx = current_idx;
    current_idx = next;
    do_switch(old_idx, next);
}

void sched_yield(void) {
    reclaim_dead_threads();

    if (thread_count < 2 || !current_thread) return;

    int next = next_ready(current_idx);
    if (next < 0) {
        extern void vga_puts(const char *);
        vga_puts("\n[KERNEL] all threads dead, halting\n");
        for (;;) __asm__ volatile("hlt");
    }

    int old_idx = current_idx;
    current_idx = next;
    do_switch(old_idx, next);
}

void sched_start(void) {
    if (thread_count == 0) {
        extern void vga_puts(const char *);
        vga_puts("[FATAL] sched_start: no threads\n");
        for (;;) __asm__ volatile("hlt");
    }

    current_idx    = 0;
    current_thread = threads[0];

    uint32_t *ks = current_thread->kernel_stack
                 ? current_thread->kernel_stack
                 : current_thread->stack_base;
    if (ks) gdt_set_kernel_stack((uint32_t)ks + STACK_SIZE);

    if (current_thread->page_dir &&
        current_thread->page_dir != paging_get_dir())
        paging_switch_dir(current_thread->page_dir);

    uint32_t dummy;
    switch_to(&dummy, current_thread->esp);
    for (;;) __asm__ volatile("hlt");
}

/* ★ C2.5: 遍历接口 */
int sched_thread_count(void) {
    return thread_count;
}

thread_t *sched_thread_at(int idx) {
    if (idx < 0 || idx >= thread_count) return 0;
    return threads[idx];
}