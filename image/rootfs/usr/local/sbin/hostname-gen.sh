#!/bin/sh
# Generate unique hostname from MAC address

mac=""
for iface in /sys/class/net/*; do
    [ -e "$iface/address" ] || continue
    name=$(basename "$iface")
    [ "$name" = "lo" ] && continue
    mac=$(cat "$iface/address")
    [ -n "$mac" ] && [ "$mac" != "00:00:00:00:00:00" ] && break
    mac=""
done

if [ -n "$mac" ]; then
    suffix=$(echo "$mac" | awk -F: '{printf "%s%s%s", $4, $5, $6}')
    newhost="rendermatic-${suffix}"
    hostnamectl set-hostname "$newhost" 2>/dev/null || hostname "$newhost"
    echo "Hostname set to $newhost"
else
    hostnamectl set-hostname "rendermatic" 2>/dev/null || hostname "rendermatic"
    echo "Warning: No MAC address found, using default hostname"
fi
