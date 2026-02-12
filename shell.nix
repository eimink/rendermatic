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
    
    # Dependencies
    boost
    glfw3
    directfb
    libGL
    xorg.libX11
    
    # Wayland support
    wayland
    libxkbcommon
    libffi
    
    # Optional but useful
    gdb
    valgrind
    ccache
  ];

  shellHook = ''
    echo "🎬 Rendermatic development environment loaded"
    echo "Available commands:"
    echo "  mkdir build && cd build && cmake .. && cmake --build ."
    echo "  # or for DirectFB-only:"
    echo "  mkdir build && cd build && cmake -DDFB_ONLY=ON .. && cmake --build ."
  '';
}
