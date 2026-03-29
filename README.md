# Rendermatic

A framebuffer rendering application for digital signage and media display. Renders images and video directly to the screen without a window manager — designed for embedded Linux devices (Raspberry Pi, VMs, bare-metal PCs).

Features:
- Direct framebuffer rendering via DirectFB (no X11/Wayland needed)
- WebSocket control API for remote management
- Video playback with playlist support (FFmpeg)
- mDNS service discovery
- API key authentication
- Read-only OS images with writable data partition

## Pre-built Images

Pre-built disk images are available from [GitHub Releases](../../releases) for:

| Image | Target |
|-------|--------|
| `rendermatic-rpi.img.gz` | Raspberry Pi 3/4/5 |
| `rendermatic-generic-arm64.img.gz` | Generic aarch64 VMs (VirtualBox, UTM, QEMU) |
| `rendermatic-x86_64.img.gz` | x86_64 PCs and VMs (VirtualBox, VMware) |

Images boot into a read-only Alpine Linux system with Rendermatic running automatically on the framebuffer. A writable data partition holds media files, config, and SSH keys. The display resolution is auto-detected from the connected display.

### Quick Start

#### 1. Flash the image

Use [Balena Etcher](https://etcher.balena.io/), Raspberry Pi Imager, or `dd`:

```bash
gunzip -c rendermatic-rpi.img.gz | sudo dd of=/dev/sdX bs=4M status=progress
```

#### 2. Add your SSH keys

After flashing, the **BOOT** partition will appear as a drive in your file manager.

**Option A: GitHub keys (easiest)**

Create a file called `github-ssh.txt` on the BOOT partition with your GitHub username(s):

```
eimink
other-teammate
```

On first boot, the device will fetch your public keys from GitHub automatically.

**Option B: Direct public key**

Copy your public key file to the BOOT partition as `authorized_keys`:

```bash
cp ~/.ssh/id_ed25519.pub /Volumes/BOOT/authorized_keys
```

#### 3. Boot and connect

Eject the media, insert it into the device, and power on. The device will:
- Get an IP address via DHCP
- Generate a unique hostname from its MAC address (e.g. `rendermatic-ca6bf5`)
- Advertise itself via mDNS

Connect via SSH:

```bash
ssh render@rendermatic-ca6bf5.local
```

The default password for the `render` user is `m4tic!` (SSH is key-only by default, password works on the console).

### Flash Script (advanced)

For automated/scripted flashing with key injection in one step:

```bash
# With a local SSH key
sudo ./image/flash.sh rendermatic-rpi.img.gz /dev/sdX --key ~/.ssh/id_ed25519.pub

# With GitHub keys
sudo ./image/flash.sh rendermatic-rpi.img.gz /dev/sdX --github eimink

# Multiple users
sudo ./image/flash.sh rendermatic-rpi.img.gz /dev/sdX --github eimink --github other-dev
```

### Image Partition Layout

| Partition | Size | Filesystem | Mount | Description |
|-----------|------|------------|-------|-------------|
| p1 | 128MB | FAT32 | /boot | Kernel, bootloader, SSH key provisioning files |
| p2 | ~255MB | ext4 | / | Read-only root filesystem |
| p3 | 128MB | ext4 | /data | Writable: media, config.json, logs, SSH keys |

### Delivering Content

Upload media files via SCP to the `render` user's data partition. Both images (.jpg, .png) and videos (.mp4, .mkv, .mov, .avi, .webm) go to the same `media/` directory:

```bash
scp my-texture.jpg render@rendermatic-ca6bf5.local:/data/media/
scp promo-video.mp4 render@rendermatic-ca6bf5.local:/data/media/
```

Then control via WebSocket:

```json
{"command": "scan_textures"}
{"command": "set_texture", "texture": "my-texture.jpg"}

{"command": "scan_videos"}
{"command": "play_video", "source": "promo-video.mp4"}

{"command": "set_playlist", "videos": ["intro.mp4", "loop.mp4"], "loop": true}
{"command": "start_playlist"}
```

You can also place a `playlist.m3u` file on the data partition for auto-play on boot:

```
#EXTM3U
#RENDERMATIC:LOOP=true
intro.mp4
main-content.mp4
```

### Building Images Locally

```bash
# Requires podman or docker
sh image/build.sh rpi            # Raspberry Pi
sh image/build.sh generic-arm64  # Generic aarch64 VM
sh image/build.sh x86_64         # x86_64 PC/VM

# With VMDK output for VirtualBox/VMware
VMDK=1 sh image/build.sh generic-arm64
```

---

## Building from Source

### Prerequisites

- CMake 3.28 or higher
- C++20 compatible compiler, Clang preferred
- GLFW3 development files
- DirectFB (on Linux)
- ASIO (fetched automatically by CMake)

#### Ubuntu/Debian
```bash
sudo apt-get install build-essential clang cmake libglfw3-dev libdirectfb-dev pkg-config
sudo update-alternatives --install /usr/bin/cc cc /usr/bin/clang 100
sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++ 100
sudo update-alternatives --config cc
sudo update-alternatives --config c++
```

### Compilation

```bash
# Standard build with all backends
mkdir build && cd build
cmake ..
cmake --build .

# DirectFB-only build (Linux)
mkdir build && cd build
cmake -DDFB_ONLY=ON ..
cmake --build .
```

## Configuration

The application can be configured through `config.json` with the following options:

```json
{
    "fullscreen": true,
    "fullscreenScaling": false,
    "monitorIndex": 0,
    "ndiMode": false,
    "backend": "glfw|dfb|dfb-pure",
    "width": 1920,
    "height": 1080,
    "wsPort": 9002
}
```

## Installation

Run the installation script as root:

```bash
sudo ./install.sh
```

This will:
1. Install the application to `/rendermatic`
2. Set up the systemd service
3. Configure auto-start on boot

## Running the Service

### Service Management
```bash
# Start the service
sudo systemctl start rendermatic

# Check status
sudo systemctl status rendermatic

# Stop the service
sudo systemctl stop rendermatic

# Enable/disable auto-start
sudo systemctl enable rendermatic
sudo systemctl disable rendermatic
```

### Manual Execution

To run manually with root privileges:

```bash
sudo /rendermatic/rendermatic
```

## WebSocket Interface

The application provides a WebSocket control API on port 9002 (configurable). See [WEBSOCKET-API.md](WEBSOCKET-API.md) for the full protocol reference.

Key command groups:
- **Textures** — `scan_textures`, `set_texture`, `load_texture`
- **Videos** — `scan_videos`, `play_video`, `stop_video`
- **Playlists** — `set_playlist`, `start_playlist`, `next_video`, `prev_video`
- **Device** — `get_device_info`, `set_device_name`, `identify`
- **Auth** — `authenticate`, `set_auth_key`, `clear_auth_key`
