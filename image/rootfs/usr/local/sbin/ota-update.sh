#!/bin/sh
#
# OTA update script for A/B root partition layout.
#
# Checks for new rootfs and boot artifacts on the update server,
# downloads what changed, writes to the inactive slot, and reboots.
#
# Usage:
#   ota-update.sh           # normal check (skips if unchanged)
#   ota-update.sh --force   # re-download everything
#
set -e

say() { echo "[ota] $*"; }

# Prevent concurrent runs
LOCKFILE="/tmp/ota-update.lock"
if [ -f "$LOCKFILE" ]; then
    pid=$(cat "$LOCKFILE" 2>/dev/null)
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        say "Another OTA update is running (pid $pid), skipping"
        exit 0
    fi
fi
echo $$ > "$LOCKFILE"
trap 'rm -f "$LOCKFILE"' EXIT

CONF="/data/ota.conf"
SLOT_CONF="/boot/slot.conf"
FORCE=0

if [ "$1" = "--force" ]; then
    FORCE=1
fi

# --- Read OTA config ---
if [ ! -f "$CONF" ]; then
    exit 0
fi
. "$CONF"

if [ -z "$OTA_URL" ]; then
    exit 0
fi

# Determine target from hardware if not set in config
if [ -z "$OTA_TARGET" ]; then
    case "$(uname -m)" in
        x86_64|amd64)
            OTA_TARGET="rendermatic-x86_64" ;;
        aarch64|arm64)
            if [ -f /boot/config.txt ] || grep -q "Raspberry Pi" /proc/device-tree/model 2>/dev/null; then
                OTA_TARGET="rendermatic-rpi"
            else
                OTA_TARGET="rendermatic-generic-arm64"
            fi ;;
        *)
            say "ERROR: Unknown architecture $(uname -m)"; exit 1 ;;
    esac
fi

ROOTFS_FILE="${OTA_TARGET}-rootfs.img.gz"
BOOT_FILE="${OTA_TARGET}-boot.tar.gz"

# --- Detect current slot from the actual mounted root device ---
ROOT_DEV=$(findmnt -n -o SOURCE / 2>/dev/null)
ROOT_LABEL=$(blkid -s LABEL -o value "$ROOT_DEV" 2>/dev/null)
SLOT=$(echo "$ROOT_LABEL" | sed -n 's/^root-\(.\)$/\1/p')
# Fallback: slot.conf or fstab
if [ -z "$SLOT" ] && [ -f "$SLOT_CONF" ]; then
    . "$SLOT_CONF"
fi
if [ -z "$SLOT" ]; then
    SLOT=$(sed -n 's/^LABEL=root-\(.\).*/\1/p' /etc/fstab | head -1)
fi
if [ -z "$SLOT" ]; then
    say "ERROR: Could not determine current slot"
    exit 1
fi

if [ "$SLOT" = "a" ]; then
    INACTIVE="b"
else
    INACTIVE="a"
fi

say "Current slot: $SLOT, inactive: $INACTIVE"

# --- Find inactive root partition device ---
INACTIVE_DEV=$(blkid -L "root-$INACTIVE" 2>/dev/null) || true
if [ -z "$INACTIVE_DEV" ]; then
    say "ERROR: Could not find partition labeled root-$INACTIVE"
    exit 1
fi
say "Inactive partition: $INACTIVE_DEV"

# --- Check for updates via version file ---
LOCAL_VERSION=$(cat /etc/rendermatic-version 2>/dev/null || echo "unknown")
say "Running version: $LOCAL_VERSION"

REMOTE_VERSION=$(wget -q -O - "$OTA_URL/version" 2>/dev/null) || { say "Could not fetch remote version"; exit 0; }
REMOTE_VERSION=$(echo "$REMOTE_VERSION" | tr -d '[:space:]')

if [ -z "$REMOTE_VERSION" ]; then
    say "Empty remote version"
    exit 0
fi

say "Remote version: $REMOTE_VERSION"

if [ "$FORCE" != "1" ] && [ "$LOCAL_VERSION" = "$REMOTE_VERSION" ]; then
    say "Already up to date"
    exit 0
fi

