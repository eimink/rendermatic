#!/bin/sh
# Display IP address on console when rendermatic is not running.

# Only run if rendermatic is not active
if systemctl is-active --quiet rendermatic.service; then
    exit 0
fi

# Wait for network to come up
sleep 5

# Get IP addresses
ips=""
for iface in /sys/class/net/*; do
    name=$(basename "$iface")
    [ "$name" = "lo" ] && continue
    addr=$(ip -4 addr show "$name" 2>/dev/null | sed -n 's/.*inet \([0-9.]*\).*/\1/p')
    [ -n "$addr" ] && ips="${ips}  ${name}: ${addr}\n"
done

hostname=$(hostname)

# Write to virtual console
cat > /dev/tty0 << EOF


  Rendermatic - service not running

  Hostname: ${hostname}
$(printf "  %b" "${ips:-  No network detected\n}")
  SSH: ssh render@${hostname}.local

  To start: sudo systemctl start rendermatic
  Logs:     sudo journalctl -u rendermatic

EOF
