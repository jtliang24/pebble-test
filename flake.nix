{
  description = "Pebble SDK Development Environment via Pixi";

  nixConfig = {
    extra-substituters = [ "https://pebble.cachix.org" ];
    extra-trusted-public-keys = [ "pebble.cachix.org-1:aTqwT2hR6lGggw/rPISRcHZctDv2iF7ewsVxf3Hq6ow=" ];
  };

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
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        pebble-wrapped = pkgs.writeShellScriptBin "pebble" ''
          export HOME="$(git rev-parse --show-toplevel 2>/dev/null || pwd)/.local"
          exec ${pebble-nix.packages.${system}.pebble-tool}/bin/pebble "$@"
        '';
      in
      {
        devShells.default = pebble-nix.pebbleEnv.${system} {
          shellHook = ''
            # Wrap pebble CLI to override HOME and keep the SDK local
            # Prepend the wrapped pebble CLI to the PATH (works with direnv, unlike bash functions)
            export PATH="${pebble-wrapped}/bin:$PATH"

            # Automatically install the Pebble SDK version locally if not already present
            sdk_version="4.9.169"
            local_dir="$(git rev-parse --show-toplevel 2>/dev/null || pwd)/.local"
            if [ ! -d "$local_dir/.pebble-sdk/SDKs/$sdk_version" ]; then
              echo "Installing Pebble SDK version $sdk_version locally..."
              pebble sdk install $sdk_version
            fi

            echo "Entering Pebble.nix Developement Environment!"
          '';
        };
      });
}
