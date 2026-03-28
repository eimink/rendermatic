#!/bin/sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="${SCRIPT_DIR}/output"

# Use podman, fall back to docker
if command -v podman >/dev/null 2>&1; then
    CTR=podman
elif command -v docker >/dev/null 2>&1; then
    CTR=docker
else
    echo "Error: podman or docker required"
    exit 1
fi

TARGET="${1:-rpi}"
IMAGE_SIZE_MB="${2:-512}"

case "$TARGET" in
    rpi)
        PLATFORM="linux/arm64"
        ARCH_LABEL="aarch64"
        ;;
    generic-arm64|vm-arm64)
        PLATFORM="linux/arm64"
        ARCH_LABEL="aarch64"
        ;;
    x86_64|amd64|generic-amd64)
        PLATFORM="linux/amd64"
        ARCH_LABEL="x86_64"
        TARGET="x86_64"
        ;;
    *)
        echo "Usage: $0 <target> [image_size_mb]"
        echo ""
        echo "Targets:"
        echo "  rpi            Raspberry Pi 3/4/5 (default)"
        echo "  generic-arm64  Generic aarch64 VM (UTM, QEMU, Parallels)"
        echo "  x86_64         x86_64 (PC, VirtualBox, VMware)"
        echo ""
        echo "Options:"
        echo "  image_size_mb  Total image size in MB (default: 512)"
        exit 1
        ;;
esac

IMAGE_NAME="rendermatic-${TARGET}"
OUTPUT_FILE="${OUTPUT_DIR}/${IMAGE_NAME}.img"

echo "=== Rendermatic Image Builder ==="
echo "Container runtime: ${CTR}"
echo "Target:            ${TARGET} (${PLATFORM})"
echo "Image size:        ${IMAGE_SIZE_MB}MB"
echo "Output:            ${OUTPUT_FILE}.gz"
echo ""

mkdir -p "$OUTPUT_DIR"

# --- Step 1: Build rendermatic rootfs ---
echo "--- Building rootfs ---"

$CTR build \
    --platform "$PLATFORM" \
    --file "${SCRIPT_DIR}/Dockerfile" \
    --tag "rendermatic-rootfs:${ARCH_LABEL}" \
    "$PROJECT_DIR"

# --- Step 2: Export rootfs ---
echo "--- Exporting rootfs ---"

ROOTFS_TAR="${OUTPUT_DIR}/rootfs-${ARCH_LABEL}.tar"
CONTAINER_ID=$($CTR create --platform "$PLATFORM" "rendermatic-rootfs:${ARCH_LABEL}" /bin/true)
$CTR export "$CONTAINER_ID" > "$ROOTFS_TAR"
$CTR rm "$CONTAINER_ID" >/dev/null

echo "Rootfs exported: $(du -h "$ROOTFS_TAR" | cut -f1)"

# --- Step 3: Create disk image ---
echo "--- Creating disk image ---"

$CTR run --rm --privileged \
    --platform "$PLATFORM" \
    -v "${OUTPUT_DIR}:/output:z" \
    -v "${SCRIPT_DIR}/mkimage.sh:/mkimage.sh:ro,z" \
    alpine:3.21 \
    sh -c "apk add --no-cache e2fsprogs dosfstools mtools util-linux alpine-keys && sh /mkimage.sh /output/rootfs-${ARCH_LABEL}.tar /output/${IMAGE_NAME}.img ${IMAGE_SIZE_MB} ${TARGET}"

rm -f "$ROOTFS_TAR"

# --- Step 4: Compress ---
echo "--- Compressing image ---"
gzip -f "$OUTPUT_FILE"

echo ""
echo "=== Done ==="
echo "Image: ${OUTPUT_FILE}.gz ($(du -h "${OUTPUT_FILE}.gz" | cut -f1))"
echo ""
echo "Flash to SD card / create VM disk:"
echo "  gunzip -c ${OUTPUT_FILE}.gz | sudo dd of=/dev/sdX bs=4M status=progress"
echo ""
echo "After flashing, mount the data partition (p3) to add your SSH key:"
echo "  mount /dev/sdX3 /mnt"
echo "  mkdir -p /mnt/ssh"
echo "  cp ~/.ssh/id_ed25519.pub /mnt/ssh/authorized_keys"
echo "  umount /mnt"
