#!/bin/sh
#
# Creates a partitioned disk image from a rootfs tarball.
# Runs inside an Alpine container — no loop devices or mounting needed.
#
# Uses mke2fs -d (populate ext4 from directory) and mcopy (write FAT32 files).
#
# Layout (MBR):
#   p1: 128MB FAT32  /boot  (kernel, initramfs, firmware)
#   p2: var   ext4   /      (read-only rootfs)
#   p3: 128MB ext4   /data  (writable, media/config/logs)
#
set -e

ROOTFS_TAR="$1"
OUTPUT="$2"
SIZE_MB="${3:-512}"
TARGET="${4:-rpi}"

BOOT_MB=128
DATA_MB=128
ROOT_MB=$((SIZE_MB - BOOT_MB - DATA_MB - 1))

if [ -z "$ROOTFS_TAR" ] || [ -z "$OUTPUT" ]; then
    echo "Usage: mkimage.sh <rootfs.tar> <output.img> [size_mb] [target]"
    exit 1
fi

WORK="/tmp/mkimage.$$"
mkdir -p "$WORK"

echo "Building ${SIZE_MB}MB image for ${TARGET}: boot=${BOOT_MB}MB root=${ROOT_MB}MB data=${DATA_MB}MB"

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

# resolv.conf → tmpfs so DHCP can update DNS on read-only root
rm -f "$ROOTFS/etc/resolv.conf"
ln -s /run/resolv.conf "$ROOTFS/etc/resolv.conf"
printf "127.0.0.1\trendermatic localhost\n" > "$ROOTFS/etc/hosts"

# Write fstab
cat > "$ROOTFS/etc/fstab" << 'FSTAB'
LABEL=root      /               ext4    ro,noatime              0 0
LABEL=BOOT      /boot           vfat    ro,noatime              0 0
LABEL=data      /data           ext4    rw,noatime              0 0
tmpfs           /tmp            tmpfs   nosuid,nodev            0 0
tmpfs           /run            tmpfs   nosuid,nodev,mode=0755  0 0
tmpfs           /var/lib/dhcpcd tmpfs   nosuid,nodev,mode=0755  0 0
tmpfs           /var/log        tmpfs   nosuid,nodev,mode=0755  0 0
FSTAB

# Ensure init scripts are executable
chmod +x "$ROOTFS/etc/init.d/rendermatic" 2>/dev/null || true
chmod +x "$ROOTFS/etc/init.d/data-mount" 2>/dev/null || true

# Install kernel packages into rootfs
APK_REPOS="--repository https://dl-cdn.alpinelinux.org/alpine/v3.21/main --repository https://dl-cdn.alpinelinux.org/alpine/v3.21/community"
APK_BASE="apk --root $ROOTFS --initdb --no-cache --allow-untrusted $APK_REPOS --keys-dir /etc/apk/keys"

# Configure mkinitfs features — write to BOTH container and rootfs
# (apk triggers run in the container context, not inside rootfs)
mkdir -p /etc/mkinitfs "$ROOTFS/etc/mkinitfs"
case "$TARGET" in
    rpi)
        echo 'features="ata base ext4 mmc scsi usb"' | tee /etc/mkinitfs/mkinitfs.conf > "$ROOTFS/etc/mkinitfs/mkinitfs.conf"
        ;;
    *)
        echo 'features="ata base ext4 nvme scsi usb virtio"' | tee /etc/mkinitfs/mkinitfs.conf > "$ROOTFS/etc/mkinitfs/mkinitfs.conf"
        ;;
esac

case "$TARGET" in
    rpi)
        echo "Installing RPi kernel and firmware..."
        $APK_BASE add alpine-base linux-rpi linux-firmware-none raspberrypi-bootloader mkinitfs 2>&1 | tail -5
        ;;
    *)
        echo "Installing generic kernel..."
        $APK_BASE add alpine-base linux-lts linux-firmware-none mkinitfs 2>&1 | tail -5
        ;;
esac

# Regenerate initramfs with correct features (in case trigger used wrong config)
KVER=$(ls "$ROOTFS/lib/modules/" | head -1)
if [ -n "$KVER" ]; then
    echo "Regenerating initramfs for $KVER..."
    mkinitfs -b "$ROOTFS" -o "$ROOTFS/boot/initramfs-lts" "$KVER"
fi

# Generate modules.dep (root is read-only at runtime)
echo "Generating module dependencies..."
depmod -b "$ROOTFS" "$KVER"

# --- Prepare boot directory ---
echo "Preparing boot files..."
BOOTDIR="$WORK/boot"
mkdir -p "$BOOTDIR"

