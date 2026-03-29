# Rendermatic WebSocket Control API

Protocol reference for implementing **rendermatic-control** software. This document covers device discovery, connection, and the complete command set available over the WebSocket interface.

## Device Discovery

Rendermatic devices advertise themselves on the local network using mDNS (Bonjour/Avahi).

| Property       | Value                    |
|----------------|--------------------------|
| Service type   | `_rendermatic._tcp`      |
| Default port   | `9002`                   |
| Instance name  | Configurable per device  |

The instance name defaults to `rendermatic-{hostname}` when not explicitly set. It can be changed at runtime via the `set_device_name` command and is persisted across restarts.

### Discovery example (DNS-SD)

```bash
# macOS / Bonjour
dns-sd -B _rendermatic._tcp

# Linux / Avahi
avahi-browse -r _rendermatic._tcp
```

The browse result provides the device's IP address and WebSocket port. Connect to `ws://{ip}:{port}`.

## Connection

- **Protocol:** WebSocket (RFC 6455), no TLS
- **Default port:** `9002` (configurable via device `config.json` `wsPort` field)
- **Message format:** JSON text frames, one command per frame
- **Direction:** Request/response. The client sends a command, the server responds on the same connection.
- **Server-initiated message:** On connection open, the server sends an `auth_status` message indicating whether authentication is required (see [Authentication](#authentication)).

### Connection example (JavaScript)

```javascript
const ws = new WebSocket("ws://192.168.1.100:9002");

ws.onmessage = (event) => {
    const response = JSON.parse(event.data);

    if (response.command === "auth_status" && response.authRequired) {
        // Device requires authentication
        ws.send(JSON.stringify({ command: "authenticate", key: "my-secret-key" }));
        return;
    }

    if (response.command === "auth_response" && response.success) {
        // Authenticated — now safe to send commands
        ws.send(JSON.stringify({ command: "get_device_info" }));
        return;
    }

    console.log(response);
};
```

## Message Format

### Request

All requests are JSON objects with a required `command` field and optional parameters:

```json
{
    "command": "command_name",
    "param1": "value1"
}
```

### Response

All responses are JSON objects containing at minimum:

| Field     | Type    | Description                                     |
|-----------|---------|-------------------------------------------------|
| `command` | string  | Response type identifier (see per-command docs)  |
| `success` | boolean | Whether the command executed successfully         |
| `message` | string  | Error description (present only when `success` is `false`) |

Additional fields are command-specific and documented below.

### Error response (unknown command)

```json
{
    "command": "error",
    "message": "Unknown command",
    "success": false
}
```

---

## Authentication

Rendermatic supports optional API key authentication. When no key is configured, the device operates in **open mode** — all commands are accepted without authentication (backward compatible). When a key is set, clients must authenticate before issuing commands.

### How it works

1. On WebSocket connect, the server immediately sends an `auth_status` message
2. If `authRequired` is `true`, the client must send an `authenticate` command with the correct key
3. Once authenticated, all commands are accepted for the duration of the connection
4. If `authRequired` is `false` (open mode), no authentication is needed

### Server-initiated message on connect

**Open mode (no key configured):**
```json
{
    "command": "auth_status",
    "authRequired": false,
    "authenticated": true
}
```

**Auth enabled:**
```json
{
    "command": "auth_status",
    "authRequired": true,
    "authenticated": false
}
```

### Connection states

| State              | When                              | Allowed commands                           |
|--------------------|-----------------------------------|--------------------------------------------|
| Open mode          | No key configured on device       | All commands                               |
| Unauthenticated    | Key exists, not yet authenticated | `authenticate`, `get_device_info` (reduced)|
| Authenticated      | Successfully authenticated        | All commands                               |

### Unauthenticated behavior

When auth is enabled and the connection is not yet authenticated:

- **`authenticate`** is always accepted
- **`get_device_info`** returns a reduced response (only `instanceName` and `authRequired`, no operational details)
- **`identify`** is always accepted (physical operation for device location)
- **All other commands** return:
  ```json
  { "command": "auth_required", "message": "Authentication required", "success": false }
  ```

### Rate limiting

Authentication attempts are rate-limited **per IP address** (not per connection) to prevent brute-force attacks.

| Parameter          | Value       |
|--------------------|-------------|
| Max failed attempts | 5          |
| Lockout duration    | 60 seconds |

After 5 consecutive failed attempts from the same IP, further `authenticate` commands are rejected with a `retryAfterSeconds` field indicating when to try again. The counter resets on successful authentication or after the lockout expires.

### Key storage

The authentication key is stored as a SHA-256 hash in `config.json` under the `authKeyHash` field. The plaintext key is never written to disk. To set the initial key, connect while the device is in open mode and use the `set_auth_key` command.

---

## Commands

### Authentication

#### `authenticate`

Authenticates the current connection. Only needed when `authRequired` is `true`.

**Request:**
```json
{ "command": "authenticate", "key": "my-secret-key" }
```

| Parameter | Type   | Required | Description        |
|-----------|--------|----------|--------------------|
| `key`     | string | yes      | The authentication key |

**Response (success):**
```json
{
    "command": "auth_response",
    "success": true,
    "message": "Authenticated successfully"
}
```

**Response (invalid key):**
```json
{
    "command": "auth_response",
    "success": false,
    "message": "Invalid authentication key"
}
```

**Response (rate limited):**
```json
{
    "command": "auth_response",
    "success": false,
    "message": "Too many failed attempts. Try again later.",
    "retryAfterSeconds": 45
}
```

---

#### `set_auth_key`

Sets or updates the authentication key. Requires an authenticated session (or open mode). Minimum key length is 8 characters.

When called in open mode for the first time, the issuing connection is automatically marked as authenticated.

**Request:**
```json
{ "command": "set_auth_key", "key": "new-secret-key-here" }
```

| Parameter | Type   | Required | Description                       |
|-----------|--------|----------|-----------------------------------|
| `key`     | string | yes      | New auth key (minimum 8 characters) |

**Response (success):**
```json
{
    "command": "set_auth_key_response",
    "success": true,
    "message": "Authentication key updated"
}
```

**Response (failure):**
```json
{
    "command": "set_auth_key_response",
    "success": false,
    "message": "Key must be at least 8 characters"
}
```

---

#### `clear_auth_key`

Removes the authentication key and returns the device to open mode. Requires an authenticated session.

**Request:**
```json
{ "command": "clear_auth_key" }
```

**Response:**
```json
{
    "command": "clear_auth_key_response",
    "success": true,
    "message": "Authentication disabled. All connections are now open."
}
```

---

#### `get_auth_status`

Returns the current authentication configuration and connection state.

**Request:**
```json
{ "command": "get_auth_status" }
```

**Response:**
```json
{
    "command": "auth_status",
    "authEnabled": true,
    "authenticated": true,
    "success": true
}
```

| Field           | Type | Description                                 |
|-----------------|------|---------------------------------------------|
| `authEnabled`   | bool | Whether an auth key is configured on device |
| `authenticated` | bool | Whether the current connection is authenticated |

---

### Device

#### `get_device_info`

Returns information about the device.

**Request:**
```json
{ "command": "get_device_info" }
```

**Response (authenticated or open mode):**
```json
{
    "command": "device_info",
    "instanceName": "rendermatic-living-room",
    "hostname": "rpi-display-01",
    "wsPort": 9002,
    "currentTexture": "logo.png",
    "authEnabled": true,
    "ndiAvailable": true,
    "ndiMode": true,
    "ndiConnected": true,
    "ndiSource": "LAPTOP (OBS)",
    "success": true
}
```

| Field            | Type   | Description                             |
|------------------|--------|-----------------------------------------|
| `instanceName`   | string | Device's configured display name        |
| `hostname`       | string | System hostname                         |
| `wsPort`         | int    | WebSocket server port                   |
| `currentTexture` | string | Filename of the currently active texture |
| `authEnabled`    | bool   | Whether auth key is configured          |
| `ndiAvailable`   | bool   | Whether NDI runtime is installed on device (hide NDI UI if `false`) |
| `ndiMode`        | bool   | Whether NDI mode is currently active    |
| `ndiConnected`   | bool   | Whether an NDI source is currently connected |
| `ndiSource`      | string | Name of the connected/configured NDI source |

**Response (unauthenticated, reduced):**
```json
{
    "command": "device_info",
    "instanceName": "rendermatic-living-room",
    "authRequired": true,
    "authenticated": false,
    "success": true
}
```

---

#### `set_device_name`

Updates the device's instance name. The change is persisted to config and the mDNS advertisement is updated immediately.

**Request:**
```json
{ "command": "set_device_name", "name": "lobby-screen-1" }
```

| Parameter | Type   | Required | Description       |
|-----------|--------|----------|-------------------|
| `name`    | string | yes      | New instance name |

**Response (success):**
```json
{
    "command": "device_name_response",
    "instanceName": "lobby-screen-1",
    "success": true
}
```

**Response (failure):**
```json
{
    "command": "device_name_response",
    "message": "Missing 'name' field",
    "success": false
}
```

---

### Identify

#### `identify`

Triggers a lower-third overlay on the device's screen showing its hostname, IP address, and WebSocket port. Used to visually locate a physical device on the network. The overlay is composited on top of whatever content is currently playing (textures, video, or streams) and dismisses automatically after the specified duration.

This command is allowed **without authentication** — it is a physical operation useful for device location and does not expose sensitive data.

**Request:**
```json
{ "command": "identify", "duration": 10 }
```

| Parameter  | Type | Required | Default | Description                          |
|------------|------|----------|---------|--------------------------------------|
| `duration` | int  | no       | `10`    | How long to show the overlay (1–60s) |

**Response:**
```json
{
    "command": "identify_response",
    "success": true,
    "duration": 10
}
```

The overlay also appears automatically on boot for a configurable duration (default 5 seconds, controlled by `splashDurationSeconds` in `config.json`, set to `0` to disable).

---

### Textures

Rendermatic can display static images (PNG, JPG/JPEG) from its `textures/` directory.

#### `scan_textures`

Scans the device's texture directory for image files and returns the updated list.

**Request:**
```json
{ "command": "scan_textures" }
```

**Response:**
```json
{
    "command": "scan_textures_response",
    "textures": ["logo.png", "background.jpg", "overlay.png"],
    "success": true
}
```

| Field      | Type     | Description                       |
|------------|----------|-----------------------------------|
| `textures` | string[] | All discovered texture filenames  |

---

#### `list_textures`

Returns textures already known in memory (does not rescan the filesystem).

**Request:**
```json
{ "command": "list_textures" }
```

**Response:**
```json
{
    "command": "texture_list",
    "textures": ["logo.png", "background.jpg"],
    "success": true
}
```

---

#### `load_texture`

Pre-loads a texture file from disk into memory. This is optional — `set_texture` auto-loads if needed. Useful for pre-warming the cache before switching. The texture must exist in the `textures/` directory.

**Request:**
```json
{ "command": "load_texture", "texture": "logo.png" }
```

| Parameter  | Type   | Required | Description                 |
|------------|--------|----------|-----------------------------|
| `texture`  | string | yes      | Filename of texture to load |

**Response:**
```json
{
    "command": "load_texture_response",
    "success": true
}
```

---

#### `set_texture`

Sets which texture is currently displayed on screen. Automatically loads the texture from disk if it is not already in memory. Previously loaded textures that haven't been used for 30 seconds are automatically unloaded to free memory.

**Request:**
```json
{ "command": "set_texture", "texture": "logo.png" }
```

| Parameter  | Type   | Required | Description                    |
|------------|--------|----------|--------------------------------|
| `texture`  | string | yes      | Filename of texture to display |

**Response:**
```json
{
    "command": "set_texture_response",
    "success": true
}
```

---

### Media Scanning

The device stores both textures and videos in a shared `media/` directory. Files are differentiated by extension.

#### `scan_videos`

Scans the device's media directory for video files and returns the updated list.

**Request:**
```json
{ "command": "scan_videos" }
```

**Response:**
```json
{
    "command": "scan_videos_response",
    "videos": ["intro.mp4", "loop.mkv", "promo.mov"],
    "success": true
}
```

| Field    | Type     | Description                                          |
|----------|----------|------------------------------------------------------|
| `videos` | string[] | All discovered video filenames (.mp4, .mkv, .mov, .avi, .webm, .flv) |

---

#### `list_videos`

Returns videos already known from the last scan (does not rescan the filesystem).

**Request:**
```json
{ "command": "list_videos" }
```

**Response:**
```json
{
    "command": "video_list",
    "videos": ["intro.mp4", "loop.mkv"],
    "success": true
}
```

---

### Video Playback

Video commands are available when the device is built with FFmpeg support. If FFmpeg is not available, these commands return `success: false` with an appropriate message.

When video playback is active, it takes priority over texture display. Stopping video returns the device to showing the current texture.

Local video filenames are resolved from the `media/` directory. Stream URLs (rtmp://, rtsp://, etc.) are used as-is.

#### `play_video`

Starts playback of a video file or network stream. If a video is already playing, it is stopped before starting the new source.

**Request:**
```json
{ "command": "play_video", "source": "rtmp://stream.example.com/live/feed", "loop": false }
```

| Parameter | Type   | Required | Default | Description                                              |
|-----------|--------|----------|---------|----------------------------------------------------------|
| `source`  | string | yes      | --      | Video file path or stream URL                            |
| `loop`    | bool   | no       | `true`  | Loop when reaching end of file (ignored for live streams) |

**Supported source types:**

| Source        | Format                              | Loop behavior         |
|---------------|-------------------------------------|-----------------------|
| Local file    | `/path/to/video.mp4`                | Loops by default      |
| RTMP stream   | `rtmp://host/app/stream`            | Loop setting ignored  |
| RTSP stream   | `rtsp://host:port/path`             | Loop setting ignored  |
| SRT stream    | `srt://host:port`                   | Loop setting ignored  |
| HTTP/HLS      | `http://host/stream.m3u8`           | Loop setting ignored  |

Stream sources (URLs containing `://`) are configured with automatic reconnection and a 5-second connection timeout. RTSP uses TCP transport.

**Response (success):**
```json
{
    "command": "play_video_response",
    "success": true
}
```

**Response (failure):**
```json
{
    "command": "play_video_response",
    "message": "Failed to open video source",
    "success": false
}
```

---

#### `stop_video`

Stops the current video playback and returns to texture display mode.

**Request:**
```json
{ "command": "stop_video" }
```

**Response:**
```json
{
    "command": "stop_video_response",
    "success": true
}
```

---

#### `get_video_status`

Returns the current video playback state and source metadata.

**Request:**
```json
{ "command": "get_video_status" }
```

**Response (video active):**
```json
{
    "command": "video_status",
    "active": true,
    "source": "rtmp://stream.example.com/live/feed",
    "width": 1920,
    "height": 1080,
    "fps": 29.97,
    "duration": -1,
    "codec": "h264",
    "success": true
}
```

**Response (no video playing):**
```json
{
    "command": "video_status",
    "active": false,
    "source": "",
    "width": 0,
    "height": 0,
    "fps": 0,
    "duration": -1,
    "codec": "",
    "success": true
}
```

| Field      | Type   | Description                                          |
|------------|--------|------------------------------------------------------|
| `active`   | bool   | `true` if video is currently playing                 |
| `source`   | string | The source path or URL                               |
| `width`    | int    | Frame width in pixels                                |
| `height`   | int    | Frame height in pixels                               |
| `fps`      | double | Frames per second                                    |
| `duration` | double | Duration in seconds; `-1` for live streams           |
| `codec`    | string | Video codec name (e.g. `h264`, `hevc`, `vp9`)       |

---

### Playlists

Playlists allow playing a sequence of videos in order, with optional looping of the entire list. When a non-looping playlist finishes, the last frame of the last video remains on screen.

Playlists can be set via WebSocket or loaded from a `playlist.m3u` file on the data partition.

#### `set_playlist`

Sets an ordered list of video sources to play. Sources can be local filenames (resolved from `media/`) or stream URLs. Persisted to `config.json`.

**Request:**
```json
{ "command": "set_playlist", "videos": ["intro.mp4", "main-loop.mp4", "outro.mp4"], "loop": true }
```

| Parameter | Type     | Required | Default | Description                    |
|-----------|----------|----------|---------|--------------------------------|
| `videos`  | string[] | yes      | --      | Ordered list of video sources  |
| `loop`    | bool     | no       | `true`  | Loop the entire playlist       |

**Response:**
```json
{
    "command": "set_playlist_response",
    "success": true,
    "count": 3
}
```

---

#### `start_playlist`

Begins playing the playlist from the specified index (default 0).

**Request:**
```json
{ "command": "start_playlist", "index": 0 }
```

| Parameter | Type | Required | Default | Description               |
|-----------|------|----------|---------|---------------------------|
| `index`   | int  | no       | `0`     | Start from this position  |

**Response:**
```json
{
    "command": "start_playlist_response",
    "success": true
}
```

---

#### `stop_playlist`

Stops playlist playback. The current frame remains on screen.

**Request:**
```json
{ "command": "stop_playlist" }
```

**Response:**
```json
{
    "command": "stop_playlist_response",
    "success": true
}
```

---

#### `next_video`

Skips to the next video in the playlist. Wraps to the beginning if looping is enabled.

**Request:**
```json
{ "command": "next_video" }
```

**Response:**
```json
{
    "command": "next_video_response",
    "success": true,
    "currentIndex": 2
}
```

---

#### `prev_video`

Skips to the previous video in the playlist. Wraps to the end if looping is enabled.

**Request:**
```json
{ "command": "prev_video" }
```

**Response:**
```json
{
    "command": "prev_video_response",
    "success": true,
    "currentIndex": 0
}
```

---

#### `get_playlist_status`

Returns the current playlist state.

**Request:**
```json
{ "command": "get_playlist_status" }
```

**Response:**
```json
{
    "command": "playlist_status",
    "active": true,
    "videos": ["intro.mp4", "main-loop.mp4", "outro.mp4"],
    "currentIndex": 1,
    "currentSource": "main-loop.mp4",
    "loop": true,
    "success": true
}
```

| Field          | Type     | Description                          |
|----------------|----------|--------------------------------------|
| `active`       | bool     | Whether playlist is currently playing |
| `videos`       | string[] | The playlist contents                |
| `currentIndex` | int      | Index of the currently playing video |
| `currentSource`| string   | Filename/URL of the current video    |
| `loop`         | bool     | Whether the playlist loops           |

---

#### `playlist.m3u` file format

Place a `playlist.m3u` file on the data partition for persistent playlists that load on boot. Standard M3U format — one file per line, comments start with `#`:

```
#EXTM3U
#RENDERMATIC:LOOP=true
intro.mp4
main-loop.mp4
outro.mp4
```

The `#RENDERMATIC:LOOP=false` directive disables playlist looping (default is `true`). Standard `#EXTINF` lines are accepted and ignored.

---

### NDI

NDI (Network Device Interface) commands allow receiving real-time video from NDI sources on the local network. NDI commands are only functional when the NDI runtime library is installed on the device — check `ndiAvailable` in the `get_device_info` response. See [NDI.md](NDI.md) for installation and setup details.

When NDI is active, the NDI video feed takes priority over static textures (but not video playback). Disconnecting from NDI or stopping NDI mode automatically reverts the display to the current texture.

#### `scan_ndi_sources`

Discovers NDI sources on the local network. Waits up to 2 seconds for sources to appear.

**Request:**
```json
{ "command": "scan_ndi_sources" }
```

**Response:**
```json
{
    "command": "ndi_sources",
    "sources": ["LAPTOP (OBS)", "SWITCHER (Camera 1)", "GRAPHICS (CasparCG)"],
    "success": true
}
```

| Field     | Type     | Description                          |
|-----------|----------|--------------------------------------|
| `sources` | string[] | Discovered NDI source names          |

---

#### `set_ndi_source`

Connects to a specific NDI source by name. Starts the receiver if not already running. The connection is asynchronous — the response includes the current `connected` state, which will typically be `false` until the receiver thread establishes the connection. Poll `get_ndi_status` to track when the connection is established.

Setting an NDI source automatically enables NDI mode and persists the source name and mode to `config.json`. A specific source name is required — the receiver will not auto-connect to arbitrary sources.

**Request:**
```json
{ "command": "set_ndi_source", "source": "LAPTOP (OBS)" }
```

| Parameter | Type   | Required | Description               |
|-----------|--------|----------|---------------------------|
| `source`  | string | yes      | NDI source name to connect to |

**Response (success):**
```json
{
    "command": "set_ndi_source_response",
    "success": true,
    "source": "LAPTOP (OBS)",
    "connected": false
}
```

| Field       | Type   | Description                                 |
|-------------|--------|---------------------------------------------|
| `source`    | string | The requested source name                   |
| `connected` | bool   | Whether the receiver is connected (typically `false` initially) |

---

#### `get_ndi_status`

Returns the current NDI connection state.

**Request:**
```json
{ "command": "get_ndi_status" }
```

**Response:**
```json
{
    "command": "ndi_status",
    "connected": true,
    "source": "LAPTOP (OBS)",
    "success": true
}
```

| Field       | Type   | Description                             |
|-------------|--------|-----------------------------------------|
| `connected` | bool   | Whether an NDI source is currently connected |
| `source`    | string | Name of the connected/configured source (empty when disconnected) |

---

#### `stop_ndi`

Stops the NDI receiver and disables NDI mode. The display reverts to the current texture. The mode change is persisted to `config.json`. The configured source name is cleared.

**Request:**
```json
{ "command": "stop_ndi" }
```

**Response:**
```json
{
    "command": "stop_ndi_response",
    "success": true
}
```

---

## Command Summary

| Command            | Response command          | Auth required | Description                          |
|--------------------|---------------------------|---------------|--------------------------------------|
| `authenticate`     | `auth_response`           | No            | Authenticate connection              |
| `set_auth_key`     | `set_auth_key_response`   | Yes           | Set/update auth key (min 8 chars)    |
| `clear_auth_key`   | `clear_auth_key_response` | Yes           | Remove key, return to open mode      |
| `get_auth_status`  | `auth_status`             | Yes           | Query auth config and connection state |
| `get_device_info`  | `device_info`             | Partial*      | Get device name, host, port, texture |
| `identify`         | `identify_response`       | No            | Show device info overlay on screen   |
| `set_device_name`  | `device_name_response`    | Yes           | Rename device (persisted + mDNS)     |
| `scan_textures`    | `scan_textures_response`  | Yes           | Rescan filesystem, return list       |
| `list_textures`    | `texture_list`            | Yes           | Return known textures from memory    |
| `load_texture`     | `load_texture_response`   | Yes           | Load image file into memory          |
| `set_texture`      | `set_texture_response`    | Yes           | Switch displayed texture             |
| `scan_videos`      | `scan_videos_response`    | Yes           | Rescan media dir for video files     |
| `list_videos`      | `video_list`              | Yes           | Return known videos from last scan   |
| `play_video`       | `play_video_response`     | Yes           | Start video/stream playback          |
| `stop_video`       | `stop_video_response`     | Yes           | Stop playback, return to texture     |
| `get_video_status` | `video_status`            | Yes           | Query playback state and metadata    |
| `set_playlist`     | `set_playlist_response`   | Yes           | Set ordered video playlist           |
| `start_playlist`   | `start_playlist_response` | Yes           | Begin playlist playback              |
| `stop_playlist`    | `stop_playlist_response`  | Yes           | Stop playlist playback               |
| `next_video`       | `next_video_response`     | Yes           | Skip to next video in playlist       |
| `prev_video`       | `prev_video_response`     | Yes           | Go back to previous video            |
| `get_playlist_status` | `playlist_status`      | Yes           | Query playlist state                 |
| `scan_ndi_sources` | `ndi_sources`            | Yes           | Discover NDI sources on network      |
| `set_ndi_source`   | `set_ndi_source_response` | Yes          | Connect to an NDI source             |
| `get_ndi_status`   | `ndi_status`              | Yes          | Query NDI connection state           |
| `stop_ndi`         | `stop_ndi_response`       | Yes          | Disconnect from NDI source           |
| `set_rotation`     | `set_rotation_response`   | Yes          | Set display rotation (0/90/180/270)  |

*`get_device_info` returns a reduced response (instance name only) when unauthenticated. `identify` is always allowed regardless of auth state.

## Typical Workflows

### Initial connection (open mode)

```
Client                              Rendermatic
  |  connect ws://ip:port                |
  |------------------------------------->|
  |  {"command":"auth_status",           |
  |   "authRequired":false,              |
  |   "authenticated":true}              |
  |<-------------------------------------|
  |                                      |
  |  {"command":"get_device_info"}       |
  |------------------------------------->|
  |  {"command":"device_info",...}        |
  |<-------------------------------------|
```

### Initial connection (with authentication)

```
Client                              Rendermatic
  |  connect ws://ip:port                |
  |------------------------------------->|
  |  {"command":"auth_status",           |
  |   "authRequired":true,              |
  |   "authenticated":false}             |
  |<-------------------------------------|
  |                                      |
  |  {"command":"authenticate",          |
  |   "key":"my-secret-key"}             |
  |------------------------------------->|
  |  {"command":"auth_response",         |
  |   "success":true,...}                |
  |<-------------------------------------|
  |                                      |
  |  {"command":"get_device_info"}       |
  |------------------------------------->|
  |  {"command":"device_info",...}        |
  |<-------------------------------------|
```

### Display a texture

```
Client                              Rendermatic
  |  {"command":"scan_textures"}         |
  |------------------------------------->|
  |  {"textures":["a.png","b.jpg"]}      |
  |<-------------------------------------|
  |                                      |
  |  {"command":"set_texture",           |
  |   "texture":"a.png"}                 |
  |------------------------------------->|
  |  {"success":true}                    |
  |<-------------------------------------|
```

`set_texture` auto-loads from disk if needed. Use `load_texture` to pre-warm the cache before switching.

### Start a live stream, then stop

```
Client                              Rendermatic
  |  {"command":"play_video",            |
  |   "source":"rtmp://host/live/feed",  |
  |   "loop":false}                      |
  |------------------------------------->|
  |  {"success":true}                    |
  |<-------------------------------------|
  |                                      |
  |  {"command":"get_video_status"}      |
  |------------------------------------->|
  |  {"active":true,"width":1920,...}    |
  |<-------------------------------------|
  |                                      |
  |  {"command":"stop_video"}            |
  |------------------------------------->|
  |  {"success":true}                    |
  |<-------------------------------------|
```

### Identify a device on the network

```
Client                              Rendermatic
  |  {"command":"identify",              |
  |   "duration":5}                      |
  |------------------------------------->|
  |  {"command":"identify_response",     |
  |   "success":true, "duration":5}      |
  |<-------------------------------------|
  |                                      |
  |  (device screen shows overlay with   |
  |   hostname, IP, and port for 5s)     |
```

No authentication required — useful for scanning multiple devices to find the right one.

## Notes for Implementers

- **Authentication is optional.** Devices ship in open mode (no key). The `auth_status` message on connect tells the client whether to authenticate. Clients should handle both modes gracefully.
- **Handle `auth_status` on connect.** The first message received after connecting is always `auth_status`. Use it to decide whether to prompt for a key or proceed directly to commands.
- **One server-initiated message.** The `auth_status` on connect is the only message the server sends without a request. All other communication is request/response.
- **One response per request.** The server does not push unsolicited messages beyond the initial `auth_status`. The control client must poll (e.g. `get_video_status`) to track state changes.
- **Display priority: video > NDI > textures.** Video playback takes highest priority; when active, NDI and textures are hidden. NDI takes priority over textures. Stopping video reveals NDI (if active) or the current texture. Stopping NDI reveals the current texture.
- **Streams auto-reconnect.** Network stream sources (RTMP, RTSP, SRT) are configured with automatic reconnection. Check `get_video_status` to monitor whether the stream is still `active`.
- **Config persistence.** `play_video`, `set_device_name`, `set_auth_key`, and `clear_auth_key` persist their state to `config.json` on the device. On restart, the device will resume with the last known configuration.
- **Key management.** The plaintext key is never stored on disk -- only its SHA-256 hash. To set the initial key, connect while in open mode and call `set_auth_key`. To reset a forgotten key, manually clear `authKeyHash` from `config.json` and restart the device.
