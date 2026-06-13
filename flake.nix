{
  description = "Pebble SDK Development Environment via Pixi";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    pebble-nix.url = "github:pebble-dev/pebble.nix";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      pebble-nix,
    }:
    flake-utils.lib.eachDefaultSystem (
      system: {
        devShells.default = pebble-nix.pebbleEnv.${system} {
          shellHook = ''
            # Wrap pebble CLI to override HOME and keep the SDK local
            pebble() {
              HOME="$(git rev-parse --show-toplevel 2>/dev/null || pwd)/.local" command pebble "$@"
            }
            export -f pebble

            # Automatically install the Pebble SDK version locally if not already present
            sdk_version="4.9.169"
            local_dir="$(git rev-parse --show-toplevel 2>/dev/null || pwd)/.local"
            if [ ! -d "$local_dir/.pebble-sdk/SDKs/$sdk_version" ]; then
              echo "Installing Pebble SDK version $sdk_version locally..."
              pebble sdk install $sdk_version
            fi
          '';
        };
      }
    );
}
