# A/B Root Partition OTA Update System

## Context

Currently the rendermatic image has a 3-partition layout (boot + root + data) and updating requires physically reflashing. The goal is a dual-root (A/B) partition scheme where the running system can pull a new OS version over the network, write it to the inactive root partition, and reboot into it.

## Partition Layout

Current:
```
p1: 128MB   FAT32  BOOT    /boot
p2: ~1792MB ext4   root    /      (read-only)
p3: 128MB   ext4   data    /data  (read-write)
```

New (4 partitions, MBR):
```
p1: 128MB   FAT32  BOOT    /boot  (shared - GRUB + both kernels + slot.conf)
p2: ~900MB  ext4   root-a  /      (read-only, slot A)
p3: ~900MB  ext4   root-b  -      (inactive, slot B)
p4: 128MB   ext4   data    /data  (read-write, persistent)
```

Both root partitions are identically sized. Boot partition holds kernels/initrds for both slots (`vmlinuz-a`, `initrd-a.img`, `vmlinuz-b`, `initrd-b.img`) plus `slot.conf`.

Data partition is baked at 128MB but expanded on first boot to fill the remaining physical media (USB/SD/SSD/NVMe). Uses `sfdisk --force` + `resize2fs` with a `.expanded` marker to skip on subsequent boots.

## Boot Flow

`/boot/slot.conf` (shell key=value):
```
SLOT=a
```

GRUB embedded config:
```
search --label --set=root BOOT
set timeout=0
source /slot.conf
if [ "$SLOT" = "b" ]; then
    linux /vmlinuz-b root=LABEL=root-b ro quiet
    initrd /initrd-b.img
else
    linux /vmlinuz-a root=LABEL=root-a ro quiet
    initrd /initrd-a.img
fi
boot
```

Additional GRUB modules needed: `configfile`, `source`, `if`, `test`.

## First Boot: Data Partition Expansion

A oneshot systemd service (`data-expand.service`) runs before `data-mount.service`:

1. Check for `/data/.expanded` marker - if present, skip
2. Find the parent disk and partition number of `/data`
3. Unmount `/data`
4. `sfdisk --force -N PARTNUM DISK` with `", +"` to fill remaining space
5. `partprobe` / `partx -u` to reload partition table
6. `e2fsck -fy` + `resize2fs` to grow the filesystem
7. Remount `/data`
8. Write `/data/.expanded` marker

## Update Service

Systemd timer (`rendermatic-update.timer`) runs every 10 minutes. The service runs `/usr/local/sbin/ota-update.sh`:

1. Source `/data/ota.conf` for `OTA_URL` - exit if not set
2. Read `/boot/slot.conf` for current slot
3. Determine inactive slot (a->b, b->a) and its block device
4. Check rootfs and boot artifacts independently via HTTP HEAD:
   - `$OTA_URL/rootfs.img.gz` - fingerprint saved to `/data/.ota-rootfs-etag`
   - `$OTA_URL/boot.tar.gz` - fingerprint saved to `/data/.ota-boot-etag`
5. If neither changed, exit (nothing to do)
6. If rootfs changed: `wget -O - $OTA_URL/rootfs.img.gz | gunzip | dd of=/dev/INACTIVE`
7. If boot changed: remount /boot rw, `wget -O - $OTA_URL/boot.tar.gz | tar xz -C /boot`
8. If rootfs changed (regardless of boot): flip `SLOT=` in `/boot/slot.conf`
9. Reboot

Most updates are rootfs-only - the kernel/initrd rarely change. When only boot changed (no rootfs update), the current slot keeps running with the new kernel on next reboot.

### Manual trigger via SSH

```sh
# Normal update (skips if unchanged)
ssh render@device.local sudo /usr/local/sbin/ota-update.sh

# Force update (re-downloads everything)
ssh render@device.local sudo /usr/local/sbin/ota-update.sh --force
```

### Config

Per-device in `/data/ota.conf`:
```
OTA_URL=http://pentacle.lan/releases/latest
```

Set during provisioning (`flash.sh --ota-url URL`) or via SSH. If missing, update service does nothing.

### Server-side artifacts

The build pipeline produces (alongside the full disk image):
- `rendermatic-x86_64-rootfs.img.gz` - root partition ext4 image, gzipped
- `rendermatic-x86_64-boot.tar.gz` - kernel + initrd tarball

Host these on any HTTP server.

## Rollback

v1: Manual. If the new version fails to boot, use a live USB to flip `SLOT=` back in `/boot/slot.conf`.

v2 (future): GRUB environment block (`grubenv`) for automatic rollback after N failed boots.

## Files to Create

- `image/rootfs/usr/local/sbin/ota-update.sh` - update script (supports `--force` flag)
- `image/rootfs/etc/systemd/system/rendermatic-update.service` - oneshot service
- `image/rootfs/etc/systemd/system/rendermatic-update.timer` - 10min interval timer
- `image/rootfs/usr/local/sbin/data-expand.sh` - first-boot data partition expansion
- `image/rootfs/etc/systemd/system/data-expand.service` - oneshot, before data-mount

## Files to Modify

- `image/mkimage.sh` - `AB_LAYOUT=1` env var enables 4-partition layout, slot.conf, dual kernels, GRUB slot selection
- `image/build.sh` - `AB=1` flag passes `AB_LAYOUT=1` to mkimage.sh and exports rootfs + boot artifacts separately
- `image/Dockerfile` - copy OTA + expand scripts, enable timer and expand service
- `image/flash.sh` - add `--ota-url` flag

## Build Usage

```sh
AB=1 sh image/build.sh x86_64
```

Output:
```
image/output/rendermatic-x86_64.img.gz           # full A/B image for initial flash
image/output/rendermatic-x86_64-rootfs.img.gz     # root partition for OTA
image/output/rendermatic-x86_64-boot.tar.gz       # kernel+initrd for OTA
```

## Flash Usage

```sh
./flash.sh rendermatic-x86_64.img.gz /dev/sdX \
    --wifi MySSID MyPass \
    --ota-url http://pentacle.lan/releases/latest \
    --key ~/.ssh/id_ed25519.pub
```

## SSH Update Usage

```sh
# Check current slot
ssh render@device.local cat /boot/slot.conf

# Force update now
ssh render@device.local sudo /usr/local/sbin/ota-update.sh --force

# Check update log
ssh render@device.local sudo journalctl -u rendermatic-update.service
```

## Implementation Order

1. `data-expand.sh` + service - first-boot partition expansion
2. `mkimage.sh` - A/B partition layout with `AB_LAYOUT=1`
3. GRUB config with slot.conf sourcing
4. `ota-update.sh` (with --force support, independent rootfs/boot checks)
5. systemd timer + service
6. `build.sh` - `AB=1` flag + separate artifact export
7. `Dockerfile` - include and enable OTA + expand services
8. `flash.sh` - `--ota-url` flag
