#!/bin/sh
#
# Creates a partitioned disk image from a rootfs tarball.
# Runs inside an Alpine container — no loop devices or mounting needed.
#
# Uses mke2fs -d (populate ext4 from directory) and mcopy (write FAT32 files).
#
# Layout (MBR):
#   p1: 64MB  FAT32  /boot  (kernel, initramfs, RPi firmware)
#   p2: var   ext4   /      (read-only rootfs)
#   p3: 128MB ext4   /data  (writable, textures/config/logs)
#
set -e

ROOTFS_TAR="$1"
OUTPUT="$2"
SIZE_MB="${3:-512}"

BOOT_MB=128
DATA_MB=128
ROOT_MB=$((SIZE_MB - BOOT_MB - DATA_MB - 1))

if [ -z "$ROOTFS_TAR" ] || [ -z "$OUTPUT" ]; then
    echo "Usage: mkimage.sh <rootfs.tar> <output.img> [size_mb]"
    exit 1
fi

WORK="/tmp/mkimage.$$"
mkdir -p "$WORK"

echo "Building ${SIZE_MB}MB image: boot=${BOOT_MB}MB root=${ROOT_MB}MB data=${DATA_MB}MB"

MACHINE=$(uname -m)

# --- Prepare rootfs directory ---
echo "Extracting rootfs..."
ROOTFS="$WORK/rootfs"
mkdir -p "$ROOTFS"
tar xf "$ROOTFS_TAR" -C "$ROOTFS"

# Clean Docker artifacts
rm -f "$ROOTFS/.dockerenv"
rm -rf "$ROOTFS/dev/"* "$ROOTFS/proc/"* "$ROOTFS/sys/"*
mkdir -p "$ROOTFS/dev" "$ROOTFS/proc" "$ROOTFS/sys" \
         "$ROOTFS/data" "$ROOTFS/boot" "$ROOTFS/tmp" "$ROOTFS/run"

# Set hostname
echo "rendermatic" > "$ROOTFS/etc/hostname"
printf "127.0.0.1\trendermatic localhost\n" > "$ROOTFS/etc/hosts"

# Write fstab
cat > "$ROOTFS/etc/fstab" << 'FSTAB'
LABEL=root      /           ext4    ro,noatime              0 1
LABEL=BOOT      /boot       vfat    ro,noatime              0 2
LABEL=data      /data       ext4    rw,noatime,commit=60    0 2
tmpfs           /tmp        tmpfs   nosuid,nodev            0 0
tmpfs           /run        tmpfs   nosuid,nodev,mode=0755  0 0
FSTAB

# Ensure init scripts are executable
chmod +x "$ROOTFS/etc/init.d/rendermatic" 2>/dev/null || true
chmod +x "$ROOTFS/etc/init.d/data-mount" 2>/dev/null || true

# Install kernel packages into rootfs
APK_REPOS="--repository https://dl-cdn.alpinelinux.org/alpine/v3.21/main --repository https://dl-cdn.alpinelinux.org/alpine/v3.21/community"

if [ "$MACHINE" = "aarch64" ]; then
    echo "Installing RPi kernel and firmware..."
    apk --root "$ROOTFS" --initdb --no-cache --allow-untrusted \
        $APK_REPOS --keys-dir /etc/apk/keys \
        add alpine-base linux-rpi linux-firmware-none raspberrypi-bootloader mkinitfs 2>&1 | tail -5
else
    echo "Installing kernel..."
    apk --root "$ROOTFS" --initdb --no-cache --allow-untrusted \
        $APK_REPOS --keys-dir /etc/apk/keys \
        add alpine-base linux-lts linux-firmware-none mkinitfs 2>&1 | tail -5
fi

# --- Prepare boot directory ---
echo "Preparing boot files..."
BOOTDIR="$WORK/boot"
mkdir -p "$BOOTDIR"

# Copy kernel and initramfs
cp "$ROOTFS/boot/"* "$BOOTDIR/" 2>/dev/null || true

if [ "$MACHINE" = "aarch64" ]; then
    cat > "$BOOTDIR/config.txt" << 'RPICFG'
disable_overscan=1
dtparam=audio=off
gpu_mem=64

[pi3]
kernel=vmlinuz-rpi
initramfs initramfs-rpi

