/* kernel/proc.c —— /proc 动态文件系统 */

#include "proc.h"
#include "vfs.h"
#include "heap.h"
#include "pmm.h"
#include "thread.h"
#include "sched.h"      /* ★ C2.5 */
#include "timer.h"

static int u32_to_str(uint32_t v, char *out) {
    char tmp[12];
    int n = 0;
    if (v == 0) { out[0] = '0'; return 1; }
    while (v) { tmp[n++] = '0' + (v % 10); v /= 10; }
    int i = 0;
    while (n > 0) out[i++] = tmp[--n];
    return i;
}

static int copy_str(const char *s, uint32_t slen, uint32_t offset,
                    uint8_t *buf, uint32_t len) {
    if (offset >= slen) return 0;
    uint32_t avail = slen - offset;
    uint32_t take = (len < avail) ? len : avail;
    for (uint32_t i = 0; i < take; i++) buf[i] = (uint8_t)s[offset + i];
    return (int)take;
}

static int proc_mem_read(vfs_node_t *n, uint32_t offset,
                         uint8_t *buf, uint32_t len) {
    (void)n;
    char tmp[128];
    int p = 0;

    const char *l1 = "total: ";
    while (*l1) tmp[p++] = *l1++;
    p += u32_to_str(pmm_total_pages(), tmp + p);
    tmp[p++] = '\n';

    const char *l2 = "used:  ";
    while (*l2) tmp[p++] = *l2++;
    p += u32_to_str(pmm_used_pages(), tmp + p);
    tmp[p++] = '\n';

    const char *l3 = "free:  ";
    while (*l3) tmp[p++] = *l3++;
    p += u32_to_str(pmm_total_pages() - pmm_used_pages(), tmp + p);
    tmp[p++] = '\n';

    return copy_str(tmp, (uint32_t)p, offset, buf, len);
}

static int proc_tid_read(vfs_node_t *n, uint32_t offset,
                         uint8_t *buf, uint32_t len) {
    (void)n;
    char tmp[16];
    int p = 0;
    uint32_t tid = current_thread ? (uint32_t)current_thread->id : 0;
    p += u32_to_str(tid, tmp + p);
    tmp[p++] = '\n';
    return copy_str(tmp, (uint32_t)p, offset, buf, len);
}

static int proc_uptime_read(vfs_node_t *n, uint32_t offset,
                            uint8_t *buf, uint32_t len) {
    (void)n;
    uint32_t t = timer_get_ticks();
    char tmp[32];
    int p = 0;
    p += u32_to_str(t / 100, tmp + p);
    tmp[p++] = '.';
    uint32_t cs = t % 100;
    tmp[p++] = (char)('0' + (cs / 10));
    tmp[p++] = (char)('0' + (cs % 10));
    tmp[p++] = '\n';
    return copy_str(tmp, (uint32_t)p, offset, buf, len);
}

/* ★ C2.5: 列出所有线程 */
static const char *state_name(int s) {
    switch (s) {
        case THREAD_READY:   return "READY";
        case THREAD_BLOCKED: return "BLOCK";
        case THREAD_DEAD:    return "DEAD";
        default:             return "?";
    }
}

static int proc_threads_read(vfs_node_t *n, uint32_t offset,
                             uint8_t *buf, uint32_t len) {
    (void)n;
    char tmp[2048];
    int p = 0;

    const char *hdr = "tid state user\n";
    while (*hdr && p < 2000) tmp[p++] = *hdr++;

    int cnt = sched_thread_count();
    for (int i = 0; i < cnt && p < 2000; i++) {
        thread_t *t = sched_thread_at(i);
        if (!t) continue;

        p += u32_to_str((uint32_t)t->id, tmp + p);
        tmp[p++] = ' ';

        const char *sn = state_name(t->state);
        while (*sn && p < 2000) tmp[p++] = *sn++;

        tmp[p++] = ' ';
        tmp[p++] = t->is_user ? '1' : '0';
        tmp[p++] = '\n';
    }

    return copy_str(tmp, (uint32_t)p, offset, buf, len);
}

static vfs_node_t *make_proc_file(const char *name, vfs_read_fn fn) {
    vfs_node_t *n = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!n) return 0;

    int i = 0;
    while (name[i] && i < MAX_NAME - 1) { n->name[i] = name[i]; i++; }
    n->name[i]   = 0;

    n->type       = VFS_FILE;
    n->size       = 0;
    n->data       = 0;
    n->capacity   = 0;
    n->disk_block = NO_DISK_BLOCK;
    n->read_fn    = fn;
    n->is_dynamic = 1;
    n->parent     = 0;
    n->children   = 0;
    n->next       = 0;
    return n;
}

int proc_init(void) {
    extern vfs_node_t *vfs_root(void);
    vfs_node_t *root = vfs_root();
    if (!root) return -1;

    for (vfs_node_t *c = root->children; c; c = c->next) {
        if (c->name[0] == 'p' && c->name[1] == 'r' &&
            c->name[2] == 'o' && c->name[3] == 'c' && c->name[4] == 0) {
            return 0;
        }
    }

    vfs_node_t *dir = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!dir) return -1;
    const char *dn = "proc";
    int i = 0;
    while (dn[i] && i < MAX_NAME - 1) { dir->name[i] = dn[i]; i++; }
    dir->name[i]   = 0;
    dir->type      = VFS_DIR;
    dir->size      = 0;
    dir->data      = 0;
    dir->capacity  = 0;
    dir->disk_block = NO_DISK_BLOCK;
    dir->read_fn   = 0;
    dir->is_dynamic = 1;
    dir->parent    = root;
    dir->children  = 0;
    dir->next      = 0;

    vfs_node_t *files[4];
    files[0] = make_proc_file("mem",     proc_mem_read);
    files[1] = make_proc_file("tid",     proc_tid_read);
    files[2] = make_proc_file("uptime",  proc_uptime_read);
    files[3] = make_proc_file("threads", proc_threads_read);

    vfs_node_t *tail = 0;
    for (int k = 0; k < 4; k++) {
        vfs_node_t *n = files[k];
        if (!n) continue;
        n->parent = dir;
        if (!tail) dir->children = n;
        else       tail->next    = n;
        tail = n;
    }

    if (!root->children) {
        root->children = dir;
    } else {
        vfs_node_t *c = root->children;
        while (c->next) c = c->next;
        c->next = dir;
    }

    return 0;
}