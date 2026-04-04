#!/bin/sh
# WiFi provisioning and wpa_supplicant startup.
# Handles both first-boot (wifi.txt on /boot) and subsequent boots (/data/wifi/).

CONF="/data/wifi/wpa_supplicant.conf"

# Provision from boot partition if wifi.txt exists (first boot)
if [ -f /boot/wifi.txt ]; then
    ssid=$(sed -n '1p' /boot/wifi.txt)
    psk=$(sed -n '2p' /boot/wifi.txt)

    if [ -n "$ssid" ] && [ -n "$psk" ]; then
        mkdir -p /data/wifi
        wpa_passphrase "$ssid" "$psk" > "$CONF"
        echo "WiFi configured for SSID: $ssid"
    fi

    mount -o remount,rw /boot 2>/dev/null || true
    rm -f /boot/wifi.txt 2>/dev/null || true
    mount -o remount,ro /boot 2>/dev/null || true
fi

# No config — nothing to do
[ -f "$CONF" ] || exit 0

# Wait for a wireless interface to appear (firmware may load late)
attempt=0
while [ $attempt -lt 15 ]; do
    for iface in /sys/class/net/wl*; do
        [ -e "$iface" ] || continue
        ifname=$(basename "$iface")
        echo "Starting wpa_supplicant on $ifname"
        wpa_supplicant -B -i "$ifname" -c "$CONF"
        exit 0
    done
    attempt=$((attempt + 1))
    sleep 1
done

echo "No wireless interface found after 15s"
exit 0
