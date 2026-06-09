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
        pebble-sdk-version = "4.9.169";

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
          ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
            pkgs.sndio
            pkgs.udev
            pkgs.libcap
          ];

        qemu-pebble-wrapped =
          if pkgs.stdenv.isLinux then
            pkgs.writeShellScriptBin "qemu-pebble" ''
              exec ${pkgs.glibc.out}/lib/ld-linux-x86-64.so.2 \
                --library-path "${pkgs.glibc.out}/lib:${pkgs.lib.makeLibraryPath emulatorDeps}" \
                "''${XDG_DATA_HOME:-$HOME/.local/share}/pebble-sdk/SDKs/current/toolchain/bin/qemu-pebble" "$@"
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
            echo "▶ Pebble SDK Version: ${pebble-sdk-version}"
            echo "===================================================="

            # Point XDG_DATA_HOME to the repository's .local/share directory
            export XDG_DATA_HOME="$(git rev-parse --show-toplevel 2>/dev/null || pwd)/.local/share"

            # Keeps the Pebble emulator from throwing missing library errors
            export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath emulatorDeps}:$LD_LIBRARY_PATH"

            ${pkgs.lib.optionalString pkgs.stdenv.isLinux ''
              export PEBBLE_QEMU_PATH="${qemu-pebble-wrapped}/bin/qemu-pebble"
            ''}

            eval "$(pixi shell-hook)"

            # Ensure the pebble CLI tool is installed via Pixi
            if ! command -v pebble &> /dev/null; then
              echo "Initializing pebble-tool CLI via Pixi..."
              pixi run setup-pebble
            fi

            # Automatically install the pinned Pebble SDK version if not already present
            if [ ! -d "$XDG_DATA_HOME/pebble-sdk/SDKs/${pebble-sdk-version}" ]; then
              echo "Installing Pebble SDK version ${pebble-sdk-version}..."
              pebble sdk install ${pebble-sdk-version}
            fi
          '';
        };
      }
    );
}