# Copy kernel and initramfs
cp "$ROOTFS/boot/"* "$BOOTDIR/" 2>/dev/null || true

case "$TARGET" in
    rpi)
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
        ;;

    generic-arm64|vm-arm64)
        echo "Installing GRUB EFI bootloader..."
        apk add --no-cache grub-efi >/dev/null 2>&1
        cat > /tmp/grub-early.cfg << 'GRUBCFG'
search --label --set=root BOOT
set timeout=0
linux /vmlinuz-lts root=LABEL=root ro quiet console=tty0 modules=ext4
initrd /initramfs-lts
boot
GRUBCFG
        mkdir -p "$BOOTDIR/EFI/BOOT"
        grub-mkimage -O arm64-efi \
            -o "$BOOTDIR/EFI/BOOT/BOOTAA64.EFI" \
            -c /tmp/grub-early.cfg \
            -p /EFI/BOOT \
            part_msdos part_gpt fat ext2 linux boot search search_label
        ;;

    x86_64)
        echo "Installing GRUB EFI bootloader..."
        apk add --no-cache grub-efi >/dev/null 2>&1
        cat > /tmp/grub-early.cfg << 'GRUBCFG'
search --label --set=root BOOT
set timeout=0
linux /vmlinuz-lts root=LABEL=root ro quiet modules=ext4
initrd /initramfs-lts
boot
GRUBCFG
        mkdir -p "$BOOTDIR/EFI/BOOT"
        grub-mkimage -O x86_64-efi \
            -o "$BOOTDIR/EFI/BOOT/BOOTX64.EFI" \
            -c /tmp/grub-early.cfg \
            -p /EFI/BOOT \
            part_msdos part_gpt fat ext2 linux boot search search_label
        ;;
esac

# --- Prepare data directory ---
DATADIR="$WORK/data"
mkdir -p "$DATADIR/media" "$DATADIR/logs" "$DATADIR/ssh"
cp "$ROOTFS/rendermatic/config.json.default" "$DATADIR/config.json" 2>/dev/null || true
cp "$ROOTFS/rendermatic/media/"* "$DATADIR/media/" 2>/dev/null || true
touch "$DATADIR/.initialized"

# Clear boot files from rootfs (they live on the boot partition)
rm -rf "$ROOTFS/boot/"*

# --- Create filesystem images ---
echo "Creating boot partition (FAT32)..."
BOOT_IMG="$WORK/boot.img"
dd if=/dev/zero of="$BOOT_IMG" bs=1M count="$BOOT_MB" 2>/dev/null
mkfs.vfat -F 32 -n BOOT "$BOOT_IMG" >/dev/null

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
mke2fs -q -t ext4 -L root -O ^has_journal,^metadata_csum -d "$ROOTFS" "$ROOT_IMG" "${ROOT_BLOCKS}k"

echo "Creating data partition (ext4)..."
DATA_IMG="$WORK/data.img"
DATA_BLOCKS=$((DATA_MB * 1024))
mke2fs -q -t ext4 -L data -O ^has_journal,^metadata_csum -d "$DATADIR" "$DATA_IMG" "${DATA_BLOCKS}k"

# --- Assemble final image ---
echo "Assembling disk image..."

dd if=/dev/zero of="$OUTPUT" bs=1M count=1 2>/dev/null
cat "$BOOT_IMG" >> "$OUTPUT"
cat "$ROOT_IMG" >> "$OUTPUT"
cat "$DATA_IMG" >> "$OUTPUT"

# Write partition table
BOOT_START=2048
BOOT_SECTORS=$((BOOT_MB * 2048))
ROOT_START=$((BOOT_START + BOOT_SECTORS))
ROOT_SECTORS=$((ROOT_MB * 2048))
DATA_START=$((ROOT_START + ROOT_SECTORS))

# Use type=ef for EFI System Partition on UEFI targets, type=c for RPi
case "$TARGET" in
    rpi)         BOOT_TYPE="c" ;;
    *)           BOOT_TYPE="ef" ;;
esac

sfdisk "$OUTPUT" << PARTS 2>/dev/null
label: dos
unit: sectors

start=$BOOT_START, size=$BOOT_SECTORS, type=$BOOT_TYPE, bootable
start=$ROOT_START, size=$ROOT_SECTORS, type=83
start=$DATA_START, type=83
PARTS


# Cleanup
rm -rf "$WORK"

IMGSIZE=$(du -h "$OUTPUT" | cut -f1)
echo "Image created: $OUTPUT ($IMGSIZE)"