[pi4]
kernel=vmlinuz-rpi
initramfs initramfs-rpi
arm_64bit=1
enable_gic=1

[pi5]
kernel=vmlinuz-rpi
initramfs initramfs-rpi
arm_64bit=1

[all]
RPICFG

    cat > "$BOOTDIR/cmdline.txt" << 'CMDLINE'
root=LABEL=root ro modules=loop,ext4 rootfstype=ext4 console=tty1 quiet
CMDLINE
fi

if [ "$MACHINE" = "x86_64" ]; then
    cat > "$BOOTDIR/extlinux.conf" << 'BOOTCFG'
DEFAULT rendermatic
TIMEOUT 10
LABEL rendermatic
    LINUX /vmlinuz-lts
    INITRD /initramfs-lts
    APPEND root=LABEL=root ro quiet
BOOTCFG
fi

# --- Prepare data directory ---
DATADIR="$WORK/data"
mkdir -p "$DATADIR/textures" "$DATADIR/logs" "$DATADIR/ssh"
cp "$ROOTFS/rendermatic/config.json.default" "$DATADIR/config.json" 2>/dev/null || true
cp "$ROOTFS/rendermatic/textures/"* "$DATADIR/textures/" 2>/dev/null || true
touch "$DATADIR/.initialized"

# Clear boot files from rootfs (they live on the boot partition)
rm -rf "$ROOTFS/boot/"*

# --- Create filesystem images ---
echo "Creating boot partition (FAT32)..."
BOOT_IMG="$WORK/boot.img"
dd if=/dev/zero of="$BOOT_IMG" bs=1M count="$BOOT_MB" 2>/dev/null
mkfs.vfat -F 32 -n BOOT "$BOOT_IMG" >/dev/null

# Copy boot files using mcopy
for f in "$BOOTDIR/"*; do
    [ -e "$f" ] || continue
    if [ -d "$f" ]; then
        mcopy -i "$BOOT_IMG" -s "$f" "::$(basename "$f")"
    else
        mcopy -i "$BOOT_IMG" "$f" "::$(basename "$f")"
    fi
done

echo "Creating root partition (ext4)..."
ROOT_IMG="$WORK/root.img"
ROOT_BLOCKS=$((ROOT_MB * 1024))
mke2fs -q -t ext4 -L root -O ^has_journal -d "$ROOTFS" "$ROOT_IMG" "${ROOT_BLOCKS}k"

echo "Creating data partition (ext4)..."
DATA_IMG="$WORK/data.img"
DATA_BLOCKS=$((DATA_MB * 1024))
mke2fs -q -t ext4 -L data -d "$DATADIR" "$DATA_IMG" "${DATA_BLOCKS}k"

# --- Assemble final image ---
echo "Assembling disk image..."

# MBR gap (1MB) + partitions
dd if=/dev/zero of="$OUTPUT" bs=1M count=1 2>/dev/null
cat "$BOOT_IMG" >> "$OUTPUT"
cat "$ROOT_IMG" >> "$OUTPUT"
cat "$DATA_IMG" >> "$OUTPUT"

# Write partition table with sfdisk
BOOT_START=2048
BOOT_SECTORS=$((BOOT_MB * 2048))
ROOT_START=$((BOOT_START + BOOT_SECTORS))
ROOT_SECTORS=$((ROOT_MB * 2048))
DATA_START=$((ROOT_START + ROOT_SECTORS))

sfdisk "$OUTPUT" << PARTS 2>/dev/null
label: dos
unit: sectors

start=$BOOT_START, size=$BOOT_SECTORS, type=c, bootable
start=$ROOT_START, size=$ROOT_SECTORS, type=83
start=$DATA_START, type=83
PARTS

# x86_64: install MBR bootloader
if [ "$MACHINE" = "x86_64" ]; then
    apk add --no-cache syslinux 2>/dev/null || true
    if [ -f /usr/share/syslinux/mbr.bin ]; then
        dd if=/usr/share/syslinux/mbr.bin of="$OUTPUT" bs=440 count=1 conv=notrunc 2>/dev/null
    fi
fi

# Cleanup
rm -rf "$WORK"

IMGSIZE=$(du -h "$OUTPUT" | cut -f1)
echo "Image created: $OUTPUT ($IMGSIZE)"
