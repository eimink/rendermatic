# Alpine Linux Setup for Rendermatic

Run Rendermatic directly on a Linux framebuffer without a window manager. This guide covers Alpine Linux setup from scratch through to a production deployment where the device boots straight into rendering.

Tested on Alpine Standard 3.21.2 (aarch64). Works on both physical hardware and VMs (UTM recommended for Apple Silicon Macs).

## Alpine Installation

Boot with the Alpine ISO and log in as `root` (no password).

```sh
setup-alpine
```

During setup:
- Enable SSH and add your public key
- Choose your disk (e.g. `vda`) and select **sys** mode for persistent install

Eject the ISO and reboot, then SSH in.

## Package Setup

Enable community repository:

```sh
sed -i 's|#\(.*community\)|\1|' /etc/apk/repositories
apk update
```

Install build tools and dependencies:

```sh
# Build essentials
apk add git cmake clang clang-dev make gcc g++ libc-dev linux-headers

# DirectFB (framebuffer rendering)
apk add directfb directfb-dev

# FFmpeg (video/stream playback)
apk add ffmpeg-dev

# Avahi (mDNS device discovery)
apk add avahi avahi-dev dbus
```

### Start Avahi (mDNS)

Avahi requires D-Bus. Enable both services so they persist across reboots:

```sh
rc-update add dbus default
rc-update add avahi-daemon default
rc-service dbus start
rc-service avahi-daemon start
```

## Build

```sh
git clone https://github.com/eimink/rendermatic.git
cd rendermatic
mkdir build && cd build
cmake -DDFB_PURE_ONLY=ON ..
cmake --build .
```

This builds with the pure DirectFB renderer — no OpenGL, no window manager, direct framebuffer access.

Verify the binary runs:

```sh
cd bin
./rendermatic
```

You should see output on the connected display. Press ESC or Ctrl+C to stop.

## DirectFB Configuration

Create `/etc/directfbrc` to configure framebuffer access:

```sh
cat > /etc/directfbrc << 'EOF'
system=fbdev
no-cursor
EOF
```

If your framebuffer device is not `/dev/fb0`, specify it:

```
system=fbdev
fbdev=/dev/fb1
no-cursor
```

### Verify framebuffer access

```sh
# Check device exists
ls -l /dev/fb0

# Check resolution
cat /sys/class/graphics/fb0/virtual_size
```

If `/dev/fb0` doesn't exist, you may need a kernel framebuffer driver. For common hardware:

```sh
# Load framebuffer module (example for simple framebuffer)
modprobe simplefb

# Or for Intel
modprobe i915

# Make it persistent
echo "simplefb" >> /etc/modules
```

## Install as Service

From the project root:

```sh
sudo ./install.sh
```

This will:
1. Install the binary, shaders, textures, and config to `/rendermatic`
2. Detect OpenRC and install the init script
3. Enable auto-start on boot

### Service Management

```sh
# Start / stop / restart
rc-service rendermatic start
rc-service rendermatic stop
rc-service rendermatic restart

# Check status
rc-service rendermatic status

# View logs
tail -f /var/log/rendermatic.log

# Disable auto-start
rc-update del rendermatic default
```

## Configuration

Edit `/rendermatic/config.json`:

```json
{
    "fullscreen": true,
    "fullscreenScaling": true,
    "backend": "dfb-pure",
    "width": 1920,
    "height": 1080,
    "wsPort": 9002,
    "instanceName": "",
    "authKeyHash": ""
}
```

Key settings for headless operation:
- `fullscreen: true` — use the entire display
- `fullscreenScaling: true` — scale content to fill the screen
- `backend: "dfb-pure"` — forced at compile time with `DFB_PURE_ONLY`, but good to have explicit
- `instanceName` — auto-generates from hostname if empty

After editing, restart the service:

```sh
rc-service rendermatic restart
```

## Network Control

Once running, the device is controllable over the network:

- **WebSocket:** `ws://<device-ip>:9002`
- **mDNS discovery:** `avahi-browse -r _rendermatic._tcp`

See [WEBSOCKET-API.md](WEBSOCKET-API.md) for the full command reference.

## Uploading Textures

Place image files (PNG, JPG) in `/rendermatic/textures/`, then tell rendermatic to pick them up:

```json
{"command": "scan_textures"}
{"command": "set_texture", "texture": "my-image.png"}
```

Or via SCP:

```sh
scp my-image.png root@<device-ip>:/rendermatic/textures/
```

## Troubleshooting

### "Framebuffer device /dev/fb0 not found"

The kernel doesn't have a framebuffer driver loaded. Check available modules:

```sh
ls /lib/modules/$(uname -r)/kernel/drivers/video/
```

Load the appropriate one and add it to `/etc/modules` for persistence.

### "Failed to initialize DirectFB"

Check that DirectFB can access the framebuffer:

```sh
# Run DirectFB test
dfbinfo
```

If running over SSH, DirectFB may fail because the TTY isn't active. Run directly on the console or ensure the service has framebuffer access.

### "Failed to load default texture"

The `textures/` directory needs at least a `default.jpg` file. The install script copies the bundled textures, but if they're missing:

```sh
ls /rendermatic/textures/
```

### Service won't start on boot

```sh
# Verify it's in the default runlevel
rc-update show default | grep rendermatic

# Check if framebuffer is available at boot time
dmesg | grep -i framebuffer
```

### Video playback doesn't work

Ensure FFmpeg was detected during the build:

```sh
# Rebuild and check cmake output
cmake -DDFB_PURE_ONLY=ON .. 2>&1 | grep -i ffmpeg
```

You should see `Found FFmpeg for video/stream support`. If not, install `ffmpeg-dev` and rebuild.

### mDNS discovery doesn't work

```sh
# Check services are running
rc-service dbus status
rc-service avahi-daemon status

# Test locally
avahi-browse -a
```
