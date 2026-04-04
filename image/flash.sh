#!/bin/sh
#
# Flash a Rendermatic image to a disk and optionally provision SSH keys.
# Works on both Linux and macOS.
#
# Usage:
#   ./flash.sh <image.img.gz> <device> [options]
#
# Options:
#   --key <path>           Add an SSH public key file
#   --github <user>        Fetch SSH keys from GitHub on first boot
#   --wifi <ssid> <pass>   Configure WiFi credentials
#   --ota-url <url>        Configure OTA update server URL
#
# Examples:
#   ./flash.sh rendermatic-rpi.img.gz /dev/sdX --key ~/.ssh/id_ed25519.pub
#   ./flash.sh rendermatic-rpi.img.gz /dev/disk4 --github eimink --wifi MyNetwork MyPassword
#   ./flash.sh rendermatic-x86_64.img.gz /dev/sdX --github user1 --github user2
#
set -e

IMAGE="$1"
DEVICE="$2"
shift 2 || true

if [ -z "$IMAGE" ] || [ -z "$DEVICE" ]; then
    echo "Usage: $0 <image.img.gz> <device> [--key <pubkey>] [--github <user>] [--wifi <ssid> <pass>]"
    exit 1
fi

if [ ! -f "$IMAGE" ]; then
    echo "Error: Image not found: $IMAGE"
    exit 1
fi

# Detect macOS vs Linux
IS_MACOS=false
if [ "$(uname)" = "Darwin" ]; then
    IS_MACOS=true
fi

# Validate device
if $IS_MACOS; then
    # macOS: disk devices are character devices, not block devices
    case "$DEVICE" in
        /dev/disk[0-9]*)
            ;;
        *)
            echo "Error: Expected /dev/diskN device on macOS, got: $DEVICE"
            exit 1
            ;;
    esac
else
    if [ ! -b "$DEVICE" ]; then
        echo "Error: Not a block device: $DEVICE"
        exit 1
    fi
fi

# Parse options
KEYS=""
GITHUB_USERS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --key)
            shift
            if [ ! -f "$1" ]; then
                echo "Error: Key file not found: $1"
                exit 1
            fi
            KEYS="${KEYS}$(cat "$1")
"
            ;;
        --github)
            shift
            GITHUB_USERS="${GITHUB_USERS}${1}
"
            ;;
        --wifi)
            shift
            WIFI_SSID="$1"
            shift
            WIFI_PSK="$1"
            if [ -z "$WIFI_SSID" ] || [ -z "$WIFI_PSK" ]; then
                echo "Error: --wifi requires SSID and password arguments"
                exit 1
            fi
            ;;
        --ota-url)
            shift
            OTA_URL="$1"
            if [ -z "$OTA_URL" ]; then
                echo "Error: --ota-url requires a URL argument"
                exit 1
            fi
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
    shift
done

# Confirm
echo "=== Rendermatic Flash Tool ==="
echo "Image:  $IMAGE"
echo "Device: $DEVICE"
[ -n "$KEYS" ] && echo "Keys:   $(echo "$KEYS" | wc -l | tr -d ' ') SSH key(s)"
[ -n "$GITHUB_USERS" ] && echo "GitHub: $(echo "$GITHUB_USERS" | grep -c . | tr -d ' ') user(s)"
[ -n "$WIFI_SSID" ] && echo "WiFi:   $WIFI_SSID"
[ -n "$OTA_URL" ] && echo "OTA:    $OTA_URL"
echo ""
echo "WARNING: All data on $DEVICE will be destroyed!"
printf "Continue? [y/N] "
read -r confirm
case "$confirm" in
    y|Y) ;;
    *) echo "Aborted."; exit 1 ;;
esac

