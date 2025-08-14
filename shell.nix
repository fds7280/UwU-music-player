let
  pkgs = import <nixpkgs> {};
in
pkgs.mkShell {
  buildInputs = with pkgs;
  [
    gnumake
    gcc
    ncurses
    pipewire
    libsndfile
    taglib
    pkg-config
    zlib
  ];
}
