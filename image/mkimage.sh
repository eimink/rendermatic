#!/bin/sh
#
# Creates a partitioned disk image from a rootfs tarball.
# Runs inside a Debian container - no loop devices or mounting needed.
#
# Uses mke2fs -d (populate ext4 from directory) and mcopy (write FAT32 files).
#
# Standard layout (MBR):
#   p1: 128MB FAT32  /boot  (kernel, initramfs, firmware)
#   p2: var   ext4   /      (read-only rootfs)
#   p3: 128MB ext4   /data  (writable, media/config/logs)
#
# A/B layout (AB_LAYOUT=1):
#   p1: 128MB FAT32  BOOT    /boot   (GRUB + both kernels + slot.conf)
#   p2: var   ext4   root-a  /       (read-only, slot A)
#   p3: var   ext4   root-b  -       (inactive, slot B)
#   p4: 128MB ext4   data    /data   (writable, expanded on first boot)
#
set -e

ROOTFS_TAR="$1"
OUTPUT="$2"
SIZE_MB="${3:-2048}"
TARGET="${4:-rpi}"

DATA_MB="${DATA_MB:-128}"

# A/B layout needs more boot space for two sets of kernel + initrd.
# RPi needs even more since it keeps generic vmlinuz/initrd.img active copies.
if [ "${AB_LAYOUT:-0}" = "1" ]; then
    if [ "$TARGET" = "rpi" ]; then
        BOOT_MB=512
    else
        BOOT_MB=384
    fi
else
    BOOT_MB=128
fi

if [ "${AB_LAYOUT:-0}" = "1" ]; then
    ROOT_MB=$(( (SIZE_MB - BOOT_MB - DATA_MB - 1) / 2 ))
else
    ROOT_MB=$((SIZE_MB - BOOT_MB - DATA_MB - 1))
fi

if [ -z "$ROOTFS_TAR" ] || [ -z "$OUTPUT" ]; then
    echo "Usage: mkimage.sh <rootfs.tar> <output.img> [size_mb] [target]"
    exit 1
fi

WORK="/tmp/mkimage.$$"
mkdir -p "$WORK"

if [ "${AB_LAYOUT:-0}" = "1" ]; then
    echo "Building ${SIZE_MB}MB A/B image for ${TARGET}: boot=${BOOT_MB}MB root-a=${ROOT_MB}MB root-b=${ROOT_MB}MB data=${DATA_MB}MB"
else
    echo "Building ${SIZE_MB}MB image for ${TARGET}: boot=${BOOT_MB}MB root=${ROOT_MB}MB data=${DATA_MB}MB"
fi

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

# Set hostname - container runtime writes container ID to /etc/hostname,
# so ignore anything that looks like a hex hash and default to rendermatic.
HOSTNAME=$(cat "$ROOTFS/etc/hostname" 2>/dev/null | tr -d '[:space:]')
case "$HOSTNAME" in
    [0-9a-f][0-9a-f][0-9a-f][0-9a-f]*) HOSTNAME="" ;;
esac
HOSTNAME="${HOSTNAME:-rendermatic}"
echo "$HOSTNAME" > "$ROOTFS/etc/hostname"

# resolv.conf → systemd-resolved
rm -f "$ROOTFS/etc/resolv.conf"
ln -s /run/systemd/resolve/resolv.conf "$ROOTFS/etc/resolv.conf"
printf "127.0.0.1\t%s localhost\n" "$HOSTNAME" > "$ROOTFS/etc/hosts"

# Write fstab
if [ "${AB_LAYOUT:-0}" = "1" ]; then
    ROOT_LABEL="${ROOT_LABEL:-root-a}"
else
    ROOT_LABEL="${ROOT_LABEL:-root}"
fi
cat > "$ROOTFS/etc/fstab" << FSTAB
LABEL=$ROOT_LABEL      /               ext4    ro,noatime              0 0
LABEL=BOOT      /boot           vfat    ro,noatime              0 0
LABEL=data      /data           ext4    rw,noatime,nofail        0 0
tmpfs           /tmp            tmpfs   nosuid,nodev            0 0
tmpfs           /run            tmpfs   nosuid,nodev,mode=0755  0 0
tmpfs           /var/lib/systemd tmpfs  nosuid,nodev,mode=0755  0 0
tmpfs           /var/log        tmpfs   nosuid,nodev,mode=0755  0 0
FSTAB

