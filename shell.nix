let
  pkgs = import <nixpkgs> {};
in
pkgs.mkShell {
  buildInputs = with pkgs;
  [
    gnumake
    gcc
    ncurses
    portaudio
    sox
    libsndfile
    taglib
    libcaca
    pkg-config
    zlib
  ];
}
