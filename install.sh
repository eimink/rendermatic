#!/bin/bash

set -e  # Exit on any error

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root"
    exit 1
fi

INSTALL_DIR="/rendermatic"
BUILD_DIR="build"

# Install files
echo "Installing files..."
install -d ${INSTALL_DIR}/{bin,shaders,textures}
install -m 755 bin/rendermatic ${INSTALL_DIR}/
cp -r ../shaders/* ${INSTALL_DIR}/shaders/
cp -r ../textures/* ${INSTALL_DIR}/textures/
cp ../config.json ${INSTALL_DIR}/

# Install service file
install -m 644 ../.vscode/service/rendermatic.service /etc/systemd/system/

# Set permissions
chown -R root:root ${INSTALL_DIR}
chmod -R 755 ${INSTALL_DIR}

# Enable and start service
systemctl daemon-reload
systemctl enable rendermatic
systemctl start rendermatic

echo "Installation complete! Rendermatic is now running as a service."
echo "Check status with: systemctl status rendermatic"
