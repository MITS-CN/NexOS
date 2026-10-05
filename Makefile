CC      = gcc
CFLAGS  = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-pie
LDFLAGS = -m32 -T linker.ld -ffreestanding -O2 -nostdlib -no-pie

USER_CFLAGS  = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
               -fno-pie -fno-stack-protector -fno-builtin
USER_LDFLAGS = -m32 -T user/user.ld -ffreestanding -O2 -nostdlib -no-pie

IMAGE_DIR = images
NXFS_IMG = build/nxfs.img

OBJS = build/serial.o \
       build/entry.o build/kmain.o \
       build/gdt.o build/gdt_flush.o \
       build/idt.o build/idt_flush.o \
       build/isr.o build/isr_stub.o \
       build/pmm.o build/paging.o build/paging_flush.o \
       build/heap.o build/vfs.o build/ramfs.o \
       build/ata.o build/nxfs.o build/block_cache.o build/part.o \
       build/thread.o build/sched.o build/switch.o \
       build/timer.o build/ipc.o build/kbd.o \
       build/syscall.o build/usermode.o \
       build/elf_loader.o

# ============================================================
# 默认目标：生成 ISO + 磁盘镜像
# ============================================================
all: build/NexOS-NEXT.iso $(IMAGE_DIR)/disk.img

# ============================================================
# 运行
# ============================================================
run: $(IMAGE_DIR)/disk.img $(IMAGE_DIR)/disk2.img
	@-pkill -x qemu-system-i386 2>/dev/null
	@sleep 1
	@rm -f /tmp/qemu-dbg.log
	qemu-system-i386 \
	    -drive file=$(IMAGE_DIR)/disk.img,format=raw,if=ide,index=0 \
	    -drive file=$(IMAGE_DIR)/disk2.img,format=raw,if=ide,index=1 \
	    -m 128M \
	    -no-reboot -no-shutdown \
	    -d int,cpu_reset -D /tmp/qemu-dbg.log

run-iso: build/NexOS-NEXT.iso $(IMAGE_DIR)/disk.img $(IMAGE_DIR)/disk2.img
	qemu-system-i386 -cdrom build/NexOS-NEXT.iso -boot d \
	    -drive file=$(IMAGE_DIR)/disk.img,format=raw,if=ide,index=0 \
	    -drive file=$(IMAGE_DIR)/disk2.img,format=raw,if=ide,index=1 \
	    -m 128M

# ============================================================
# 目录
# ============================================================
build:
	mkdir -p build

$(IMAGE_DIR):
	mkdir -p $(IMAGE_DIR)

# ============================================================
# 编译规则
# ============================================================
build/%.o: kernel/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: kernel/%.S | build
	$(CC) $(CFLAGS) -c $< -o $@

build/entry.o: kernel/entry.S | build
	$(CC) $(CFLAGS) -c $< -o $@

# ============================================================
# 用户程序
# ============================================================
build/user_start.o: user/start.S | build
	$(CC) $(USER_CFLAGS) -c $< -o $@

build/user_main.o: user/main.c | build
	$(CC) $(USER_CFLAGS) -c $< -o $@

build/init.elf: build/user_start.o build/user_main.o user/user.ld
	$(CC) $(USER_LDFLAGS) -o $@ build/user_start.o build/user_main.o

# ============================================================
# 内核 ELF
# ============================================================
build/NexOS-NEXT.elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

# ============================================================
# 内核 raw binary（给自写引导器用）
# ============================================================
build/kernel.bin: build/NexOS-NEXT.elf
	objcopy -O binary -R .multiboot -R .bss -R .comment -R .note $< $@

# 加 512B 头
build/kernel_payload.bin: build/kernel.bin tools/mkkernel
	./tools/mkkernel build/kernel.bin $@

tools/mkkernel: tools/mkkernel.c
	$(CC) -O2 -o $@ $<

# ============================================================
# 引导器
# ============================================================
build/stage1.bin: boot/stage1.asm | build
	nasm -f bin -o $@ $<

build/stage2.bin: boot/stage2.asm | build
	nasm -f bin -o $@ $<

# ============================================================
# ISO（GRUB 启动）
# ============================================================
build/NexOS-NEXT.iso: build/NexOS-NEXT.elf build/init.elf grub.cfg
	mkdir -p build/iso/boot/grub
	cp build/NexOS-NEXT.elf build/iso/boot/
	cp build/init.elf       build/iso/boot/
	cp grub.cfg build/iso/boot/grub/
	grub-mkrescue -o $@ build/iso

# ============================================================
# 磁盘镜像（自写引导器启动）
# ============================================================
$(IMAGE_DIR)/disk.img: build/stage1.bin build/stage2.bin build/kernel_payload.bin $(NXFS_IMG) | $(IMAGE_DIR)
	@if [ ! -f $@ ]; then \
	    echo "==> creating $@ (64MB)"; \
	    dd if=/dev/zero of=$@ bs=1M count=64 status=none; \
	fi
	dd if=build/stage1.bin of=$@ bs=512 seek=0   conv=notrunc status=none
	dd if=build/stage2.bin of=$@ bs=512 seek=1   conv=notrunc status=none
	dd if=build/kernel_payload.bin of=$@ bs=512 seek=17 conv=notrunc status=none
	dd if=$(NXFS_IMG) of=$@ bs=1M seek=1 conv=notrunc status=none
	@echo "==> disk.img updated (boot + NXFS)"

$(IMAGE_DIR)/disk2.img: | $(IMAGE_DIR)
	@if [ ! -f $@ ]; then \
	    dd if=/dev/zero of=$@ bs=1M count=16 status=none; \
	    echo "==> created $@"; \
	fi

#TOOLS

tools/mknxfs: tools/mknxfs.c
	$(CC) -O2 -o $@ $<

$(NXFS_IMG): build/init.elf tools/mknxfs | build
	./tools/mknxfs $@ build/init.elf:init.elf

# ============================================================
# 清理
# ============================================================
clean:
	rm -rf build

clean-all: clean
	rm -rf $(IMAGE_DIR)

.PHONY: all run run-iso clean clean-all