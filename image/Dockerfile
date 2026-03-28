FROM alpine:3.21 AS builder

RUN apk add --no-cache \
    git cmake clang clang-dev make gcc g++ \
    libc-dev linux-headers \
    directfb-dev \
    ffmpeg-dev \
    avahi-dev \
    python3 py3-jinja2

WORKDIR /src
COPY . .

RUN rm -rf build && mkdir build && cd build && \
    cmake -DDFB_PURE_ONLY=ON \
          -DPython_EXECUTABLE=$(which python3) \
          -DPython3_EXECUTABLE=$(which python3) \
          .. && \
    cmake --build . --parallel $(nproc)

# --- Minimal runtime image ---
FROM alpine:3.21

RUN apk add --no-cache \
    # Runtime libraries
    directfb \
    ffmpeg-libs \
    libstdc++ \
    # mDNS discovery
    avahi avahi-tools dbus \
    # Network
    dhcpcd openssh-server \
    # System
    openrc busybox-openrc

# Rendermatic binary and assets
COPY --from=builder /src/build/bin/rendermatic /rendermatic/rendermatic
COPY --from=builder /src/build/bin/shaders/ /rendermatic/shaders/
COPY --from=builder /src/build/bin/textures/ /rendermatic/textures/
COPY --from=builder /src/build/bin/config.json /rendermatic/config.json.default

# Overlay: system configuration
COPY image/rootfs/ /

# Enable services
RUN rc-update add networking boot && \
    rc-update add dhcpcd default && \
    rc-update add sshd default && \
    rc-update add dbus default && \
    rc-update add avahi-daemon default && \
    rc-update add rendermatic default && \
    rc-update add data-mount boot && \
    # SSH: key-only auth (keys go on data partition)
    sed -i 's/#PermitRootLogin.*/PermitRootLogin prohibit-password/' /etc/ssh/sshd_config && \
    sed -i 's/#PasswordAuthentication.*/PasswordAuthentication no/' /etc/ssh/sshd_config && \
    # Generate SSH host keys (will be on read-only root, but that's fine)
    ssh-keygen -A && \
    # Create data mount point
    mkdir -p /data && \
    # Make rendermatic executable
    chmod +x /rendermatic/rendermatic

# Default command (for Docker testing, not used in image)
CMD ["/rendermatic/rendermatic"]
