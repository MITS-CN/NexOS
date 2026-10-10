#include "ramfs.h"
#include "heap.h"

static vfs_node_t *root = 0;

static vfs_node_t *node_alloc(const char *name, int type) {
    vfs_node_t *n = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!n) return 0;

    int i = 0;
    while (name[i] && i < MAX_NAME - 1) { n->name[i] = name[i]; i++; }
    n->name[i] = 0;

    n->type     = type;
    n->size     = 0;
    n->data     = 0;
    n->capacity = 0;
    n->disk_block = NO_DISK_BLOCK;
    n->read_fn  = 0;          /* ★ C2 */
    n->is_dynamic = 0;        /* ★ C2 */
    n->parent   = 0;
    n->children = 0;
    n->next     = 0;
    return n;
}

static vfs_node_t *ramfs_create(vfs_node_t *parent, const char *name, int type) {
    vfs_node_t *n = node_alloc(name, type);
    if (!n) return 0;

    n->parent = parent;

    if (!parent->children) {
        parent->children = n;
    } else {
        vfs_node_t *c = parent->children;
        while (c->next) c = c->next;
        c->next = n;
    }
    return n;
}

static int ramfs_unlink(vfs_node_t *node) {
    if (!node || !node->parent) return -1;
    if (node->type == VFS_DIR && node->children) return -1;
    if (node->is_dynamic) return -1;    /* ★ C2 */

    vfs_node_t *parent = node->parent;

    if (parent->children == node) {
        parent->children = node->next;
    } else {
        vfs_node_t *c = parent->children;
        while (c && c->next != node) c = c->next;
        if (!c) return -1;
        c->next = node->next;
    }

    if (node->data) kfree(node->data);
    kfree(node);
    return 0;
}

static fs_driver_t ramfs_drv = {
    .name   = "ramfs",
    .init   = ramfs_init,
    .create = ramfs_create,
    .unlink = ramfs_unlink,
};

int ramfs_init(void) {
    root = node_alloc("/", VFS_DIR);
    if (!root) return -1;
    root->parent = 0;

    vfs_node_t *readme = ramfs_create(root, "README", VFS_FILE);
    static const char msg[] = "Welcome to NexOS-NEXT!\nThis is ramfs.\n";
    vfs_write(readme, 0, (const uint8_t *)msg, sizeof(msg) - 1);

    vfs_node_t *etc = ramfs_create(root, "etc", VFS_DIR);
    vfs_node_t *ver = ramfs_create(etc, "version", VFS_FILE);
    static const char ver_msg[] = "0.1\n";
    vfs_write(ver, 0, (const uint8_t *)ver_msg, sizeof(ver_msg) - 1);

    return 0;
}

vfs_node_t  *ramfs_root(void)   { return root; }
fs_driver_t *ramfs_driver(void) { return &ramfs_drv; }