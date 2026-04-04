#!/bin/sh
# Expand the data partition to fill the remaining disk space.
# Runs once on first boot before local-fs.target mounts partitions.

say() { echo "[data-expand] $*"; }

# Wait for the data partition to appear
DATA_DEV=""
for i in $(seq 1 10); do
    DATA_DEV=$(blkid -L data 2>/dev/null) || true
    [ -n "$DATA_DEV" ] && break
    say "Waiting for data partition to appear ($i/10)..."
    sleep 1
done

if [ -z "$DATA_DEV" ]; then
    say "No partition labeled 'data' found, skipping"
    exit 0
fi

say "Found data partition: $DATA_DEV"

# Check for expansion marker
mkdir -p /tmp/data-expand-mnt
if mount -o ro "$DATA_DEV" /tmp/data-expand-mnt 2>/dev/null; then
    if [ -f /tmp/data-expand-mnt/.expanded ]; then
        umount /tmp/data-expand-mnt
        rmdir /tmp/data-expand-mnt
        say "Already expanded, skipping"
        exit 0
    fi
    umount /tmp/data-expand-mnt
fi

# Find parent disk and partition number
DISK_DEV=$(lsblk -no PKNAME "$DATA_DEV" 2>/dev/null)
if [ -z "$DISK_DEV" ]; then
    say "ERROR: Could not determine parent disk for $DATA_DEV"
    exit 1
fi
DISK_DEV="/dev/$DISK_DEV"
PART_NUM=$(echo "$DATA_DEV" | grep -o '[0-9]*$')

say "Expanding partition $PART_NUM on $DISK_DEV..."

# Grow partition to fill remaining space
echo ", +" | sfdisk --force -N "$PART_NUM" "$DISK_DEV" 2>&1
partx -u "$DISK_DEV" 2>/dev/null || partprobe "$DISK_DEV" 2>/dev/null || true
sleep 2

# Check and resize filesystem
e2fsck -fy "$DATA_DEV" 2>&1 || true
resize2fs "$DATA_DEV" 2>&1

# Write marker
if mount "$DATA_DEV" /tmp/data-expand-mnt 2>/dev/null; then
    touch /tmp/data-expand-mnt/.expanded
    umount /tmp/data-expand-mnt
fi
rmdir /tmp/data-expand-mnt 2>/dev/null || true

say "Done: $(blockdev --getsize64 "$DATA_DEV" | awk '{printf "%.0fMB", $1/1048576}')"
