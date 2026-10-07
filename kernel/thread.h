#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>

#define STACK_SIZE (32 * 1024)

typedef enum { THREAD_READY, THREAD_BLOCKED, THREAD_DEAD } thread_state_t;

typedef struct message {
    int       sender;
    int       type;
    uint32_t  data[8];
    struct message *next;
} message_t;

typedef struct {
    int      sender;
    int      type;
    uint32_t data[8];
} user_msg_t;

typedef struct thread {
    uint32_t       esp;
    uint32_t      *stack_base;
    uint32_t      *kernel_stack;
    uint32_t      *user_stack;
    void         (*entry)(void);
    int            id;
    int            state;
    int            is_user;
    uint32_t      *page_dir;
    /* ★ S4.5: 语义从"键盘 owner 链"改为"父进程"——exec/exit 栈式嵌套 */
    struct thread *prev_owner;
    message_t     *msg_head;
    message_t     *msg_tail;
    struct thread *next;
} thread_t;

thread_t *thread_create(void (*entry)(void));
thread_t *thread_create_user(void (*entry)(void));
thread_t *thread_create_elf(uint32_t entry, uint32_t stack_top, uint32_t *page_dir);
void      thread_init(void);

extern thread_t *current_thread;

void switch_to(uint32_t *old_esp_ptr, uint32_t new_esp);

#endif