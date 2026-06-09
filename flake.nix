{
  description = "Pebble SDK Development Environment via Pixi";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { system = system; };

        # Fundamental native packages needed for Pebble's QEMU emulator to bind to your OS graphic loop
        emulatorDeps =
          with pkgs;
          [
            libGL
            glib
            pixman
            zlib
            SDL2
            alsa-lib
            libpulseaudio
            bzip2
          ]
          ++ pkgs.lib.optionals pkgs.stdenv.isLinux [ pkgs.sndio pkgs.udev pkgs.libcap ];

        qemu-pebble-wrapped =
          if pkgs.stdenv.isLinux then
            pkgs.writeShellScriptBin "qemu-pebble" ''
              exec ${pkgs.glibc.out}/lib/ld-linux-x86-64.so.2 \
                --library-path "${pkgs.glibc.out}/lib:${pkgs.lib.makeLibraryPath emulatorDeps}" \
                /home/jtliang/.local/share/pebble-sdk/SDKs/current/toolchain/bin/qemu-pebble "$@"
            ''
          else
            null;
      in
      {
        devShells.default = pkgs.mkShellNoCC {
          buildInputs =
            with pkgs;
            [
              nodejs_24
              pixi
            ]
            ++ emulatorDeps
            ++ pkgs.lib.optionals pkgs.stdenv.isLinux [ qemu-pebble-wrapped ];

          shellHook = ''
            echo "===================================================="
            echo "▶ rePebble SDK Environment (Pixi + Nix Platform)"
            echo "▶ Pixi Version: $(pixi --version)"
            echo "===================================================="

            # Keeps the Pebble emulator from throwing missing library errors
            export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath emulatorDeps}:$LD_LIBRARY_PATH"

            ${pkgs.lib.optionalString pkgs.stdenv.isLinux ''
              export PEBBLE_QEMU_PATH="${qemu-pebble-wrapped}/bin/qemu-pebble"
            ''}

            eval "$(pixi shell-hook)"
          '';
        };
      }
    );
}
