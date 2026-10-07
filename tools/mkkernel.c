#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Tools

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input.bin> <output.bin>\n", argv[0]);
        return 1;
    }

    FILE *in = fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); return 1; }

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);

    if (size <= 0 || size > 32 * 1024 * 1024) {
        fprintf(stderr, "mkkernel: bad input size %ld\n", size);
        return 1;
    }

    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); return 1; }

    unsigned char hdr[512];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "NEXK", 4);

    uint32_t sz = (uint32_t)size;
    hdr[4] =  sz        & 0xFF;
    hdr[5] = (sz >> 8)  & 0xFF;
    hdr[6] = (sz >> 16) & 0xFF;
    hdr[7] = (sz >> 24) & 0xFF;

    fwrite(hdr, 1, 512, out);

    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);

    fclose(in);
    fclose(out);

    fprintf(stderr, "mkkernel: %s (%ld bytes) -> %s (header 512 + payload)\n",
            argv[1], size, argv[2]);
    return 0;
}