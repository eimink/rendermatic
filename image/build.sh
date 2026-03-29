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
IMAGE_SIZE_MB="${2:-2048}"

case "$TARGET" in
    rpi)
        PLATFORM="linux/arm64"
        ;;
    generic-arm64|vm-arm64)
        PLATFORM="linux/arm64"
        ;;
    x86_64|amd64|generic-amd64)
        PLATFORM="linux/amd64"
        TARGET="x86_64"
        ;;
    *)
        echo "Usage: $0 <target> [image_size_mb]"
        echo ""
        echo "Targets:"
        echo "  rpi            Raspberry Pi 4/5 (default)"
        echo "  generic-arm64  Generic aarch64 VM (VirtualBox, QEMU)"
        echo "  x86_64         x86_64 PC / VM"
        echo ""
        echo "Options:"
        echo "  image_size_mb  Total image size in MB (default: 2048)"
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
    --build-arg "TARGET=${TARGET}" \
    --file "${SCRIPT_DIR}/Dockerfile" \
    --tag "rendermatic-rootfs:${TARGET}" \
    "$PROJECT_DIR"

# --- Step 2: Export rootfs ---
echo "--- Exporting rootfs ---"

ROOTFS_TAR="${OUTPUT_DIR}/rootfs-${TARGET}.tar"
CONTAINER_ID=$($CTR create --platform "$PLATFORM" "rendermatic-rootfs:${TARGET}" /bin/true)
$CTR export "$CONTAINER_ID" > "$ROOTFS_TAR"
$CTR rm "$CONTAINER_ID" >/dev/null

echo "Rootfs exported: $(du -h "$ROOTFS_TAR" | cut -f1)"

# --- Step 3: Create disk image ---
echo "--- Creating disk image ---"

$CTR run --rm --privileged \
    --platform "$PLATFORM" \
    -v "${OUTPUT_DIR}:/output:z" \
    -v "${SCRIPT_DIR}/mkimage.sh:/mkimage.sh:ro,z" \
    debian:bookworm-slim \
    sh -c "apt-get update -qq && apt-get install -y -qq --no-install-recommends e2fsprogs dosfstools mtools fdisk kmod >/dev/null 2>&1 && sh /mkimage.sh /output/rootfs-${TARGET}.tar /output/${IMAGE_NAME}.img ${IMAGE_SIZE_MB} ${TARGET}"

rm -f "$ROOTFS_TAR"

# --- Step 4: Convert to VM formats (opt-in) ---
VMDK_FILE="${OUTPUT_DIR}/${IMAGE_NAME}.vmdk"

if [ "${VMDK:-0}" = "1" ] && [ "$TARGET" != "rpi" ]; then
    echo "--- Converting to VMDK ---"
    $CTR run --rm \
        --platform "$PLATFORM" \
        -v "${OUTPUT_DIR}:/output:z" \
        debian:bookworm-slim \
        sh -c "apt-get update -qq && apt-get install -y -qq --no-install-recommends qemu-utils >/dev/null 2>&1 && qemu-img convert -f raw -O vmdk /output/${IMAGE_NAME}.img /output/${IMAGE_NAME}.vmdk"
    echo "VMDK: ${VMDK_FILE} ($(du -h "${VMDK_FILE}" | cut -f1))"
fi

# --- Step 5: Compress ---
echo "--- Compressing raw image ---"
gzip -f "$OUTPUT_FILE"

echo ""
echo "=== Done ==="
echo "Raw image: ${OUTPUT_FILE}.gz ($(du -h "${OUTPUT_FILE}.gz" | cut -f1))"

if [ -f "$VMDK_FILE" ]; then
    echo "VMDK image: ${VMDK_FILE} ($(du -h "${VMDK_FILE}" | cut -f1))"
    echo ""
    echo "For VirtualBox / VMware:"
    echo "  Use ${VMDK_FILE} directly as a virtual disk"
fi

echo ""
echo "Flash to SD card / bare metal:"
echo "  gunzip -c ${OUTPUT_FILE}.gz | sudo dd of=/dev/sdX bs=4M status=progress"

echo ""
echo "After flashing, mount the data partition (p3) to add your SSH key:"
echo "  mount /dev/sdX3 /mnt"
echo "  mkdir -p /mnt/ssh"
echo "  cp ~/.ssh/id_ed25519.pub /mnt/ssh/authorized_keys"
echo "  umount /mnt"
