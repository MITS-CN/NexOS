#include "thread.h"
#include "heap.h"
#include "sched.h"

thread_t *current_thread = 0;
static int next_id = 1;
extern void enter_usermode(uint32_t entry, uint32_t user_stack);

static void thread_user_stub(void) {
    __asm__ volatile("sti");
    uint32_t uesp = current_thread->entry_esp;
    if (uesp == 0)
        uesp = (uint32_t)current_thread->user_stack + 16 * 1024;
    enter_usermode((uint32_t)current_thread->entry, uesp);
    for (;;) __asm__ volatile("pause");
}

static void thread_stub(void) {
    __asm__ volatile("sti");
    if (current_thread && current_thread->entry)
        current_thread->entry();
    for (;;) __asm__ volatile("hlt");
}

thread_t *thread_create_user(void (*entry)(void)) {
    thread_t *t = (thread_t *)kmalloc(sizeof(thread_t));
    if (!t) return 0;

    t->kernel_stack = (uint32_t *)kmalloc(STACK_SIZE);
    t->user_stack   = (uint32_t *)kmalloc(16 * 1024);
    if (!t->kernel_stack || !t->user_stack) return 0;

    t->stack_base = t->kernel_stack;
    t->entry      = entry;
    t->entry_esp  = 0;      /* ★ S7a: 走兜底 */
    t->id         = next_id++;
    t->state      = THREAD_READY;
    t->is_user    = 1;
    t->page_dir   = 0;
    t->msg_head   = 0;
    t->msg_tail   = 0;
    t->next       = 0;

    uint32_t *sp = t->kernel_stack + (STACK_SIZE / sizeof(uint32_t));
    sp = (uint32_t *)((uint32_t)sp & ~15u);

    *--sp = (uint32_t)thread_user_stub;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;

    t->esp = (uint32_t)sp;
    sched_add(t);
    return t;
}

thread_t *thread_create_elf(uint32_t entry, uint32_t stack_top,
                            uint32_t *page_dir, uint32_t entry_esp) {
    thread_t *t = (thread_t *)kmalloc(sizeof(thread_t));
    if (!t) return 0;

    t->kernel_stack = (uint32_t *)kmalloc(STACK_SIZE);
    if (!t->kernel_stack) { kfree(t); return 0; }

    t->user_stack = (uint32_t *)(stack_top - 16 * 1024);
    t->stack_base = t->kernel_stack;
    t->entry      = (void (*)(void))entry;
    t->entry_esp  = entry_esp;   /* ★ S7a */
    t->id         = next_id++;
    t->state      = THREAD_READY;
    t->is_user    = 1;
    t->msg_head   = 0;
    t->msg_tail   = 0;
    t->next       = 0;
    t->page_dir   = page_dir;

    uint32_t *sp = t->kernel_stack + (STACK_SIZE / sizeof(uint32_t));
    sp = (uint32_t *)((uint32_t)sp & ~15u);

    *--sp = (uint32_t)thread_user_stub;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;

    t->esp = (uint32_t)sp;
    sched_add(t);
    return t;
}

thread_t *thread_create(void (*entry)(void)) {
    thread_t *t = (thread_t *)kmalloc(sizeof(thread_t));
    if (!t) return 0;

    uint32_t *stack = (uint32_t *)kmalloc(STACK_SIZE);
    if (!stack) return 0;

    t->stack_base   = stack;
    t->kernel_stack = stack;
    t->user_stack   = 0;
    t->entry        = entry;
    t->entry_esp    = 0;      /* 内核线程不用 */
    t->id           = next_id++;
    t->state        = THREAD_READY;
    t->is_user      = 0;
    t->page_dir     = 0;
    t->msg_head     = 0;
    t->msg_tail     = 0;
    t->next         = 0;

    uint32_t *sp = stack + (STACK_SIZE / sizeof(uint32_t));
    sp = (uint32_t *)((uint32_t)sp & ~15u);

    *--sp = (uint32_t)thread_stub;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;

    t->esp = (uint32_t)sp;
    sched_add(t);
    return t;
}

void thread_init(void) { }