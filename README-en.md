

# NexOS-NEXT

> A 32-bit operating system kernel for x86, written from scratch in C and assembly (branch `NexOS-NEXT`).

NexOS-NEXT includes a custom bootloader, memory management, paging, multithreading with scheduling, inter-process communication, system calls, user mode (ring 3), ELF loading, an ATA disk driver, MBR partition table support, and a self-designed on-disk filesystem, **NXFS**. After boot, the kernel loads an embedded init program and drops into an interactive shell.

- **Architecture**: i386 (32-bit protected mode)
- **Languages**: C (`-std=gnu99`) + GNU/NASM assembly
- **License**: Apache License 2.0 (see [LICENSE](LICENSE))
- **English** | [简体中文](README.md)

---

## Table of Contents

- [Features](#features)
- [Directory Layout](#directory-layout)
- [Build Requirements](#build-requirements)
- [Build & Run](#build--run)
- [Boot Flow](#boot-flow)
- [Disk Image Layout](#disk-image-layout)
- [Memory Layout](#memory-layout)
- [System Calls](#system-calls)
- [Shell Commands](#shell-commands)
- [The NXFS Filesystem](#the-nxfs-filesystem)
- [Known Limitations](#known-limitations)
- [License](#license)

---

## Features

| Module | Description | Location |
| --- | --- | --- |
| Boot | Custom two-stage bootloader (MBR + stage2); also boots via GRUB/Multiboot ISO | [boot/](boot/) |
| GDT / TSS | Protected-mode segment descriptors, ring 0/ring 3, TSS for user→kernel stack switching | [kernel/gdt.c](kernel/gdt.c) |
| IDT / Interrupts | Interrupt descriptor table, ISR stubs, exception handling | [kernel/idt.c](kernel/idt.c), [kernel/isr.c](kernel/isr.c) |
| Physical memory | Page-frame allocator (PMM) | [kernel/pmm.c](kernel/pmm.c) |
| Paging | Two-level page tables, virtual address mapping | [kernel/paging.c](kernel/paging.c) |
| Heap | Kernel `kmalloc` / `kfree` | [kernel/heap.c](kernel/heap.c) |
| Threads & scheduling | Thread creation, context switching, round-robin scheduling, **100 Hz** timer | [kernel/thread.c](kernel/thread.c), [kernel/sched.c](kernel/sched.c) |
| IPC | Inter-thread message passing (`send` / `recv`) | [kernel/ipc.c](kernel/ipc.c) |
| System calls | 16 syscalls exposed via `int 0x80` | [kernel/syscall.c](kernel/syscall.c) |
| User mode | ring 3 execution with user-pointer range validation | [kernel/usermode.S](kernel/usermode.S) |
| ELF loading | Parses and loads the init ELF into user space | [kernel/elf_loader.c](kernel/elf_loader.c) |
| Device drivers | VGA text console, serial (COM1, 38400 8N1), PS/2 keyboard, ATA PIO | [kernel/serial.c](kernel/serial.c), [kernel/kbd.c](kernel/kbd.c), [kernel/ata.c](kernel/ata.c) |
| Block cache | Read/write cache for disk blocks | [kernel/block_cache.c](kernel/block_cache.c) |
| Filesystems | VFS abstraction + ramfs + **NXFS** (persistent on disk) | [kernel/vfs.c](kernel/vfs.c), [kernel/ramfs.c](kernel/ramfs.c), [kernel/nxfs.c](kernel/nxfs.c) |
| Partition table | Read/write MBR primary partition table (4 entries) | [kernel/part.c](kernel/part.c) |
| User program | Embedded init shell (`NexOS-NEXT Shell v0.5`) | [user/main.c](user/main.c) |

---

## Directory Layout

```
NexOS-NEXT/
├── boot/                 # Custom bootloader
│   ├── stage1.asm        # 512-byte MBR, loads stage2
│   └── stage2.asm        # Reads the kernel, enters protected mode, jumps
├── kernel/               # Kernel sources
│   ├── entry.S           # Kernel entry _start (clears BSS, calls kmain)
│   ├── kmain.c           # Main kernel flow and VGA console
│   ├── gdt / idt / isr   # Descriptor tables and interrupt handling
│   ├── pmm / paging      # Physical memory and paging
│   ├── heap              # Kernel heap
│   ├── thread / sched    # Threads and scheduling
│   ├── ipc               # Inter-process communication
│   ├── syscall           # Syscall dispatch (int 0x80)
│   ├── elf_loader        # ELF loader
│   ├── ata / part / block_cache / nxfs   # Disk and filesystem
│   ├── vfs / ramfs       # Virtual filesystem and in-memory filesystem
│   └── kbd / serial      # I/O drivers
├── user/                 # User-space program (shell)
│   ├── main.c            # Shell implementation
│   ├── start.S           # User-space entry
│   ├── syscall.h         # User-space syscall wrappers
│   └── user.ld           # User-space linker script
├── tools/
│   └── mkkernel.c        # Prepends a 512-byte header ("NEXK" + size) to the raw kernel image
├── linker.ld             # Kernel linker script (load address 0x10000)
├── grub.cfg              # GRUB Multiboot configuration
├── Makefile              # Build, images, and run targets
└── LICENSE
```

---

## Build Requirements

The following tools are required:

| Tool | Purpose |
| --- | --- |
| `gcc` with 32-bit support (`gcc-multilib` on Debian/Ubuntu) | Compiles the kernel and user program (`-m32`) |
| `binutils` (`objcopy`) | Produces the raw binary |
| `nasm` | Assembles the bootloader |
| `grub-mkrescue` (`grub-pc-bin`, `xorriso`) | Builds the bootable ISO |
| `qemu-system-i386` | Runs and tests the system |
| `make` | Build system |

Install on Debian / Ubuntu:

```bash
sudo apt install build-essential gcc-multilib nasm make \
    grub-pc-bin grub-common xorriso qemu-system-x86
```

---

## Build & Run

```bash
# Build the kernel, ISO, and disk images
make

# Boot from the hard-disk images using the custom bootloader
# (images/disk.img is the system disk, images/disk2.img is the second disk)
make run

# Boot from the GRUB ISO (opens a display window for interactive use)
make run-iso

# Clean
make clean        # removes build/
make clean-all    # removes build/ and images/
```

Artifacts:

- `build/NexOS-NEXT.elf` — kernel ELF
- `build/NexOS-NEXT.iso` — bootable ISO (GRUB)
- `images/disk.img` — 64 MB system disk (boot area, kernel, NXFS)
- `images/disk2.img` — 16 MB secondary disk

`make run` launches QEMU with `-m 64M -display none -serial stdio`, so kernel output is echoed to the terminal over serial; `make run-iso` opens a QEMU display window for keyboard interaction.

---

## Boot Flow

NexOS-NEXT supports two boot paths, both ending at the kernel entry point `_start`:

**Path 1: Custom bootloader (hard-disk boot)**

1. **stage1** (MBR, LBA 0): reads 16 sectors starting at LBA 1 into `0x7E00`, verifies the `SXN2` magic, then jumps.
2. **stage2** (LBA 1..16): reads the 512-byte kernel header at LBA 17, verifies the `NEXK` magic and size; loads the kernel in chunks to `0x10000`; installs a temporary GDT, enters protected mode, and jumps to `0x10000`.
3. **Kernel `_start`**: sets up the stack, clears BSS, and calls `kmain`.

**Path 2: GRUB / Multiboot (ISO boot)**

GRUB loads `NexOS-NEXT.elf` via [grub.cfg](grub.cfg) (the kernel carries a Multiboot header) and jumps directly to `_start`.

**Kernel initialization order** ([kernel/kmain.c](kernel/kmain.c)):

```
GDT → IDT → syscall → PMM → Paging → Heap → VFS(ramfs) → Thread → Timer(100Hz)
→ Keyboard → ATA → mount NXFS → switch VFS to NXFS → load init ELF → start scheduler
```

---

## Disk Image Layout

Low-level layout of `images/disk.img` (64 MB):

| LBA | Contents |
| --- | --- |
| 0 | stage1 (MBR, 512 bytes) |
| 1 – 16 | stage2 |
| 17 | Kernel header (`NEXK` magic + kernel size) |
| 18 – … | Raw kernel binary (loaded in chunks) |
| 2048 (1 MB) onward | NXFS partition (superblock + FAT + data blocks) |

NXFS starts at LBA 2048, keeping clear of the boot area and the kernel region.

---

## Memory Layout

- The kernel is linked and loaded at physical address **`0x10000`** (see [linker.ld](linker.ld)).
- The bootloader places the kernel contents starting at `0x10000`.
- The kernel stack is a 16 KB region in `.bss` (see [kernel/entry.S](kernel/entry.S)).
- Each thread has its own 32 KB stack (`STACK_SIZE`, see [kernel/thread.h](kernel/thread.h)).
- System calls validate user-supplied pointers against the user address range and reject out-of-range addresses.

---

## System Calls

User mode traps into the kernel via `int 0x80`. The call number goes in `EAX` and arguments are passed in `EBX`, `ECX`, `EDX`, `ESI`, `EDI`. Dispatch logic is in [kernel/syscall.c](kernel/syscall.c); user-space wrappers are in [user/syscall.h](user/syscall.h).

| # | Name | Description |
| --- | --- | --- |
| 1 | `print` | Print a string |
| 2 | `exit` | Terminate the current thread |
| 3 | `send` | Send an IPC message |
| 4 | `recv` | Receive an IPC message |
| 5 | `getid` | Get the current thread ID |
| 6 | `getchar` | Read one key (blocking) |
| 7 | `putchar` | Write a single character |
| 8 | `open` | Open/create a file |
| 9 | `close` | Close a file descriptor |
| 10 | `read` | Read from a file |
| 11 | `write` | Write to a file |
| 12 | `readdir` | Read a directory entry |
| 13 | `mkdir` | Create a directory |
| 14 | `unlink` | Delete a file/directory |
| 15 | `part_list` | Read the partition table |
| 16 | `part_mkp` | Write a single partition entry |

Open flags: `O_RDONLY` `0x0000`, `O_WRONLY` `0x0001`, `O_RDWR` `0x0002`, `O_CREAT` `0x0100`, `O_TRUNC` `0x0200`.

---

## Shell Commands

After boot, the kernel loads the embedded init ELF and enters the shell ([user/main.c](user/main.c)). Supported commands:

```
help              Show help
clear             Clear the screen
pwd               Print the current directory
cd <dir>          Change directory
tid               Print the current thread ID
echo XXX          Print text
ls [path]         List a directory
cat <file>        Print file contents
mkdir <dir>       Create a directory
touch <file>      Create an empty file
write <f> <txt>   Write text to a file
rm <file>         Delete a file
rm -r <dir>       Recursively delete a directory
rmdir <dir>       Delete an empty directory
part [drive]      List the partition table
mkpart D I T S C  Create partition I on drive D: type T, start LBA S, sectors C
exit              Exit the shell
```

---

## The NXFS Filesystem

NXFS is the project's self-designed on-disk filesystem, using FAT-like chained allocation (see [kernel/nxfs.c](kernel/nxfs.c)):

- **Superblock**: magic `0x5346584E` ("NXFS"), version, block size, total blocks, FAT and data-area locations, root block.
- **Blocks**: 8 sectors each (4 KB), up to `8192` blocks.
- **FAT**: tracks block chains; `0x00000000` means free, `0xFFFFFFFF` means end-of-chain.
- **Directory entry**: 32 bytes, holding a 24-byte filename, type, size, and first block.
- **Persistence**: changes are written back to the FAT and directory entries, with block-cache flushing.

The kernel initializes VFS on ramfs, then detects and mounts NXFS and switches the root filesystem to disk via `vfs_use_nxfs()`. VFS abstracts different filesystems through `fs_driver_t` ([kernel/vfs.h](kernel/vfs.h)).

---

## Known Limitations

- i386 only, uniprocessor, no SMP, no preemptive priorities (simple round-robin scheduling).
- NXFS is fixed at LBA 2048 (1 MB into the disk); a blank disk or an invalid superblock triggers auto-formatting.
- `make run` uses `-display none`, which is mainly convenient for reading serial output; use `make run-iso` for interactive operation.
- Some debug output remains in the kernel sources (e.g. `BEFORE kbd_init` / `AFTER kbd_init`).

---

## License

This project is released under the [Apache License 2.0](LICENSE).
