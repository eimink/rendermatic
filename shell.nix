{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    # Build tools
    cmake
    pkg-config
    ninja
    clang
    clang-tools
    (python3.withPackages (ps: with ps; [
      glad
      jinja2
    ]))
    extra-cmake-modules

    # C++ standard library
    stdenv.cc.cc.lib
    libcxx

    # Common dependencies
    glfw3
    ffmpeg

    # Optional but useful
    ccache
  ]
  ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
    # Graphics / display
    directfb
    libGL
    libx11

    # Wayland support
    wayland
    libxkbcommon
    libffi

    # mDNS / Service Discovery
    avahi

    # Debugging (Linux only)
    gdb
    valgrind
  ];

  shellHook = ''
    # Wrap cmake so find_package(Python) always uses the nix Python (has jinja2 for GLAD)
    _nix_python="$(which python3)"
    cmake() {
      case "$1" in
        --build|--install|--open)
          command cmake "$@" ;;
        *)
          command cmake -DPython_EXECUTABLE="$_nix_python" -DPython3_EXECUTABLE="$_nix_python" "$@" ;;
      esac
    }
    export -f cmake

    echo "Rendermatic development environment loaded"
    echo "Available commands:"
    echo "  mkdir build && cd build && cmake .. && cmake --build ."
    echo "  # or for DirectFB-only (Linux):"
    echo "  mkdir build && cd build && cmake -DDFB_ONLY=ON .. && cmake --build ."
  '';
}
