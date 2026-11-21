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
    # OpenOCD build and USB/adapter dependencies
    autoconf \
    automake \
    libtool \
    pkg-config \
    texinfo \
    libusb-1.0-0-dev \
    libhidapi-dev \
    libftdi1-dev \
    # Clangd & formatting tools for C/C++ development
    clangd \
    clang-format \
    # Python 3 and dependencies
    python3 \
    python3-pip \
    python3-venv \
    python3-dev \
    udev \
    bubblewrap \
    && rm -rf /var/lib/apt/lists/*

RUN apt update && apt install ngspice -y  --no-install-recommends && rm -rf /var/lib/apt/lists/*

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

RUN rm -f /etc/sudoers.d/vscode

# Build and install STMicroelectronics fork of OpenOCD (supports STM32C5x)
RUN git clone --depth 1 --branch openocd-cubeide-r7 https://github.com/STMicroelectronics/OpenOCD.git /tmp/openocd \
    && cd /tmp/openocd \
    && git submodule update --init jimtcl \
    && ./bootstrap nosubmodule \
    && ./configure --prefix=/usr/local \
        --enable-stlink \
        --enable-cmsis-dap \
        --enable-ftdi \
        --disable-internal-libjaylink \
        --disable-werror \
    && make -j"$(nproc)" \
    && make install \
    && rm -rf /tmp/openocd

# Install STM32C5 flash loaders (.xldr) and register them in stm32c5x.cfg
RUN mkdir -p /usr/local/share/openocd/loaders \
    && curl -fsSL "https://raw.githubusercontent.com/STMicroelectronics/stm32c5xx-dfp/HEAD/Flash/STM32C5%5B34%5Dx.xldr" -o /usr/local/share/openocd/loaders/STM32C534x.xldr \
    && curl -fsSL "https://raw.githubusercontent.com/STMicroelectronics/stm32c5xx-dfp/HEAD/Flash/STM32C5%5B56%5Dx.xldr" -o /usr/local/share/openocd/loaders/STM32C556x.xldr \
    && curl -fsSL "https://raw.githubusercontent.com/STMicroelectronics/stm32c5xx-dfp/HEAD/Flash/STM32C5%5B9A%5Dx.xldr" -o /usr/local/share/openocd/loaders/STM32C59Ax.xldr \
    && python3 -c 'import pathlib, re; p = pathlib.Path("/usr/local/share/openocd/scripts/target/stm32c5x.cfg"); s = p.read_text(); r, n1 = re.subn(r"set dev_id_loader\s*\{\s*\}", "set dev_id_loader {\n    0x44E \"/usr/local/share/openocd/loaders/STM32C556x.xldr\"\n    0x44F \"/usr/local/share/openocd/loaders/STM32C534x.xldr\"\n    0x45A \"/usr/local/share/openocd/loaders/STM32C59Ax.xldr\"\n}", s, count=1); assert n1 == 1, "Failed to patch dev_id_loader in stm32c5x.cfg"; r, n2 = re.subn(r"\$_CHIPNAME\.cpu configure -event examine-end \{", "$_CHIPNAME.cpu configure -event examine-end {\n    global dev_id_loader\n    set _CHIPNAME [stm32c5x_get_chipname]", r, count=1); assert n2 == 1, "Failed to patch examine-end in stm32c5x.cfg"; p.write_text(r)'

# 8. Configure user environment
USER vscode
ENV PATH="/home/vscode/.local/bin:${PATH}"

