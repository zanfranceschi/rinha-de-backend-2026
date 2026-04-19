#!/usr/bin/env bash
cd "$(dirname "$0")/.."
nix-shell -p python3Packages.matplotlib python3Packages.numpy --run "python3 visualization/visualize_14d.py"