# Flash
echo "--- Flashing image ---"
if $IS_MACOS; then
    # Unmount all partitions first
    diskutil unmountDisk "$DEVICE" 2>/dev/null || true
    # Use raw device for faster writes
    RAW_DEVICE=$(echo "$DEVICE" | sed 's|/dev/disk|/dev/rdisk|')
    if command -v pv >/dev/null 2>&1; then
        # pv shows throughput - no percentage since gunzip -l is unreliable for large files
        gunzip -c "$IMAGE" | pv | dd of="$RAW_DEVICE" bs=4m 2>/dev/null
    else
        echo "Writing $(du -h "$IMAGE" | cut -f1) compressed (press ctrl+T for progress)..."
        gunzip -c "$IMAGE" | dd of="$RAW_DEVICE" bs=4m
    fi
    sync
    # Re-probe partitions
    diskutil unmountDisk "$DEVICE" 2>/dev/null || true
    sleep 1
else
    gunzip -c "$IMAGE" | dd of="$DEVICE" bs=4M status=progress
    sync
fi

# Determine boot partition device
if $IS_MACOS; then
    BOOT_PART="${DEVICE}s1"
else
    BOOT_PART="${DEVICE}1"
    case "$DEVICE" in
        *nvme*|*mmcblk*|*loop*) BOOT_PART="${DEVICE}p1" ;;
    esac
fi

if [ -n "$KEYS" ] || [ -n "$GITHUB_USERS" ] || [ -n "$WIFI_SSID" ]; then
    echo "--- Provisioning boot partition ---"
    MOUNT_DIR=$(mktemp -d)

    if $IS_MACOS; then
        # EFI partition type (0xEF) won't mount with diskutil; use mount_msdos
        mount -t msdos "$BOOT_PART" "$MOUNT_DIR"
    else
        mount "$BOOT_PART" "$MOUNT_DIR"
    fi

    if [ -n "$KEYS" ]; then
        echo "$KEYS" > "$MOUNT_DIR/authorized_keys"
        echo "Wrote authorized_keys to boot partition"
    fi

    if [ -n "$GITHUB_USERS" ]; then
        echo "$GITHUB_USERS" > "$MOUNT_DIR/github-ssh.txt"
        echo "Wrote github-ssh.txt to boot partition"
    fi

    if [ -n "$WIFI_SSID" ]; then
        printf '%s\n%s\n' "$WIFI_SSID" "$WIFI_PSK" > "$MOUNT_DIR/wifi.txt"
        echo "Wrote wifi.txt to boot partition"
    fi

    umount "$MOUNT_DIR"
    rmdir "$MOUNT_DIR"
fi

# Provision OTA URL on the data partition
if [ -n "$OTA_URL" ]; then
    echo "--- Provisioning data partition ---"

    # Data partition is the last partition (p3 standard, p4 A/B)
    if $IS_MACOS; then
        # Try p4 first (A/B), fall back to p3 (standard)
        DATA_PART="${DEVICE}s4"
        diskutil info "$DATA_PART" >/dev/null 2>&1 || DATA_PART="${DEVICE}s3"
    else
        case "$DEVICE" in
            *nvme*|*mmcblk*|*loop*)
                DATA_PART="${DEVICE}p4"
                [ -b "$DATA_PART" ] || DATA_PART="${DEVICE}p3"
                ;;
            *)
                DATA_PART="${DEVICE}4"
                [ -b "$DATA_PART" ] || DATA_PART="${DEVICE}3"
                ;;
        esac
    fi

    MOUNT_DIR=$(mktemp -d)
    MOUNTED=false
    if $IS_MACOS; then
        # macOS can't mount ext4 natively
        echo ""
        echo "NOTE: Cannot write ota.conf from macOS (ext4 partition)."
        echo "After first boot, run:"
        echo "  ssh render@rendermatic-XXXXXX.local 'echo \"OTA_URL=$OTA_URL\" | sudo tee /data/ota.conf'"
    else
        mount "$DATA_PART" "$MOUNT_DIR" && MOUNTED=true
    fi

    if $MOUNTED; then
        printf 'OTA_URL=%s\n' "$OTA_URL" > "$MOUNT_DIR/ota.conf"
        echo "Wrote ota.conf to data partition"
        umount "$MOUNT_DIR"
    fi
    rmdir "$MOUNT_DIR"
fi

echo ""
echo "=== Done ==="
echo ""
echo "Insert the media into your device and power on."
echo "The device will appear on your network as rendermatic-XXXXXX.local"
echo ""
echo "Connect via SSH:"
echo "  ssh render@rendermatic-XXXXXX.local"