# Kernel is already installed in rootfs from Dockerfile.
# Just regenerate modules.dep for read-only root.
KVER=$(ls "$ROOTFS/lib/modules/" 2>/dev/null | head -1)
if [ -n "$KVER" ]; then
    echo "Generating module dependencies for $KVER..."
    depmod -b "$ROOTFS" "$KVER"
fi

# --- Prepare boot directory ---
echo "Preparing boot files..."
BOOTDIR="$WORK/boot"
mkdir -p "$BOOTDIR"

# Copy kernel and initramfs with short names (FAT32 has no symlinks)
if [ "${AB_LAYOUT:-0}" = "1" ]; then
    cp "$ROOTFS/boot/vmlinuz-"* "$BOOTDIR/vmlinuz-a"
    cp "$ROOTFS/boot/initrd.img-"* "$BOOTDIR/initrd-a.img"
    cp "$ROOTFS/boot/vmlinuz-"* "$BOOTDIR/vmlinuz-b"
    cp "$ROOTFS/boot/initrd.img-"* "$BOOTDIR/initrd-b.img"
    # RPi needs active copies with generic names (config.txt loads vmlinuz/initrd.img).
    # GRUB targets don't need these since grub-slot.cfg references -a/-b directly.
    if [ "$TARGET" = "rpi" ]; then
        cp "$ROOTFS/boot/vmlinuz-"* "$BOOTDIR/vmlinuz"
        cp "$ROOTFS/boot/initrd.img-"* "$BOOTDIR/initrd.img"
    fi
    # Slot tracking file (read by OTA script to determine current slot)
    printf 'SLOT=a\n' > "$BOOTDIR/slot.conf"
    # GRUB boot config (loaded via configfile on EFI targets, ignored on RPi)
    cat > "$BOOTDIR/grub-slot.cfg" << 'GRUBSLOT'
linux /vmlinuz-a root=LABEL=root-a ro quiet loglevel=3 modules=ext4
initrd /initrd-a.img
boot
GRUBSLOT
else
    cp "$ROOTFS/boot/vmlinuz-"* "$BOOTDIR/vmlinuz"
    cp "$ROOTFS/boot/initrd.img-"* "$BOOTDIR/initrd.img"
fi

GRUB_MODULES="normal part_msdos part_gpt fat ext2 linux boot search search_label"

case "$TARGET" in
    rpi)
        cat > "$BOOTDIR/config.txt" << 'RPICFG'
disable_overscan=1
dtparam=audio=off
gpu_mem=64

[pi3]
kernel=vmlinuz
initramfs initrd.img
arm_64bit=1

[pi4]
kernel=vmlinuz
initramfs initrd.img
arm_64bit=1
enable_gic=1

[pi5]
kernel=vmlinuz
initramfs initrd.img
arm_64bit=1

[all]
RPICFG

        cat > "$BOOTDIR/cmdline.txt" << CMDLINE
root=LABEL=$ROOT_LABEL ro modules=loop,ext4 rootfstype=ext4 console=tty1 quiet loglevel=3
CMDLINE
        ;;

    generic-arm64|vm-arm64)
        echo "Installing GRUB EFI bootloader..."
        apt-get update -qq && apt-get install -y -qq --no-install-recommends grub-efi-arm64 >/dev/null 2>&1
        if [ "${AB_LAYOUT:-0}" = "1" ]; then
            # Embedded config finds boot partition and loads grub-slot.cfg
            # which contains the boot stanza for the active slot.
            cat > /tmp/grub-early.cfg << 'GRUBCFG'
search --label --set=root BOOT
set timeout=0
configfile /grub-slot.cfg
GRUBCFG
            GRUB_MODULES="$GRUB_MODULES configfile"
        else
            cat > /tmp/grub-early.cfg << GRUBCFG
search --label --set=root BOOT
set timeout=0
linux /vmlinuz root=LABEL=$ROOT_LABEL ro quiet loglevel=3 console=tty0 modules=ext4
initrd /initrd.img
boot
GRUBCFG
        fi
        mkdir -p "$BOOTDIR/EFI/BOOT"
        grub-mkimage -O arm64-efi \
            -o "$BOOTDIR/EFI/BOOT/BOOTAA64.EFI" \
            -c /tmp/grub-early.cfg \
            -p "" \
            $GRUB_MODULES
        ;;

    x86_64)
        echo "Installing GRUB EFI bootloader..."
        apt-get update -qq && apt-get install -y -qq --no-install-recommends grub-efi-amd64-bin >/dev/null 2>&1
        if [ "${AB_LAYOUT:-0}" = "1" ]; then
            cat > /tmp/grub-early.cfg << 'GRUBCFG'
