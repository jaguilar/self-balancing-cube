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

# 2. Install Node.js 22 LTS (pi.dev requires Node.js >= 22.19.0) and pi.dev agent
RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && npm install -g --ignore-scripts @earendil-works/pi-coding-agent \
    && rm -rf /var/lib/apt/lists/*

# 3. Install Jujutsu (jj) VCS binary from GitHub releases
ARG JJ_VERSION="v0.46.0"
RUN set -eux; \
    ARCH="$(uname -m)"; \
    case "${ARCH}" in \
        x86_64) JJ_ARCH="x86_64-unknown-linux-musl" ;; \
        aarch64) JJ_ARCH="aarch64-unknown-linux-musl" ;; \
        *) echo "Unsupported architecture for jj: ${ARCH}" && exit 1 ;; \
    esac; \
    curl -fsSL "https://github.com/jj-vcs/jj/releases/download/${JJ_VERSION}/jj-${JJ_VERSION}-${JJ_ARCH}.tar.gz" -o /tmp/jj.tar.gz; \
    mkdir -p /tmp/jj-extract; \
    tar -xzf /tmp/jj.tar.gz -C /tmp/jj-extract; \
    find /tmp/jj-extract -type f -name jj -exec mv {} /usr/local/bin/jj \;; \
    chmod +x /usr/local/bin/jj; \
    rm -rf /tmp/jj*; \
    jj --version; \
    mkdir -p /etc/bash_completion.d; \
    jj util completion bash > /etc/bash_completion.d/jj

# 4. Install Bazelisk (provides bazel CLI launcher)
RUN set -eux; \
    ARCH="$(uname -m)"; \
    case "${ARCH}" in \
        x86_64) BAZELISK_ARCH="amd64" ;; \
        aarch64) BAZELISK_ARCH="arm64" ;; \
        *) echo "Unsupported architecture for bazelisk: ${ARCH}" && exit 1 ;; \
    esac; \
    curl -fsSL "https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-${BAZELISK_ARCH}" -o /usr/local/bin/bazel; \
    chmod +x /usr/local/bin/bazel; \
    ln -sf /usr/local/bin/bazel /usr/local/bin/bazelisk

# 5. Install Buildifier (Bazel build file formatter and linter)
ARG BUILDIFIER_VERSION="v10.1.0"
RUN set -eux; \
    ARCH="$(uname -m)"; \
    case "${ARCH}" in \
        x86_64) BUILDIFIER_ARCH="amd64" ;; \
        aarch64) BUILDIFIER_ARCH="arm64" ;; \
        *) echo "Unsupported architecture for buildifier: ${ARCH}" && exit 1 ;; \
    esac; \
    curl -fsSL "https://github.com/bazel-contrib/buildtools/releases/download/${BUILDIFIER_VERSION}/buildifier-linux-${BUILDIFIER_ARCH}" -o /usr/local/bin/buildifier; \
    chmod +x /usr/local/bin/buildifier; \
    buildifier --version

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

