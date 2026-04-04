#!/bin/sh
# Mount data partition and bind writable paths into rendermatic

echo "Setting up rendermatic data paths"

# Check that data partition is mounted
if ! mountpoint -q /data; then
    echo "Data partition not mounted, attempting mount"
    mount /data || { echo "Failed to mount /data"; exit 1; }
fi

# Initialize data partition on first boot
if [ ! -f /data/.initialized ]; then
    echo "First boot: initializing data partition"
    mkdir -p /data/media /data/logs /data/ssh /data/lib
    cp /rendermatic/config.json.default /data/config.json
    cp /rendermatic/media/* /data/media/ 2>/dev/null || true
    touch /data/.initialized
fi

# --- SSH key provisioning from boot partition ---
keys_changed=false

# Option 1: GitHub username(s) in /boot/github-ssh.txt
if [ -f /boot/github-ssh.txt ]; then
    echo "Found github-ssh.txt on boot partition"
    mkdir -p /data/ssh
    while IFS= read -r username || [ -n "$username" ]; do
        # Skip empty lines and comments
        username=$(echo "$username" | sed 's/#.*//' | tr -d '[:space:]')
        [ -z "$username" ] && continue

        echo "Fetching SSH keys for GitHub user: $username"
        keys=$(wget -q -O - "https://github.com/${username}.keys" 2>/dev/null) || true
        if [ -n "$keys" ]; then
            echo "# GitHub: $username" >> /data/ssh/authorized_keys
            echo "$keys" >> /data/ssh/authorized_keys
            keys_changed=true
        else
            echo "Warning: No keys found for GitHub user: $username"
        fi
    done < /boot/github-ssh.txt

    # Remove the file so we don't re-import on next boot
    mount -o remount,rw /boot 2>/dev/null || true
    rm -f /boot/github-ssh.txt
    mount -o remount,ro /boot 2>/dev/null || true
fi

# Option 2: Direct authorized_keys file on boot partition
if [ -f /boot/authorized_keys ]; then
    echo "Found authorized_keys on boot partition"
    mkdir -p /data/ssh
    cat /boot/authorized_keys >> /data/ssh/authorized_keys
    keys_changed=true

    mount -o remount,rw /boot 2>/dev/null || true
    rm -f /boot/authorized_keys
    mount -o remount,ro /boot 2>/dev/null || true
fi

if $keys_changed; then
    sort -u /data/ssh/authorized_keys -o /data/ssh/authorized_keys
    echo "SSH keys provisioned successfully"
fi

# Ensure render user owns the data partition
chown -R render:render /data

# Bind mount writable paths into rendermatic directory
mount --bind /data/media /rendermatic/media
mount --bind /data/config.json /rendermatic/config.json

# Set up SSH authorized_keys for render user
if [ -f /data/ssh/authorized_keys ]; then
    mkdir -p /data/.ssh
    cp /data/ssh/authorized_keys /data/.ssh/authorized_keys
    chown -R render:render /data/.ssh
    chmod 700 /data/.ssh
    chmod 600 /data/.ssh/authorized_keys
fi

echo "Data paths ready"
