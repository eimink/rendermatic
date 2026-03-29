# NDI Support in Rendermatic

Rendermatic can receive real-time video over the network using [NDI](https://ndi.video/) (Network Device Interface). NDI is widely used in live production environments for low-latency video transport over standard Ethernet.

## How It Works

Rendermatic uses **dynamic loading** (`dlopen`) to load the NDI runtime at startup. The NDI SDK headers are vendored in the project (MIT-licensed), but the `libndi.so` shared library is **not included** in the Rendermatic binary or disk images.

- If `libndi.so` is present on the device, NDI features are fully available
- If `libndi.so` is absent, Rendermatic runs normally without NDI — all other features work

## Installing the NDI Runtime

### 1. Download the NDI SDK

Download the NDI SDK for Linux from [ndi.video](https://ndi.video/for-developers/ndi-sdk/). The SDK is free to download (registration required).

### 2. Extract `libndi.so`

The SDK ships as a shell script installer. Extract the runtime library for your architecture:

```bash
sh Install_NDI_SDK_v6_Linux.sh
# Accept the license agreement

# The library is at:
# x86_64:  NDI SDK for Linux/lib/x86_64-linux-gnu/libndi.so.6.3.1
# aarch64: NDI SDK for Linux/lib/aarch64-rpi4-linux-gnueabi/libndi.so.6.3.1
# armhf:   NDI SDK for Linux/lib/arm-rpi4-linux-gnueabihf/libndi.so.6.3.1
```

### 3. Deploy to device

Copy the library to the device's data partition:

```bash
scp "NDI SDK for Linux/lib/aarch64-rpi4-linux-gnueabi/libndi.so.6.3.1" \
    render@rendermatic-ca6bf5.local:/data/lib/libndi.so.6
```

Create the directory first if needed:

```bash
ssh render@rendermatic-ca6bf5.local "mkdir -p /data/lib"
```

Rendermatic searches for the library in these locations (in order):

| Path | Description |
|------|-------------|
| `/data/lib/libndi.so` | Recommended: writable data partition |
| `/data/lib/libndi.so.6` | Versioned name |
| `/usr/local/lib/libndi.so.6` | Standard system install |
| `/usr/local/lib/libndi.so` | Standard system install |
| `/usr/lib/libndi.so.6` | System library path |
| System `LD_LIBRARY_PATH` | Dynamic linker search path |

### 4. Restart Rendermatic

After placing the library, restart the service:

```bash
ssh render@rendermatic-ca6bf5.local "sudo rc-service rendermatic restart"
```

The startup log will show:

```
NDI: Loaded runtime from /data/lib/libndi.so.6
NDI: Initialized (6.3.1)
```

## WebSocket Commands

### `scan_ndi_sources`

Discover NDI sources on the local network. Waits up to 2 seconds for sources to appear.

```json
{"command": "scan_ndi_sources"}
```

Response:
```json
{
    "command": "ndi_sources",
    "sources": ["LAPTOP (OBS)", "SWITCHER (Camera 1)", "GRAPHICS (CasparCG)"],
    "success": true
}
```

### `set_ndi_source`

Connect to a specific NDI source by name. Starts the receiver if not already running.

```json
{"command": "set_ndi_source", "source": "LAPTOP (OBS)"}
```

Response:
```json
{
    "command": "set_ndi_source_response",
    "success": true,
    "source": "LAPTOP (OBS)"
}
```

### `get_ndi_status`

Query the current NDI connection state.

```json
{"command": "get_ndi_status"}
```

Response:
```json
{
    "command": "ndi_status",
    "connected": true,
    "source": "LAPTOP (OBS)",
    "success": true
}
```

### `stop_ndi`

Disconnect from the current NDI source.

```json
{"command": "stop_ndi"}
```

### Error: NDI runtime not installed

If the NDI runtime is not present on the device, NDI commands return:

```json
{
    "success": false,
    "message": "NDI runtime not installed. Place libndi.so in /data/lib/"
}
```

## Configuration

NDI mode can be enabled via `config.json`:

```json
{
    "ndiMode": true,
    "ndiSourceName": "LAPTOP (OBS)"
}
```

Or via command-line flag: `./rendermatic -n`

If `ndiSourceName` is empty, Rendermatic connects to the first NDI source it discovers.

## Platform Support

| Platform | NDI Runtime | Notes |
|----------|-------------|-------|
| Linux x86_64 | `lib/x86_64-linux-gnu/libndi.so.6.3.1` | Standard desktop/server |
| Linux aarch64 | `lib/aarch64-rpi4-linux-gnueabi/libndi.so.6.3.1` | Raspberry Pi 4/5, ARM VMs |
| Linux armhf | `lib/arm-rpi4-linux-gnueabihf/libndi.so.6.3.1` | Raspberry Pi (32-bit) |
| macOS | Via NDI SDK for macOS | Both Intel and Apple Silicon |

### Alpine Linux / musl libc

The NDI runtime libraries are built against glibc. On Alpine Linux (which uses musl), you may need the glibc compatibility layer:

```
apk add gcompat
```

## Licensing

The NDI SDK headers included in this project (`ndi/` directory) are licensed under the **MIT License** by Vizrt NDI AB. These headers define the NDI data types and the dynamic loading interface — they do not contain the NDI implementation.

The `libndi.so` runtime library is part of the **NDI SDK** and is subject to the [NDI SDK License Agreement](https://ndi.video/sdk-license/). Key points:

- The free NDI SDK license covers applications running on **general-purpose operating systems** (desktops, servers, laptops)
- Deployment on **fixed-purpose embedded devices** (appliances with restricted/read-only OS) requires a **commercial license** from Vizrt NDI AB
- Rendermatic does **not** distribute `libndi.so` — the deployer is responsible for obtaining and installing the NDI runtime in compliance with their own license terms

If you plan to deploy Rendermatic with NDI on embedded hardware (Raspberry Pi appliances, digital signage devices), contact [Vizrt NDI](https://ndi.video/) about the NDI Advanced SDK and commercial licensing.

## Architecture

Rendermatic uses dynamic loading to avoid any compile-time or link-time dependency on the NDI SDK:

1. The NDI headers (`ndi/Processing.NDI.*.h`) define types and the `NDIlib_v6` function table struct
2. At startup, `NDIReceiver::loadRuntime()` calls `dlopen()` to load `libndi.so`
3. It resolves `NDIlib_v6_load` via `dlsym()` to get the function pointer table
4. All NDI API calls go through this function table (`m_ndiLib->find_create_v2()`, etc.)
5. If `dlopen()` fails, NDI is simply unavailable — no crash, no error beyond an info log

This means:
- The Rendermatic binary has **zero dependency** on libndi at link time
- NDI support is a **runtime capability**, not a compile-time switch
- The same binary works with or without NDI installed
