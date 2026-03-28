#!/bin/sh

set -e

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run as root"
    exit 1
fi

INSTALL_DIR="/rendermatic"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_BIN="${SCRIPT_DIR}/build/bin"

if [ ! -f "${BUILD_BIN}/rendermatic" ]; then
    echo "Error: Build not found at ${BUILD_BIN}/rendermatic"
    echo "Run cmake --build first, then re-run this script from the project root."
    exit 1
fi

echo "Installing rendermatic to ${INSTALL_DIR}..."

install -d "${INSTALL_DIR}/shaders" "${INSTALL_DIR}/textures"
install -m 755 "${BUILD_BIN}/rendermatic" "${INSTALL_DIR}/"
cp -r "${BUILD_BIN}/shaders/"* "${INSTALL_DIR}/shaders/"
cp -r "${BUILD_BIN}/textures/"* "${INSTALL_DIR}/textures/"

# Preserve existing config if present, otherwise install default
if [ ! -f "${INSTALL_DIR}/config.json" ]; then
    cp "${BUILD_BIN}/config.json" "${INSTALL_DIR}/"
else
    echo "Keeping existing config.json"
fi

chown -R root:root "${INSTALL_DIR}"
chmod -R 755 "${INSTALL_DIR}"

# Detect init system and install appropriate service
if command -v systemctl >/dev/null 2>&1; then
    echo "Detected systemd"
    install -m 644 "${SCRIPT_DIR}/service/rendermatic.service" /etc/systemd/system/
    systemctl daemon-reload
    systemctl enable rendermatic
    echo "Service enabled. Start with: systemctl start rendermatic"

elif command -v rc-update >/dev/null 2>&1; then
    echo "Detected OpenRC"
    install -m 755 "${SCRIPT_DIR}/service/rendermatic.initd" /etc/init.d/rendermatic
    rc-update add rendermatic default
    echo "Service enabled. Start with: rc-service rendermatic start"

else
    echo "Warning: No supported init system detected (systemd or OpenRC)"
    echo "You will need to start rendermatic manually: ${INSTALL_DIR}/rendermatic"
fi

echo "Installation complete!"
