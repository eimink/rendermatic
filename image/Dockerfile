# === Stage 1: Build DirectFB2 from source (not packaged in Debian 12+) ===
FROM debian:trixie-slim AS dfb-builder

RUN rm -f /etc/apt/sources.list.d/*.sources && \
    echo 'deb http://deb.debian.org/debian trixie main non-free-firmware' > /etc/apt/sources.list && \
    apt-get update && apt-get install -y --no-install-recommends \
    git ca-certificates \
    build-essential meson ninja-build pkg-config \
    libdrm-dev libgbm-dev libfreetype-dev libjpeg-dev libpng-dev \
    libegl-dev libgles-dev libgl-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/directfb2/DirectFB2.git /src/directfb2 && \
    cd /src/directfb2 && \
    meson setup build -Dprefix=/usr -Dneon=false && \
    meson compile -C build && \
    DESTDIR=/dfb-install meson install -C build

# Install DirectFB2 into system paths so DirectFBGL-EGL can find headers
RUN cp -a /dfb-install/usr/lib/* /usr/lib/ && \
    cp -a /dfb-install/usr/include/* /usr/include/ && \
    ldconfig

# Build DirectFBGL-EGL (OpenGL interface for DirectFB2)
RUN git clone --depth 1 https://github.com/directfb2/DirectFBGL-EGL.git /src/directfbgl && \
    cd /src/directfbgl && \
    meson setup build -Dprefix=/usr && \
    meson compile -C build && \
    DESTDIR=/dfb-install meson install -C build

# === Stage 2: Build rendermatic ===
FROM debian:trixie-slim AS builder

RUN rm -f /etc/apt/sources.list.d/*.sources && \
    echo 'deb http://deb.debian.org/debian trixie main non-free-firmware' > /etc/apt/sources.list && \
    apt-get update && apt-get install -y --no-install-recommends \
    git ca-certificates \
    cmake clang make pkg-config \
    libavformat-dev libavcodec-dev libswscale-dev libavutil-dev \
    libavahi-client-dev \
    libegl-dev libgles-dev libgl-dev \
    libdrm-dev libgbm-dev \
    python3 python3-jinja2 \
    && rm -rf /var/lib/apt/lists/*

# Install DirectFB2 build artifacts
COPY --from=dfb-builder /dfb-install/usr/ /usr/
RUN ldconfig

WORKDIR /src
COPY . .

RUN rm -rf build && mkdir build && cd build && \
    cmake -DDFB_ONLY=ON \
          -DCMAKE_C_COMPILER=clang \
          -DCMAKE_CXX_COMPILER=clang++ \
          -DPython_EXECUTABLE=$(which python3) \
          -DPython3_EXECUTABLE=$(which python3) \
          .. && \
    cmake --build . --parallel $(nproc)

# === Stage 3: Runtime rootfs ===
FROM debian:trixie-slim

ARG TARGET=generic-arm64

RUN rm -f /etc/apt/sources.list.d/*.sources && \
    echo 'deb http://deb.debian.org/debian trixie main non-free-firmware' > /etc/apt/sources.list && \
    apt-get update && apt-get install -y --no-install-recommends \
    # DirectFB2 runtime deps
    libdrm2 libfreetype6 libjpeg62-turbo libpng16-16t64 \
    # OpenGL/EGL runtime + Mesa DRI drivers (Intel, AMD, Broadcom, llvmpipe)
    libegl1 libgl1 libgles2 libgl1-mesa-dri libgbm1 \
    # FFmpeg runtime
    libavformat61 libavcodec61 libswscale8 libavutil59 \
    # VA-API hardware video decode (Intel/AMD)
    libva2 libva-drm2 mesa-va-drivers intel-media-va-driver \
    # mDNS discovery
    avahi-daemon avahi-utils libavahi-client3 dbus \
    # Network
    openssh-server iproute2 wpasupplicant \
    # Filesystem tools (fdisk + parted for data partition expansion)
    e2fsprogs dosfstools fdisk parted \
    # System
    systemd systemd-sysv systemd-resolved sudo kmod \
    # Firmware (WiFi, Bluetooth, GPU)
    firmware-linux-free firmware-iwlwifi firmware-amd-graphics firmware-misc-nonfree \
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
# DRACUT_NO_XATTR suppresses harmless xattr warnings from mkinitramfs in containers
RUN export DRACUT_NO_XATTR=1 && apt-get update && \
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

# Version stamp (set via build arg, defaults to "dev")
ARG BUILD_VERSION=dev
RUN echo "$BUILD_VERSION" > /etc/rendermatic-version

# Enable services and configure system
RUN systemctl enable rendermatic.service && \
    systemctl enable data-expand.service && \
    systemctl enable data-mount.service && \
    systemctl enable hostname-gen.service && \
    systemctl enable ssh.service && \
    systemctl enable avahi-daemon.service && \
    systemctl enable systemd-networkd.service && \
    systemctl enable systemd-resolved.service && \
    systemctl enable fb-fallback.service && \
    systemctl enable wpa-supplicant-rendermatic.service && \
    systemctl enable rendermatic-update.timer && \
    # Create render user
    groupadd -f render && \
    useradd -g render -m -d /data -s /bin/bash render && \
    echo 'render:m4tic!' | chpasswd && \
    echo 'render ALL=(ALL) ALL' > /etc/sudoers.d/render && \
    chmod 440 /etc/sudoers.d/render && \
    # Root: console-only access (SSH login disabled via sshd_config)
    echo 'root:m4tic!' | chpasswd && \
    sed -i 's/#PermitRootLogin.*/PermitRootLogin no/' /etc/ssh/sshd_config && \
    sed -i 's/#PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config && \
    ssh-keygen -A && \
    # Disable all console logins (rendermatic owns the framebuffer)
    systemctl mask getty@.service && \
    systemctl mask getty.target && \
    systemctl mask serial-getty@.service && \
    # Fast boot: don't block on network, disable unused services
    systemctl mask systemd-networkd-wait-online.service && \
    systemctl mask apt-daily.timer && \
    systemctl mask apt-daily-upgrade.timer && \
    systemctl mask e2scrub_all.timer && \
    systemctl mask fstrim.timer && \
    # Create data mount point
    mkdir -p /data && \
    # Make scripts executable
    chmod +x /rendermatic/rendermatic && \
    chmod +x /usr/local/sbin/data-mount.sh && \
    chmod +x /usr/local/sbin/hostname-gen.sh && \
    chmod +x /usr/local/sbin/fb-fallback.sh && \
    chmod +x /usr/local/sbin/wifi-start.sh && \
    chmod +x /usr/local/sbin/data-expand.sh && \
    chmod +x /usr/local/sbin/ota-update.sh

CMD ["/rendermatic/rendermatic"]
