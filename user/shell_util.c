#include "shell.h"

void putc_(char c) { sys_putchar(c); }
void puts_(const char *s) { while (*s) putc_(*s++); }

int str_len(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

int str_prefix(const char *s, const char *pre) {
    while (*pre) { if (*s++ != *pre++) return 0; }
    return 1;
}

void skip_spaces(const char **p) {
    while (**p == ' ') (*p)++;
}

int take_word(const char *src, char *out, int out_size) {
    int i = 0;
    while (src[i] && src[i] != ' ' && i < out_size - 1) {
        out[i] = src[i];
        i++;
    }
    out[i] = 0;
    return i;
}

void print_hex16(uint16_t v) {
    const char *h = "0123456789ABCDEF";
    putc_(h[(v >> 12) & 0xF]);
    putc_(h[(v >>  8) & 0xF]);
    putc_(h[(v >>  4) & 0xF]);
    putc_(h[ v        & 0xF]);
}

void print_dec(int v) {
    char b[12]; int i = 0;
    if (v < 0) { putc_('-'); v = -v; }
    if (v == 0) b[i++] = '0';
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    while (i--) putc_(b[i]);
}

void print_dec_pad(uint32_t v, int width) {
    char b[12]; int i = 0;
    if (v == 0) b[i++] = '0';
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    while (i < width) b[i++] = ' ';
    while (i--) putc_(b[i]);
}

void perm_str(uint16_t mode, char *out) {
    const char *rwx = "rwx";
    for (int i = 0; i < 9; i++) {
        int bit = 1 << (8 - i);
        out[i] = (mode & bit) ? rwx[i % 3] : '-';
    }
    out[9] = 0;
}

uint32_t parse_oct(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '7') {
        v = v * 8 + (*s - '0');
        s++;
    }
    return v;
}

uint32_t parse_u32(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

void resolve_path(const char *in, char *out, int out_size) {
    char tmp[MAX_PATH];

    if (!in || !*in) in = ".";

    if (in[0] == '/') {
        tmp[0] = 0;
    } else {
        int i = 0;
        while (cwd[i] && i < MAX_PATH - 1) { tmp[i] = cwd[i]; i++; }
        tmp[i] = 0;
    }

    const char *p = in;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        char seg[64];
        int len = 0;
        while (*p && *p != '/' && len < 63) seg[len++] = *p++;
        seg[len] = 0;

        if (str_eq(seg, ".")) continue;

        if (str_eq(seg, "..")) {
            int i = 0;
            while (tmp[i]) i++;
            while (i > 0 && tmp[i - 1] != '/') i--;
            if (i > 0) i--;
            if (i == 0) { tmp[0] = '/'; tmp[1] = 0; }
            else tmp[i] = 0;
            continue;
        }

        int i = 0;
        while (tmp[i]) i++;
        if (i == 0 || tmp[i - 1] != '/') tmp[i++] = '/';
        int j = 0;
        while (seg[j] && i < MAX_PATH - 1) tmp[i++] = seg[j++];
        tmp[i] = 0;
    }

    if (tmp[0] == 0) { tmp[0] = '/'; tmp[1] = 0; }

    int i = 0;
    while (tmp[i] && i < out_size - 1) { out[i] = tmp[i]; i++; }
    out[i] = 0;
}