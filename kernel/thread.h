#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>

#define STACK_SIZE (32 * 1024)

/* ★ IPC 消息类型集中定义（内核 ↔ 用户态驱动共用） */
#define MSG_IRQ            0x100
#define MSG_IRQ_OWNER_DIED 0x101
#define MSG_HELLO          0x200
#define MSG_CHAR           0x201
#define MSG_EXIT           0x202
#define MSG_VGA_CHAR       0x300
#define MSG_VGA_OWNER_DIED 0x301
#define MSG_MOUSE_QUERY    0x302
#define MSG_MOUSE_REPORT   0x303
#define MSG_SCROLL         0x304
#define MSG_SIGINT         0x305
/* ★ S6c: ATA 请求 */
#define MSG_ATA_REQ        0x400

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