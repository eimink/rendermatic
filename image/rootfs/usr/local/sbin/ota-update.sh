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

# --- Read current slot ---
if [ -f "$SLOT_CONF" ]; then
    . "$SLOT_CONF"
fi
# Fallback: detect from fstab (current root label)
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

# --- Check for updates via HTTP HEAD ---
get_fingerprint() {
    url="$1"
    headers=$(wget --spider -S "$url" 2>&1) || return 1
    size=$(echo "$headers" | sed -n 's/.*Content-Length: \([0-9]*\).*/\1/p' | tail -1)
    modified=$(echo "$headers" | sed -n 's/.*Last-Modified: \(.*\)/\1/p' | tail -1)
    echo "${size}|${modified}"
}

is_changed() {
    url="$1"
    etag_file="$2"

    if [ "$FORCE" = "1" ]; then
        return 0
    fi

    fingerprint=$(get_fingerprint "$url") || return 0
    if [ -f "$etag_file" ] && [ "$(cat "$etag_file")" = "$fingerprint" ]; then
        return 1
    fi
    return 0
}

ROOTFS_CHANGED=0
BOOT_CHANGED=0

if is_changed "$OTA_URL/rootfs.img.gz" "/data/.ota-rootfs-etag"; then
    ROOTFS_CHANGED=1
    say "Rootfs image changed on server"
fi

if is_changed "$OTA_URL/boot.tar.gz" "/data/.ota-boot-etag"; then
    BOOT_CHANGED=1
    say "Boot files changed on server"
fi

if [ "$ROOTFS_CHANGED" = "0" ] && [ "$BOOT_CHANGED" = "0" ]; then
    say "No updates available"
    exit 0
fi

# --- Download and write rootfs ---
if [ "$ROOTFS_CHANGED" = "1" ]; then
    say "Downloading rootfs..."
    wget -q -O /tmp/ota-rootfs.img.gz "$OTA_URL/rootfs.img.gz" || { say "ERROR: rootfs download failed"; exit 1; }

    say "Writing rootfs to $INACTIVE_DEV..."
    gunzip -c /tmp/ota-rootfs.img.gz | dd of="$INACTIVE_DEV" bs=4M conv=fsync 2>/dev/null
    rm -f /tmp/ota-rootfs.img.gz
    sync

    # Fix the filesystem label and fstab for the target slot.
    # The build produces rootfs with label root-a and fstab referencing root-a.
    # If we're writing to slot B, both need to say root-b.
    say "Relabeling partition to root-$INACTIVE..."
    e2label "$INACTIVE_DEV" "root-$INACTIVE"

    mkdir -p /tmp/ota-fixup
    mount "$INACTIVE_DEV" /tmp/ota-fixup
    sed -i "s/LABEL=root-[ab]/LABEL=root-$INACTIVE/" /tmp/ota-fixup/etc/fstab
    umount /tmp/ota-fixup
    rmdir /tmp/ota-fixup

    # Save fingerprint only after successful write
    fp=$(get_fingerprint "$OTA_URL/rootfs.img.gz") && echo "$fp" > /data/.ota-rootfs-etag
    say "Rootfs written to $INACTIVE_DEV"
fi

# --- Download and extract boot files ---
if [ "$BOOT_CHANGED" = "1" ]; then
    say "Downloading boot files..."
    wget -q -O /tmp/ota-boot.tar.gz "$OTA_URL/boot.tar.gz" || { say "ERROR: boot download failed"; exit 1; }

    # Extract to a temp dir, then copy only the inactive slot's files
    mkdir -p /tmp/ota-boot
    tar xzf /tmp/ota-boot.tar.gz -C /tmp/ota-boot
    rm -f /tmp/ota-boot.tar.gz

    mount -o remount,rw /boot
    # The tarball contains vmlinuz and initrd.img (slot-agnostic names).
    # Rename to the inactive slot's filenames.
    cp /tmp/ota-boot/vmlinuz "/boot/vmlinuz-$INACTIVE"
    cp /tmp/ota-boot/initrd.img "/boot/initrd-$INACTIVE.img"
    mount -o remount,ro /boot

    rm -rf /tmp/ota-boot

    fp=$(get_fingerprint "$OTA_URL/boot.tar.gz") && echo "$fp" > /data/.ota-boot-etag
    say "Boot files updated for slot $INACTIVE"
fi

# --- Flip slot if rootfs was updated ---
if [ "$ROOTFS_CHANGED" = "1" ]; then
    say "Switching to slot $INACTIVE"
    mount -o remount,rw /boot

    # Update slot tracking
    printf 'SLOT=%s\n' "$INACTIVE" > "$SLOT_CONF"

    # Update bootloader config (GRUB or RPi)
    if [ -f /boot/grub-slot.cfg ]; then
        # GRUB targets: rewrite the config that configfile loads
        cat > /boot/grub-slot.cfg << GRUBEOF
linux /vmlinuz-$INACTIVE root=LABEL=root-$INACTIVE ro quiet loglevel=3 modules=ext4
initrd /initrd-$INACTIVE.img
boot
GRUBEOF
    fi
    if [ -f /boot/cmdline.txt ]; then
        # RPi: update root label in cmdline.txt and copy active kernel
        sed -i "s/root=LABEL=root-[ab]/root=LABEL=root-$INACTIVE/" /boot/cmdline.txt
        cp "/boot/vmlinuz-$INACTIVE" /boot/vmlinuz
        cp "/boot/initrd-$INACTIVE.img" /boot/initrd.img
    fi

    mount -o remount,ro /boot
    sync

    say "Rebooting into slot $INACTIVE..."
    reboot
fi

say "Boot files updated, will take effect on next reboot"
