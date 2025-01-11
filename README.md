# Rendermatic

A 'high-performance' rendering application

## Building from Source

### Prerequisites

- CMake 3.28 or higher
- C++20 compatible compiler, Clang preferred
- GLFW3 development files
- DirectFB (on Linux)
- Boost development files

#### Ubuntu/Debian
```bash
sudo apt-get install build-essential clang cmake libboost-system-dev libglfw3-dev libdirectfb-dev pkg-config
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

The application provides a WebSocket server for remote control. Default port is 9002 (configurable in config.json).

### Available Commands

Send commands as JSON objects. Examples:

```json
// Scan textures directory for new files
{"command": "scan_textures"}

// List available textures
{"command": "list_textures"}

// Load a specific texture
{"command": "load_texture", "texture": "texture_name.png"}

// Set current texture
{"command": "set_texture", "texture": "texture_name.png"}
```

### Response Format

Responses are JSON objects with the following structure:

```json
{
    "command": "command_name_response",
    "success": true/false,
    "textures": ["texture1.png", "texture2.png"]  // For list_textures and scan_textures
}
```

## Troubleshooting

Check service logs:
```bash
sudo journalctl -u rendermatic -f
```

Service configuration file location:
```bash
/etc/systemd/system/rendermatic.service
```
