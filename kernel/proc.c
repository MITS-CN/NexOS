/* kernel/proc.c —— /proc 动态文件系统
 *
 * 挂载点：NXFS root 下的 "proc" 目录（纯内存，不落盘）
 * 当前暴露：
 *   /proc/mem    - total / used / free 页面数
 *   /proc/tid    - 读该文件的进程 tid
 *   /proc/uptime - PIT tick 换算的秒.厘秒
 */

#include "proc.h"
#include "vfs.h"
#include "heap.h"
#include "pmm.h"
#include "thread.h"
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

    /* 幂等：已经挂过就不重复挂 */
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
    dir->is_dynamic = 1;         /* 不可 rmdir */
    dir->parent    = root;
    dir->children  = 0;
    dir->next      = 0;

    vfs_node_t *mem    = make_proc_file("mem",    proc_mem_read);
    vfs_node_t *tid    = make_proc_file("tid",    proc_tid_read);
    vfs_node_t *uptime = make_proc_file("uptime", proc_uptime_read);

    vfs_node_t *tail = 0;
    vfs_node_t *arr[3] = { mem, tid, uptime };
    for (int k = 0; k < 3; k++) {
        vfs_node_t *n = arr[k];
        if (!n) continue;
        n->parent = dir;
        if (!tail) dir->children = n;
        else       tail->next    = n;
        tail = n;
    }

    /* 挂到 root 末尾 */
    if (!root->children) {
        root->children = dir;
    } else {
        vfs_node_t *c = root->children;
        while (c->next) c = c->next;
        c->next = dir;
    }

    return 0;
}