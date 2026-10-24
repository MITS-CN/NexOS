#!/usr/bin/env bash

set -e

# 检查是否在 WSL 环境下
if grep -qi microsoft /proc/version 2>/dev/null; then
    echo "[提示] 检测到 WSL 环境。请确保你的 WSL 是 WSL2（某些 QEMU 功能需要）。"
fi

# 获取 sudo 权限（如果不是 root）
if [ "$EUID" -ne 0 ]; then
    echo "[提示] 需要 sudo 权限来安装系统包。"
    SUDO="sudo"
else
    SUDO=""
fi

# 检测发行版
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
else
    echo "[错误] 无法检测系统发行版（缺少 /etc/os-release）。请手动安装依赖。"
    exit 1
fi

echo "[信息] 检测到系统：$OS"

# 通用依赖列表（名字可能因发行版而异，以下在 case 里调整）
install_debian() {
    echo "[信息] 正在使用 apt 安装依赖..."
    $SUDO apt update
    $SUDO apt install -y \
        build-essential \
        nasm \
        xorriso \
        mtools \
        gcc-multilib \
        grub-pc-bin \
        qemu-system-x86
}

install_arch() {
    echo "[信息] 正在使用 pacman 安装依赖..."
    # 确保启用了 multilib 仓库
    if ! grep -q "^\[multilib\]" /etc/pacman.conf; then
        echo "[警告] 你的 Arch 系统未启用 multilib 仓库。"
        echo "请先编辑 /etc/pacman.conf，取消 [multilib] 相关的注释，然后重新运行此脚本。"
        exit 1
    fi
    $SUDO pacman -Sy --noconfirm
    $SUDO pacman -S --needed --noconfirm \
        base-devel \
        nasm \
        xorriso \
        mtools \
        lib32-gcc-libs \
        grub \
        qemu-system-x86
}

install_fedora() {
    echo "[信息] 正在使用 dnf 安装依赖..."
    $SUDO dnf install -y \
        gcc \
        gcc-c++ \
        make \
        nasm \
        xorriso \
        mtools \
        glibc-devel.i686 \
        grub2-tools-extra \
        qemu-system-x86
}

install_centos() {
    echo "[信息] 正在使用 yum 安装依赖..."
    $SUDO yum install -y \
        gcc \
        gcc-c++ \
        make \
        nasm \
        xorriso \
        mtools \
        glibc-devel.i686 \
        grub2-tools-extra \
        qemu-system-x86
}

case "$OS" in
    ubuntu|debian|linuxmint|pop)
        install_debian
        ;;
    arch|manjaro)
        install_arch
        ;;
    fedora)
        install_fedora
        ;;
    centos|rhel|rocky|alma)
        install_centos
        ;;
    *)
        echo "[错误] 尚未支持的系统：$OS"
        echo "请参考以下包名手动安装："
        echo "  - 32位 gcc (gcc-multilib / glibc-devel.i686)"
        echo "  - nasm"
        echo "  - xorriso"
        echo "  - mtools"
        echo "  - grub-mkrescue (grub-pc-bin / grub)"
        echo "  - qemu-system-i386 (qemu-system-x86)"
        exit 1
        ;;
esac

echo ""
echo "依赖安装完成！"
echo "现在你可以运行：make clean-all && make"