if [ "$FORCE" != "1" ]; then
    # Don't downgrade - compare as strings (versions should sort correctly)
    if [ "$REMOTE_VERSION" \< "$LOCAL_VERSION" ] 2>/dev/null; then
        say "Remote version is older, skipping"
        exit 0
    fi
fi

say "Update available: $LOCAL_VERSION -> $REMOTE_VERSION"

# --- Download and write rootfs ---
say "Downloading rootfs..."
wget -q -O /tmp/ota-rootfs.img.gz "$OTA_URL/$ROOTFS_FILE" || { say "ERROR: rootfs download failed"; exit 1; }

say "Writing rootfs to $INACTIVE_DEV..."
gunzip -c /tmp/ota-rootfs.img.gz | dd of="$INACTIVE_DEV" bs=4M conv=fsync 2>/dev/null
rm -f /tmp/ota-rootfs.img.gz
sync

# Fix the filesystem label and fstab for the target slot.
say "Relabeling partition to root-$INACTIVE..."
e2label "$INACTIVE_DEV" "root-$INACTIVE"

mkdir -p /tmp/ota-fixup
mount -o rw "$INACTIVE_DEV" /tmp/ota-fixup
sed -i "s/LABEL=root-[ab]/LABEL=root-$INACTIVE/" /tmp/ota-fixup/etc/fstab
umount /tmp/ota-fixup 2>/dev/null || true
rmdir /tmp/ota-fixup 2>/dev/null || true

say "Rootfs written to $INACTIVE_DEV"

# --- Download and extract boot files (if changed) ---
# This section must not abort the script - rootfs is already written,
# we need to reach the slot flip and reboot no matter what.
set +e
REMOTE_BOOT_VERSION=$(wget -q -O - "$OTA_URL/boot-version" 2>/dev/null | tr -d '[:space:]')
LOCAL_BOOT_VERSION=$(cat /data/.ota-boot-version 2>/dev/null)

if [ "$FORCE" = "1" ] || { [ "$REMOTE_BOOT_VERSION" != "$LOCAL_BOOT_VERSION" ] && [ -n "$REMOTE_BOOT_VERSION" ]; }; then
    say "Downloading boot files..."
    if wget -q -O /tmp/ota-boot.tar.gz "$OTA_URL/$BOOT_FILE" 2>/dev/null; then
        mkdir -p /tmp/ota-boot
        if tar xzf /tmp/ota-boot.tar.gz -C /tmp/ota-boot 2>/dev/null; then
            mount -o remount,rw /boot 2>/dev/null
            cp /tmp/ota-boot/vmlinuz "/boot/vmlinuz-$INACTIVE" 2>/dev/null
            cp /tmp/ota-boot/initrd.img "/boot/initrd-$INACTIVE.img" 2>/dev/null
            mount -o remount,ro /boot 2>/dev/null
            echo "$REMOTE_BOOT_VERSION" > /data/.ota-boot-version
            say "Boot files updated for slot $INACTIVE"
        else
            say "WARNING: boot tarball extraction failed"
        fi
        rm -f /tmp/ota-boot.tar.gz
        rm -rf /tmp/ota-boot
    else
        say "WARNING: boot download failed, continuing"
    fi
else
    say "Boot files unchanged, skipping"
fi
set -e

# --- Flip slot and reboot ---
say "Switching to slot $INACTIVE"
mount -o remount,rw /boot

printf 'SLOT=%s\n' "$INACTIVE" > "$SLOT_CONF"

if [ -f /boot/grub-slot.cfg ]; then
    printf 'linux /vmlinuz-%s root=LABEL=root-%s ro quiet loglevel=3 modules=ext4\ninitrd /initrd-%s.img\nboot\n' "$INACTIVE" "$INACTIVE" "$INACTIVE" > /boot/grub-slot.cfg
fi
if [ -f /boot/cmdline.txt ]; then
    sed -i "s/root=LABEL=root-[ab]/root=LABEL=root-$INACTIVE/" /boot/cmdline.txt
    cp "/boot/vmlinuz-$INACTIVE" /boot/vmlinuz
    cp "/boot/initrd-$INACTIVE.img" /boot/initrd.img
fi

mount -o remount,ro /boot
sync

say "Rebooting into slot $INACTIVE..."
reboot
