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
# A/B layout needs more space (2x root + larger boot)
if [ "${AB:-0}" = "1" ]; then
    IMAGE_SIZE_MB="${2:-3072}"
else
    IMAGE_SIZE_MB="${2:-2048}"
fi

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
        echo ""
        echo "Environment:"
        echo "  AB=1           Build with A/B dual-root partition layout"
        echo "  DATA_MB=N      Data partition size in MB (default: 128, expands on first boot)"
        echo "  VMDK=1         Also produce VMDK image (non-RPi targets)"
        echo ""
        echo "Examples:"
        echo "  $0 x86_64                          # standard 2GB image"
        echo "  AB=1 $0 x86_64                     # A/B layout, 2GB"
        echo "  AB=1 DATA_MB=4096 $0 x86_64 6144   # A/B with 4GB data, 6GB image"
        exit 1
        ;;
esac

IMAGE_NAME="rendermatic-${TARGET}"
OUTPUT_FILE="${OUTPUT_DIR}/${IMAGE_NAME}.img"

# A/B layout support
AB_LAYOUT="${AB:-0}"

echo "=== Rendermatic Image Builder ==="
echo "Container runtime: ${CTR}"
echo "Target:            ${TARGET} (${PLATFORM})"
echo "Image size:        ${IMAGE_SIZE_MB}MB"
[ "$AB_LAYOUT" = "1" ] && echo "Layout:            A/B dual-root"
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
    sh -c "apt-get update -qq && apt-get install -y -qq --no-install-recommends e2fsprogs dosfstools mtools fdisk kmod >/dev/null 2>&1 && AB_LAYOUT=${AB_LAYOUT} DATA_MB=${DATA_MB:-128} sh /mkimage.sh /output/rootfs-${TARGET}.tar /output/${IMAGE_NAME}.img ${IMAGE_SIZE_MB} ${TARGET}"

[ "$AB_LAYOUT" != "1" ] && rm -f "$ROOTFS_TAR"

# --- Step 4: Export OTA artifacts (A/B only) ---
if [ "$AB_LAYOUT" = "1" ]; then
    echo "--- Exporting OTA artifacts ---"

    # Extract the root partition image from the disk image for OTA serving.
    # Re-use the rootfs tarball to create a standalone root partition image.
    ROOTFS_TAR="${OUTPUT_DIR}/rootfs-${TARGET}.tar"
    OTA_ROOTFS="${OUTPUT_DIR}/${IMAGE_NAME}-rootfs.img"
    OTA_BOOT="${OUTPUT_DIR}/${IMAGE_NAME}-boot.tar.gz"

    $CTR run --rm --privileged \
        --platform "$PLATFORM" \
        -v "${OUTPUT_DIR}:/output:z" \
        debian:bookworm-slim \
        sh -c "
            apt-get update -qq && apt-get install -y -qq --no-install-recommends e2fsprogs kmod tar gzip >/dev/null 2>&1
            WORK=/tmp/ota.\$\$
            mkdir -p \$WORK/rootfs
            tar xf /output/rootfs-${TARGET}.tar -C \$WORK/rootfs
            rm -f \$WORK/rootfs/.dockerenv
            rm -rf \$WORK/rootfs/dev/* \$WORK/rootfs/proc/* \$WORK/rootfs/sys/*
            mkdir -p \$WORK/rootfs/dev \$WORK/rootfs/proc \$WORK/rootfs/sys \$WORK/rootfs/data \$WORK/rootfs/tmp \$WORK/rootfs/run

            # Extract boot files BEFORE clearing them from rootfs
            mkdir -p \$WORK/boot
            cp \$WORK/rootfs/boot/vmlinuz-* \$WORK/boot/vmlinuz
            cp \$WORK/rootfs/boot/initrd.img-* \$WORK/boot/initrd.img
            tar czf /output/${IMAGE_NAME}-boot.tar.gz -C \$WORK/boot .

            # Now clear boot files from rootfs (they live on the boot partition)
            rm -rf \$WORK/rootfs/boot/*
            mkdir -p \$WORK/rootfs/boot

            KVER=\$(ls \$WORK/rootfs/lib/modules/ 2>/dev/null | head -1)
            [ -n \"\$KVER\" ] && depmod -b \$WORK/rootfs \$KVER

            # Create rootfs partition image (same size as in the A/B image)
            ROOT_MB=\$(( (${IMAGE_SIZE_MB} - 128 - 128 - 1) / 2 ))
            ROOT_BLOCKS=\$((ROOT_MB * 1024))
            mke2fs -q -t ext4 -L root-a -O ^has_journal,^metadata_csum -d \$WORK/rootfs /output/${IMAGE_NAME}-rootfs.img \"\${ROOT_BLOCKS}k\"
            rm -rf \$WORK
        "

    gzip -f "$OTA_ROOTFS"
    echo "OTA rootfs: ${OTA_ROOTFS}.gz ($(du -h "${OTA_ROOTFS}.gz" | cut -f1))"
    echo "OTA boot:   ${OTA_BOOT} ($(du -h "${OTA_BOOT}" | cut -f1))"
fi

[ "$AB_LAYOUT" = "1" ] && rm -f "$ROOTFS_TAR"

# --- Convert to VM formats (opt-in) ---
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

# --- Compress ---
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

if [ "$AB_LAYOUT" = "1" ]; then
    DATA_PART="4"
else
    DATA_PART="3"
fi
echo ""
echo "After flashing, mount the data partition (p${DATA_PART}) to add your SSH key:"
echo "  mount /dev/sdX${DATA_PART} /mnt"
echo "  mkdir -p /mnt/ssh"
echo "  cp ~/.ssh/id_ed25519.pub /mnt/ssh/authorized_keys"
echo "  umount /mnt"
