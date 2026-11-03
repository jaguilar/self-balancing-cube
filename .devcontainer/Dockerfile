FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# 1. Install base utilities, build tools, GNU tools, ARM toolchain, GDB, OpenOCD, and Clang tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Basic GNU & CLI tools
    bash \
    bash-completion \
    grep \
    sed \
    gawk \
    coreutils \
    findutils \
    diffutils \
    patch \
    tar \
    gzip \
    bzip2 \
    xz-utils \
    unzip \
    zip \
    less \
    procps \
    tree \
    jq \
    file \
    curl \
    wget \
    ca-certificates \
    gnupg \
    build-essential \
    make \
    cmake \
    ninja-build \
    # Version control
    git \
    git-lfs \
    # ARM embedded toolchain & multiarch GDB
    gcc-arm-none-eabi \
    binutils-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    gdb-multiarch \
    # OpenOCD for STM32 flashing and hardware debugging
    openocd \
    # Clangd & formatting tools for C/C++ development
    clangd \
    clang-format \
    # Python 3 and dependencies
    python3 \
    python3-pip \
    python3-venv \
    python3-dev \
    udev \
    && rm -rf /var/lib/apt/lists/*

# Symlink arm-none-eabi-gdb to gdb-multiarch for tool compatibility
RUN ln -sf /usr/bin/gdb-multiarch /usr/local/bin/arm-none-eabi-gdb

# 6. Install Python dependencies used by scripts in the repository
RUN pip3 install --no-cache-dir --break-system-packages \
    numpy \
    scipy \
    control \
    pyserial

# 7. Add vscode user to dialout and plugdev groups for USB/serial/ST-Link access
RUN usermod -aG dialout,plugdev vscode || true

# 8. Configure user environment
USER vscode
ENV PATH="/home/vscode/.local/bin:${PATH}"

