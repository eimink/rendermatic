#!/bin/sh
#
# Flash a Rendermatic image to a disk and optionally provision SSH keys.
#
# Usage:
#   ./flash.sh <image.img.gz> <device> [options]
#
# Options:
#   --key <path>       Add an SSH public key file
#   --github <user>    Fetch SSH keys from GitHub on first boot
#
# Examples:
#   ./flash.sh rendermatic-rpi.img.gz /dev/sdX --key ~/.ssh/id_ed25519.pub
#   ./flash.sh rendermatic-generic-arm64.img.gz /dev/sdX --github eimink
#   ./flash.sh rendermatic-x86_64.img.gz /dev/sdX --github user1 --github user2
#
set -e

IMAGE="$1"
DEVICE="$2"
shift 2 || true

if [ -z "$IMAGE" ] || [ -z "$DEVICE" ]; then
    echo "Usage: $0 <image.img.gz> <device> [--key <pubkey>] [--github <user>]"
    exit 1
fi

if [ ! -f "$IMAGE" ]; then
    echo "Error: Image not found: $IMAGE"
    exit 1
fi

if [ ! -b "$DEVICE" ]; then
    echo "Error: Not a block device: $DEVICE"
    exit 1
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
gunzip -c "$IMAGE" | dd of="$DEVICE" bs=4M status=progress
sync

# Mount boot partition to inject keys
BOOT_PART="${DEVICE}1"
# Handle partition naming for nvme/mmcblk devices
case "$DEVICE" in
    *nvme*|*mmcblk*|*loop*) BOOT_PART="${DEVICE}p1" ;;
esac

if [ -n "$KEYS" ] || [ -n "$GITHUB_USERS" ]; then
    echo "--- Provisioning SSH keys ---"
    MOUNT_DIR=$(mktemp -d)

    mount "$BOOT_PART" "$MOUNT_DIR"

    if [ -n "$KEYS" ]; then
        echo "$KEYS" > "$MOUNT_DIR/authorized_keys"
        echo "Wrote authorized_keys to boot partition"
    fi

    if [ -n "$GITHUB_USERS" ]; then
        echo "$GITHUB_USERS" > "$MOUNT_DIR/github-ssh.txt"
        echo "Wrote github-ssh.txt to boot partition"
    fi

    umount "$MOUNT_DIR"
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
