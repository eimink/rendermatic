# Building Alpine Linux with Rendermatic in dfb-pure mode

This document outlines the process of setting up Alpine Linux VM to run Rendermatic in pure-dfb mode.

This guide assumes you have a your device or VM available (UTM is suggested for M1/M2/M3 Macs) and latest Alpine Linux Standard image available. While writing this, alpine-standard-3.21.2-aarch64.iso was used.

## First boot

Boot up your device/VM with the Alpine Linux ISO. On first boot, it will ask for login, use root with no password. Right after login, run the setup script:
```sh
setup-alpine
```

During installation, make sure to install ssh server and input your public key.
Also when prompted, choose your disk, eg. `vda` and set it to `sys` for persisting everything on the disk. This is mainly for making everything more straightforward during our build process.

After the installation is complete, eject your image and reboot.

## Setting up packages

SSH into your machine using your private key and start adding necessary packages.

Enable community packages by removing comment from community repository line in /etc/apk/repositories

```sh
nano /etc/apk/repositories
```

Update package index
```sh
apk update
```

Add packages needed to build rendermatic
```sh
apk add nano git cmake clang clang-dev make gcc g++ libc-dev linux-headers boost-dev directfb directfb-dev
```

## Building rendermatic

Clone repository to your device

`git clone https://github.com/eimink/rendermatic.git`

Create build directory and run cmake to build the software

```sh
cd rendermatic
mkdir build && cd build
cmake -DDFB_ONLY=ON -DDFB_PURE_ONLY=ON ..
cmake --build .
```

If everything went as planned, you can now enter the binary directory and run rendermatic!

```sh
cd bin
./rendermatic
```

## Further steps

If you manage to get it to run with X11/Wayland and GLFW, let me know ;)