search --label --set=root BOOT
set timeout=0
configfile /grub-slot.cfg
GRUBCFG
            GRUB_MODULES="$GRUB_MODULES configfile"
        else
            cat > /tmp/grub-early.cfg << GRUBCFG
search --label --set=root BOOT
set timeout=0
linux /vmlinuz root=LABEL=$ROOT_LABEL ro quiet loglevel=3 modules=ext4
initrd /initrd.img
boot
GRUBCFG
        fi
        mkdir -p "$BOOTDIR/EFI/BOOT"
        grub-mkimage -O x86_64-efi \
            -o "$BOOTDIR/EFI/BOOT/BOOTX64.EFI" \
            -c /tmp/grub-early.cfg \
            -p "" \
            $GRUB_MODULES
        ;;
esac

# --- Prepare data directory ---
DATADIR="$WORK/data"
mkdir -p "$DATADIR/media" "$DATADIR/logs" "$DATADIR/ssh" "$DATADIR/lib"
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

echo "Creating root partition A (ext4)..."
ROOT_IMG="$WORK/root.img"
ROOT_BLOCKS=$((ROOT_MB * 1024))
if [ "${AB_LAYOUT:-0}" = "1" ]; then
    mke2fs -q -t ext4 -L root-a -O ^has_journal,^metadata_csum -d "$ROOTFS" "$ROOT_IMG" "${ROOT_BLOCKS}k"
else
    mke2fs -q -t ext4 -L "${ROOT_LABEL:-root}" -O ^has_journal,^metadata_csum -d "$ROOTFS" "$ROOT_IMG" "${ROOT_BLOCKS}k"
fi

if [ "${AB_LAYOUT:-0}" = "1" ]; then
    echo "Creating root partition B (ext4, empty)..."
    ROOTB_IMG="$WORK/rootb.img"
    mke2fs -q -t ext4 -L root-b -O ^has_journal,^metadata_csum "$ROOTB_IMG" "${ROOT_BLOCKS}k"
fi

echo "Creating data partition (ext4)..."
DATA_IMG="$WORK/data.img"
DATA_BLOCKS=$((DATA_MB * 1024))
mke2fs -q -t ext4 -L data -O ^has_journal,^metadata_csum -d "$DATADIR" "$DATA_IMG" "${DATA_BLOCKS}k"

# --- Assemble final image ---
echo "Assembling disk image..."

dd if=/dev/zero of="$OUTPUT" bs=1M count=1 2>/dev/null
cat "$BOOT_IMG" >> "$OUTPUT"
cat "$ROOT_IMG" >> "$OUTPUT"
if [ "${AB_LAYOUT:-0}" = "1" ]; then
    cat "$ROOTB_IMG" >> "$OUTPUT"
fi
cat "$DATA_IMG" >> "$OUTPUT"

# Write partition table
BOOT_START=2048
BOOT_SECTORS=$((BOOT_MB * 2048))
ROOT_START=$((BOOT_START + BOOT_SECTORS))
ROOT_SECTORS=$((ROOT_MB * 2048))

# Use type=ef for EFI System Partition on UEFI targets, type=c for RPi
case "$TARGET" in
    rpi)         BOOT_TYPE="c" ;;
    *)           BOOT_TYPE="ef" ;;
esac

if [ "${AB_LAYOUT:-0}" = "1" ]; then
    ROOTB_START=$((ROOT_START + ROOT_SECTORS))
    DATA_START=$((ROOTB_START + ROOT_SECTORS))
    sfdisk "$OUTPUT" << PARTS 2>/dev/null
label: dos
unit: sectors

start=$BOOT_START, size=$BOOT_SECTORS, type=$BOOT_TYPE, bootable
start=$ROOT_START, size=$ROOT_SECTORS, type=83
start=$ROOTB_START, size=$ROOT_SECTORS, type=83
start=$DATA_START, type=83
PARTS
else
    DATA_START=$((ROOT_START + ROOT_SECTORS))
    sfdisk "$OUTPUT" << PARTS 2>/dev/null
label: dos
unit: sectors

start=$BOOT_START, size=$BOOT_SECTORS, type=$BOOT_TYPE, bootable
start=$ROOT_START, size=$ROOT_SECTORS, type=83
start=$DATA_START, type=83
PARTS
fi


# Cleanup
rm -rf "$WORK"

IMGSIZE=$(du -h "$OUTPUT" | cut -f1)
echo "Image created: $OUTPUT ($IMGSIZE)"
