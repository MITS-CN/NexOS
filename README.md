# NexOS-NEXT

> 一个从零开始、用 C 与 x86 汇编编写的 32 位操作系统内核（`NexOS-NEXT` 分支）。

NexOS-NEXT 包含自举引导、内存管理、分页、多线程调度、进程间通信、系统调用、用户态（ring 3）、ELF 加载、ATA 磁盘驱动、分区表读写，以及一个自研的磁盘文件系统 **NXFS**。内核启动后会加载内嵌的 init 程序，进入一个可交互的 Shell。

- **架构**：i386（32 位保护模式）
- **语言**：C（`-std=gnu99`）+ GNU/NASM 汇编
- **许可证**：Apache License 2.0（见 [LICENSE](LICENSE)）

---

## 目录

- [特性](#特性)
- [目录结构](#目录结构)
- [构建依赖](#构建依赖)
- [构建与运行](#构建与运行)
- [启动流程](#启动流程)
- [磁盘镜像布局](#磁盘镜像布局)
- [内存布局](#内存布局)
- [系统调用](#系统调用)
- [Shell 命令](#shell-命令)
- [NXFS 文件系统](#nxfs-文件系统)
- [已知限制](#已知限制)
- [许可证](#许可证)

---

## 特性

| 模块 | 说明 | 位置 |
| --- | --- | --- |
| 引导 | 自写两段式引导器（MBR + stage2），另支持 GRUB/Multiboot ISO 引导 | [boot/](boot/) |
| GDT / TSS | 保护模式段描述符，ring 0/ring 3，TSS 用于用户态陷入内核栈切换 | [kernel/gdt.c](kernel/gdt.c) |
| IDT / 中断 | 中断描述符表、ISR 桩、异常处理 | [kernel/idt.c](kernel/idt.c)、[kernel/isr.c](kernel/isr.c) |
| 物理内存 | 页框分配器（PMM） | [kernel/pmm.c](kernel/pmm.c) |
| 分页 | 二级页表，虚拟地址映射 | [kernel/paging.c](kernel/paging.c) |
| 堆 | 内核 `kmalloc` / `kfree` | [kernel/heap.c](kernel/heap.c) |
| 线程与调度 | 线程创建、上下文切换、轮转调度，定时器 **100 Hz** | [kernel/thread.c](kernel/thread.c)、[kernel/sched.c](kernel/sched.c) |
| IPC | 线程间消息传递（`send` / `recv`） | [kernel/ipc.c](kernel/ipc.c) |
| 系统调用 | 通过 `int 0x80` 提供 16 个系统调用 | [kernel/syscall.c](kernel/syscall.c) |
| 用户态 | ring 3 执行，用户内存范围校验 | [kernel/usermode.S](kernel/usermode.S) |
| ELF 加载 | 解析并把 init ELF 载入用户空间 | [kernel/elf_loader.c](kernel/elf_loader.c) |
| 设备驱动 | VGA 文本控制台、串口（COM1, 38400 8N1）、PS/2 键盘、ATA PIO | [kernel/serial.c](kernel/serial.c)、[kernel/kbd.c](kernel/kbd.c)、[kernel/ata.c](kernel/ata.c) |
| 块缓存 | 磁盘块的读写缓存 | [kernel/block_cache.c](kernel/block_cache.c) |
| 文件系统 | VFS 抽象层 + ramfs + **NXFS**（磁盘持久化） | [kernel/vfs.c](kernel/vfs.c)、[kernel/ramfs.c](kernel/ramfs.c)、[kernel/nxfs.c](kernel/nxfs.c) |
| 分区表 | 读取/写入 MBR 主分区表（4 项） | [kernel/part.c](kernel/part.c) |
| 用户程序 | 内嵌 init Shell（`NexOS-NEXT Shell v0.5`） | [user/main.c](user/main.c) |

---

## 目录结构

```
NexOS-NEXT/
├── boot/                 # 自写引导器
│   ├── stage1.asm        # 512 字节 MBR，加载 stage2
│   └── stage2.asm        # 读取内核、切换保护模式并跳转
├── kernel/               # 内核源码
│   ├── entry.S           # 内核入口 _start（清 BSS、调用 kmain）
│   ├── kmain.c           # 内核主流程与 VGA 控制台
│   ├── gdt / idt / isr   # 描述符表与中断处理
│   ├── pmm / paging      # 物理内存与分页
│   ├── heap              # 内核堆
│   ├── thread / sched    # 线程与调度
│   ├── ipc               # 进程间通信
│   ├── syscall           # 系统调用分发（int 0x80）
│   ├── elf_loader        # ELF 加载器
│   ├── ata / part / block_cache / nxfs   # 磁盘与文件系统
│   ├── vfs / ramfs       # 虚拟文件系统与内存文件系统
│   └── kbd / serial      # 输入输出驱动
├── user/                 # 用户态程序（Shell）
│   ├── main.c            # Shell 实现
│   ├── start.S           # 用户态入口
│   ├── syscall.h         # 用户态 syscall 包装
│   └── user.ld           # 用户态链接脚本
├── tools/
│   └── mkkernel.c        # 为裸内核镜像添加 512 字节头部（"NEXK" + size）
├── linker.ld             # 内核链接脚本（加载地址 0x10000）
├── grub.cfg              # GRUB Multiboot 配置
├── Makefile              # 构建、镜像与运行
└── LICENSE
```

---

## 构建依赖

需要以下工具：

| 工具 | 用途 |
| --- | --- |
| `gcc` + 32 位支持（`gcc-multilib`，Debian/Ubuntu） | 编译内核与用户程序（`-m32`） |
| `binutils`（`objcopy`） | 生成裸二进制 |
| `nasm` | 汇编引导器 |
| `grub-mkrescue`（`grub-pc-bin`、`xorriso`） | 生成可引导 ISO |
| `qemu-system-i386` | 运行测试 |
| `make` | 构建 |

Debian / Ubuntu 安装示例：

```bash
sudo apt install build-essential gcc-multilib nasm make \
    grub-pc-bin grub-common xorriso qemu-system-x86
```

---

## 构建与运行

```bash
# 构建内核、ISO 与磁盘镜像
make

# 使用自写引导器，从硬盘镜像启动
#（images/disk.img 为系统盘，images/disk2.img 为第二块盘）
make run

# 使用 GRUB ISO 启动（带显示窗口，便于交互）
make run-iso

# 清理
make clean        # 清理 build/
make clean-all    # 清理 build/ 与 images/
```

产物：

- `build/NexOS-NEXT.elf` — 内核 ELF
- `build/NexOS-NEXT.iso` — 可引导 ISO（GRUB）
- `images/disk.img` — 64 MB 系统盘（含引导区、内核、NXFS）
- `images/disk2.img` — 16 MB 备用盘

`make run` 以 `-m 64M -display none -serial stdio` 启动，内核输出通过串口回显到终端；`make run-iso` 会打开 QEMU 显示窗口，便于使用键盘交互。

---

## 启动流程

NexOS-NEXT 支持两条引导路径，最终都进入内核入口 `_start`：

**路径一：自写引导器（硬盘启动）**

1. **stage1**（MBR，LBA 0）：读取 LBA 1 起的 16 个扇区到 `0x7E00`，校验魔数 `SXN2` 后跳转。
2. **stage2**（LBA 1..16）：读取 LBA 17 的 512 字节内核头，校验 `NEXK` 与大小；分块读取内核到 `0x10000`；建立临时 GDT，开启保护模式并跳转到 `0x10000`。
3. **内核 `_start`**：设置栈、清 BSS、调用 `kmain`。

**路径二：GRUB / Multiboot（ISO 启动）**

GRUB 通过 [grub.cfg](grub.cfg) 载入 `NexOS-NEXT.elf`（内核带 Multiboot 头），直接跳转到 `_start`。

**内核初始化顺序**（[kernel/kmain.c](kernel/kmain.c)）：

```
GDT → IDT → syscall → PMM → Paging → Heap → VFS(ramfs) → Thread → Timer(100Hz)
→ Keyboard → ATA → NXFS 挂载 → 切换 VFS 到 NXFS → 加载 init ELF → 启动调度器
```

---

## 磁盘镜像布局

`images/disk.img`（64 MB）的 low-level 布局：

| LBA | 内容 |
| --- | --- |
| 0 | stage1（MBR，512 字节） |
| 1 – 16 | stage2 |
| 17 | 内核头（`NEXK` 魔数 + 内核大小） |
| 18 – … | 内核裸二进制（分块加载） |
| 2048（1 MB）起 | NXFS 分区（超级块 + FAT + 数据块） |

NXFS 从 LBA 2048 开始，避开引导区与内核区。

---

## 内存布局

- 内核链接并加载到物理地址 **`0x10000`**（见 [linker.ld](linker.ld)）。
- 引导器把内核内容依次放置到 `0x10000` 起。
- 内核栈位于 `.bss` 中的 16 KB 区域（见 [kernel/entry.S](kernel/entry.S)）。
- 每个线程拥有独立的 32 KB 栈（`STACK_SIZE`，见 [kernel/thread.h](kernel/thread.h)）。
- 系统调用会对用户传入的指针做范围校验，拒绝越界的用户地址。

---

## 系统调用

用户态通过 `int 0x80` 陷入内核，调用号放在 `EAX`，参数依次放在 `EBX`、`ECX`、`EDX`、`ESI`、`EDI`。分发逻辑见 [kernel/syscall.c](kernel/syscall.c)，用户态封装见 [user/syscall.h](user/syscall.h)。

| # | 名称 | 说明 |
| --- | --- | --- |
| 1 | `print` | 输出字符串 |
| 2 | `exit` | 结束当前线程 |
| 3 | `send` | 发送 IPC 消息 |
| 4 | `recv` | 接收 IPC 消息 |
| 5 | `getid` | 获取当前线程 ID |
| 6 | `getchar` | 读取一个按键（阻塞） |
| 7 | `putchar` | 输出单个字符 |
| 8 | `open` | 打开/创建文件 |
| 9 | `close` | 关闭文件描述符 |
| 10 | `read` | 读文件 |
| 11 | `write` | 写文件 |
| 12 | `readdir` | 读取目录项 |
| 13 | `mkdir` | 创建目录 |
| 14 | `unlink` | 删除文件/目录 |
| 15 | `part_list` | 读取分区表 |
| 16 | `part_mkp` | 写入单个分区项 |

打开标志：`O_RDONLY` `0x0000`、`O_WRONLY` `0x0001`、`O_RDWR` `0x0002`、`O_CREAT` `0x0100`、`O_TRUNC` `0x0200`。

---

## Shell 命令

内核启动后加载内嵌的 init ELF，进入 Shell（[user/main.c](user/main.c)）。支持：

```
help            显示帮助
clear           清屏
pwd             显示当前目录
cd <dir>        切换目录
tid             显示当前线程 ID
echo XXX        打印文本
ls [path]       列出目录
cat <file>      打印文件内容
mkdir <dir>     创建目录
touch <file>    创建空文件
write <f> <txt> 向文件写入文本
rm <file>       删除文件
rm -r <dir>     递归删除目录
rmdir <dir>     删除空目录
part [drive]    列出分区表
mkpart D I T S C 在磁盘 D 上创建分区 I：类型 T、起始 LBA S、扇区数 C
exit            退出 Shell
```

---

## NXFS 文件系统

NXFS 是本项目自研的磁盘文件系统，采用类 FAT 的链式分配（见 [kernel/nxfs.c](kernel/nxfs.c)）：

- **超级块**：魔数 `0x5346584E`（"NXFS"）、版本、块大小、块总数、FAT 与数据区位置、根块。
- **块**：每块 8 个扇区（4 KB），最多 `8192` 块。
- **FAT**：记录块链，`0x00000000` 表示空闲，`0xFFFFFFFF` 表示链尾。
- **目录项**：32 字节，包含 24 字节文件名、类型、大小与首块号。
- **持久化**：修改会写回 FAT 与目录项，并支持块缓存刷盘。

内核先用 ramfs 初始化 VFS，检测并挂载 NXFS 后通过 `vfs_use_nxfs()` 把根文件系统切换到磁盘。VFS 通过 `fs_driver_t` 抽象不同文件系统（[kernel/vfs.h](kernel/vfs.h)）。

---

## 已知限制

- 仅支持 i386 单核、无 SMP、无抢占优先级（简单轮转调度）。
- NXFS 固定挂在 LBA 2048（磁盘 1 MB 处）；检测到空盘或超级块无效时会自动格式化。
- `make run` 使用 `-display none`，主要便于查看串口输出；交互操作建议使用 `make run-iso`。
- 内核源码中保留了部分调试输出（如 `BEFORE kbd_init` / `AFTER kbd_init`）。

---

## 许可证

本项目基于 [Apache License 2.0](LICENSE) 发布。