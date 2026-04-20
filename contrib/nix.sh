#!/usr/bin/env bash

set -e

# Re-exec into nix-shell for build dependencies on NixOS.
# Intended to be sourced by wrappers before use of build deps;
# exec replaces the caller so control does not return.
if [ -f "$(dirname "$0")/../.nixos" ] && [ -z "$IN_NIX_SHELL" ]; then
    exec nix-shell '<nixpkgs>' -A fwupd --run "$(printf '%q ' "$0" "$@")"
fi
