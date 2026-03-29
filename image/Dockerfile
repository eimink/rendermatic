# === Stage 1: Build DirectFB2 from source (not packaged in Debian 12+) ===
FROM debian:trixie-slim AS dfb-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    git ca-certificates \
    build-essential meson ninja-build pkg-config \
    libdrm-dev libfreetype-dev libjpeg-dev libpng-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/directfb2/DirectFB2.git /src/directfb2 && \
    cd /src/directfb2 && \
    meson setup build -Dprefix=/usr -Dneon=false && \
    meson compile -C build && \
    DESTDIR=/dfb-install meson install -C build

# === Stage 2: Build rendermatic ===
FROM debian:trixie-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    git ca-certificates \
    cmake clang make pkg-config \
    libavformat-dev libavcodec-dev libswscale-dev libavutil-dev \
    libavahi-client-dev \
    python3 python3-jinja2 \
    && rm -rf /var/lib/apt/lists/*

# Install DirectFB2 build artifacts
COPY --from=dfb-builder /dfb-install/usr/ /usr/
RUN ldconfig

WORKDIR /src
COPY . .

RUN rm -rf build && mkdir build && cd build && \
    cmake -DDFB_PURE_ONLY=ON \
          -DCMAKE_C_COMPILER=clang \
          -DCMAKE_CXX_COMPILER=clang++ \
          -DPython_EXECUTABLE=$(which python3) \
          -DPython3_EXECUTABLE=$(which python3) \
          .. && \
    cmake --build . --parallel $(nproc)

# === Stage 3: Runtime rootfs ===
FROM debian:trixie-slim

ARG TARGET=generic-arm64

RUN apt-get update && apt-get install -y --no-install-recommends \
    # DirectFB2 runtime deps
    libdrm2 libfreetype6 libjpeg62-turbo libpng16-16t64 \
    # FFmpeg runtime
    libavformat61 libavcodec61 libswscale8 libavutil59 \
    # mDNS discovery
    avahi-daemon avahi-utils libavahi-client3 dbus \
    # Network
    openssh-server iproute2 \
    # Filesystem tools
    e2fsprogs dosfstools \
    # System
    systemd systemd-sysv systemd-resolved sudo kmod \
    # Utilities
    wget ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# DirectFB2 runtime libraries and plugins (not in Debian repos)
COPY --from=dfb-builder /dfb-install/usr/lib/ /usr/lib/
RUN ldconfig

# Configure initramfs modules before kernel install
RUN mkdir -p /etc/initramfs-tools && \
    if [ "$TARGET" = "rpi" ]; then \
        printf "mmc_block\next4\n" > /etc/initramfs-tools/modules; \
    else \
        printf "virtio_pci\nvirtio_blk\nvirtio_net\next4\nnvme\nahci\n" > /etc/initramfs-tools/modules; \
    fi

# Install target-specific kernel
RUN apt-get update && \
    if [ "$TARGET" = "rpi" ]; then \
        apt-get install -y --no-install-recommends \
            linux-image-arm64 raspi-firmware; \
    elif [ "$TARGET" = "x86_64" ]; then \
        apt-get install -y --no-install-recommends \
            linux-image-amd64 initramfs-tools; \
    else \
        apt-get install -y --no-install-recommends \
            linux-image-arm64 initramfs-tools; \
    fi && \
    rm -rf /var/lib/apt/lists/*

# Rendermatic binary and assets
COPY --from=builder /src/build/bin/rendermatic /rendermatic/rendermatic
COPY --from=builder /src/build/bin/shaders/ /rendermatic/shaders/
COPY --from=builder /src/build/bin/media/ /rendermatic/media/
COPY --from=builder /src/build/bin/config.json /rendermatic/config.json.default
COPY --from=builder /src/build/bin/config.json /rendermatic/config.json

# System overlay (systemd units, configs, scripts)
COPY image/rootfs/ /

# Enable services and configure system
RUN systemctl enable rendermatic.service && \
    systemctl enable data-mount.service && \
    systemctl enable hostname-gen.service && \
    systemctl enable ssh.service && \
    systemctl enable avahi-daemon.service && \
    systemctl enable systemd-networkd.service && \
    systemctl enable systemd-resolved.service && \
    # Create render user
    useradd -g render -m -d /data -s /bin/bash render && \
    echo 'render:m4tic!' | chpasswd && \
    echo 'render ALL=(ALL) ALL' > /etc/sudoers.d/render && \
    chmod 440 /etc/sudoers.d/render && \
    # Root: console-only access (SSH login disabled via sshd_config)
    echo 'root:m4tic!' | chpasswd && \
    sed -i 's/#PermitRootLogin.*/PermitRootLogin no/' /etc/ssh/sshd_config && \
    sed -i 's/#PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config && \
    ssh-keygen -A && \
    # Create data mount point
    mkdir -p /data && \
    # Make scripts executable
    chmod +x /rendermatic/rendermatic && \
    chmod +x /usr/local/sbin/data-mount.sh && \
    chmod +x /usr/local/sbin/hostname-gen.sh

CMD ["/rendermatic/rendermatic"]
